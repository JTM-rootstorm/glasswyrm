//! Developer orchestration for Glasswyrm's tiered Rust transition checks.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;

pub const HELP: &str = "\
Glasswyrm transition task runner

Usage:
  cargo xtask [--legacy-build PATH] check subsystem NAME
  cargo xtask [--legacy-build PATH] test unit
  cargo xtask [--legacy-build PATH] test contract
  cargo xtask [--legacy-build PATH] test software-acceptance
  cargo xtask [--legacy-build PATH] test headless gwcomp
  cargo xtask [--legacy-build PATH] test checkpoint gwcomp
  cargo xtask test hardware-vrr -- HARNESS_ARGS...

The legacy Meson build defaults to ./build. Set GW_LEGACY_BUILD_DIR or pass
--legacy-build to select an already configured build directory. Meson tests
are always run with --no-rebuild.

The hardware VRR task fails closed unless GW_ALLOW_HARDWARE_TESTS=1. Arguments
after -- are passed to `tools/gw-hw milestone14-vrr-test`.
";

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Cli {
    pub legacy_build: Option<PathBuf>,
    pub task: Task,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Task {
    Help,
    CheckSubsystem(String),
    Test(TestTier),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TestTier {
    Unit,
    Contract,
    SoftwareAcceptance,
    HeadlessGwcomp,
    CheckpointGwcomp,
    HardwareVrr(Vec<String>),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Context {
    pub workspace_root: PathBuf,
    pub legacy_build_env: Option<PathBuf>,
    pub hardware_allowed: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Invocation {
    pub program: OsString,
    pub args: Vec<OsString>,
    pub remove_hardware_authorization: bool,
}

impl Invocation {
    fn new(
        program: impl Into<OsString>,
        args: impl IntoIterator<Item = impl Into<OsString>>,
    ) -> Self {
        Self {
            program: program.into(),
            args: args.into_iter().map(Into::into).collect(),
            remove_hardware_authorization: true,
        }
    }

    fn display(&self) -> String {
        std::iter::once(&self.program)
            .chain(self.args.iter())
            .map(|arg| format!("{:?}", arg))
            .collect::<Vec<_>>()
            .join(" ")
    }
}

impl Cli {
    pub fn parse(arguments: impl IntoIterator<Item = String>) -> Result<Self, String> {
        let mut arguments = arguments.into_iter().peekable();
        let mut legacy_build = None;

        while let Some(argument) = arguments.peek() {
            if argument == "--legacy-build" {
                arguments.next();
                let path = arguments
                    .next()
                    .ok_or_else(|| "--legacy-build requires a path".to_owned())?;
                if path.is_empty() {
                    return Err("--legacy-build requires a non-empty path".to_owned());
                }
                legacy_build = Some(PathBuf::from(path));
            } else if let Some(path) = argument.strip_prefix("--legacy-build=") {
                if path.is_empty() {
                    return Err("--legacy-build requires a non-empty path".to_owned());
                }
                legacy_build = Some(PathBuf::from(path));
                arguments.next();
            } else {
                break;
            }
        }

        let Some(command) = arguments.next() else {
            return Err("missing command".to_owned());
        };
        let task = match command.as_str() {
            "-h" | "--help" | "help" => Task::Help,
            "check" => {
                expect(
                    &mut arguments,
                    "subsystem",
                    "expected `check subsystem NAME`",
                )?;
                let name = arguments
                    .next()
                    .ok_or_else(|| "check subsystem requires a crate name".to_owned())?;
                validate_subsystem(&name)?;
                ensure_finished(&mut arguments)?;
                Task::CheckSubsystem(name)
            }
            "test" => Task::Test(parse_test(&mut arguments)?),
            _ => return Err(format!("unknown command `{command}`")),
        };

        if task == Task::Help {
            ensure_finished(&mut arguments)?;
        }
        Ok(Self { legacy_build, task })
    }
}

fn parse_test(arguments: &mut impl Iterator<Item = String>) -> Result<TestTier, String> {
    let tier = arguments
        .next()
        .ok_or_else(|| "test requires a tier".to_owned())?;
    let tier = match tier.as_str() {
        "unit" => {
            ensure_finished(arguments)?;
            TestTier::Unit
        }
        "contract" => {
            ensure_finished(arguments)?;
            TestTier::Contract
        }
        "software-acceptance" => {
            ensure_finished(arguments)?;
            TestTier::SoftwareAcceptance
        }
        "headless" => {
            expect(
                arguments,
                "gwcomp",
                "only `test headless gwcomp` is supported",
            )?;
            ensure_finished(arguments)?;
            TestTier::HeadlessGwcomp
        }
        "checkpoint" => {
            expect(
                arguments,
                "gwcomp",
                "only `test checkpoint gwcomp` is supported",
            )?;
            ensure_finished(arguments)?;
            TestTier::CheckpointGwcomp
        }
        "hardware-vrr" => {
            let mut rest: Vec<String> = arguments.collect();
            if rest.first().is_some_and(|argument| argument == "--") {
                rest.remove(0);
            }
            TestTier::HardwareVrr(rest)
        }
        _ => return Err(format!("unknown test tier `{tier}`")),
    };
    Ok(tier)
}

fn expect(
    arguments: &mut impl Iterator<Item = String>,
    expected: &str,
    message: &str,
) -> Result<(), String> {
    match arguments.next() {
        Some(argument) if argument == expected => Ok(()),
        _ => Err(message.to_owned()),
    }
}

fn ensure_finished(arguments: &mut impl Iterator<Item = String>) -> Result<(), String> {
    match arguments.next() {
        Some(argument) => Err(format!("unexpected argument `{argument}`")),
        None => Ok(()),
    }
}

fn validate_subsystem(name: &str) -> Result<(), String> {
    if name.is_empty()
        || name.starts_with('-')
        || !name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    {
        return Err(format!("invalid subsystem crate name `{name}`"));
    }
    Ok(())
}

pub fn plan(cli: &Cli, context: &Context) -> Result<Vec<Invocation>, String> {
    let cargo = |args: &[&str]| Invocation::new("cargo", args.iter().copied());
    let legacy_build = cli
        .legacy_build
        .clone()
        .or_else(|| context.legacy_build_env.clone())
        .unwrap_or_else(|| PathBuf::from("build"));
    let meson = |selectors: &[&str]| {
        let mut args = vec![OsString::from("test"), OsString::from("-C")];
        args.push(legacy_build.as_os_str().to_owned());
        args.extend(
            ["--no-rebuild", "--print-errorlogs"]
                .into_iter()
                .map(OsString::from),
        );
        args.extend(selectors.iter().map(OsString::from));
        Invocation::new("meson", args)
    };
    let gwcomp_cargo = || cargo(&["test", "-p", "gwcomp-core"]);
    let gwcomp_headless = || meson(&["--suite", "tier3-headless-process"]);

    let invocations = match &cli.task {
        Task::Help => Vec::new(),
        Task::CheckSubsystem(name) => {
            vec![Invocation::new("cargo", ["check", "-p", name.as_str()])]
        }
        Task::Test(TestTier::Unit) => vec![
            cargo(&["test", "--workspace", "--lib"]),
            meson(&["--suite", "tier1-unit"]),
        ],
        Task::Test(TestTier::Contract) => vec![
            cargo(&["test", "--workspace", "--tests"]),
            meson(&["--suite", "tier2-contract"]),
        ],
        Task::Test(TestTier::SoftwareAcceptance) => vec![
            cargo(&["fmt", "--all", "--", "--check"]),
            cargo(&[
                "clippy",
                "--workspace",
                "--all-targets",
                "--",
                "-D",
                "warnings",
            ]),
            cargo(&["test", "--workspace"]),
            meson(&["--suite", "tier4-software"]),
        ],
        Task::Test(TestTier::HeadlessGwcomp) => vec![gwcomp_cargo(), gwcomp_headless()],
        Task::Test(TestTier::CheckpointGwcomp) => vec![
            gwcomp_cargo(),
            gwcomp_headless(),
            meson(&["--suite", "m14-runtime"]),
        ],
        Task::Test(TestTier::HardwareVrr(arguments)) => {
            if !context.hardware_allowed {
                return Err(
                    "hardware VRR tests are disabled; set GW_ALLOW_HARDWARE_TESTS=1 explicitly"
                        .to_owned(),
                );
            }
            if arguments.is_empty() {
                return Err(
                    "hardware-vrr requires gw-hw arguments after `--`; see `tools/gw-hw --help`"
                        .to_owned(),
                );
            }
            let mut harness_arguments = vec![OsString::from("milestone14-vrr-test")];
            harness_arguments.extend(arguments.iter().map(OsString::from));
            let mut invocation = Invocation::new(
                context.workspace_root.join("tools/gw-hw").into_os_string(),
                harness_arguments,
            );
            invocation.remove_hardware_authorization = false;
            vec![invocation]
        }
    };
    Ok(invocations)
}

pub fn execute(invocations: &[Invocation], workspace_root: &Path) -> Result<(), String> {
    for invocation in invocations {
        eprintln!("xtask: running {}", invocation.display());
        let mut command = Command::new(&invocation.program);
        command.args(&invocation.args).current_dir(workspace_root);
        if invocation.remove_hardware_authorization {
            command.env_remove("GW_ALLOW_HARDWARE_TESTS");
        }
        let status = command
            .status()
            .map_err(|error| format!("could not start {}: {error}", invocation.display()))?;
        if !status.success() {
            return Err(format!(
                "command failed with {status}: {}",
                invocation.display()
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn context() -> Context {
        Context {
            workspace_root: PathBuf::from("/workspace"),
            legacy_build_env: None,
            hardware_allowed: false,
        }
    }

    fn strings(invocation: &Invocation) -> Vec<String> {
        invocation
            .args
            .iter()
            .map(|arg| arg.to_string_lossy().into_owned())
            .collect()
    }

    #[test]
    fn parses_supported_tiers() {
        let cases = [
            (vec!["test", "unit"], TestTier::Unit),
            (vec!["test", "contract"], TestTier::Contract),
            (
                vec!["test", "software-acceptance"],
                TestTier::SoftwareAcceptance,
            ),
            (vec!["test", "headless", "gwcomp"], TestTier::HeadlessGwcomp),
            (
                vec!["test", "checkpoint", "gwcomp"],
                TestTier::CheckpointGwcomp,
            ),
        ];
        for (arguments, expected) in cases {
            let cli = Cli::parse(arguments.into_iter().map(str::to_owned)).unwrap();
            assert_eq!(cli.task, Task::Test(expected));
        }
    }

    #[test]
    fn check_subsystem_is_a_cargo_only_plan() {
        let cli = Cli::parse(
            ["check", "subsystem", "gwcomp"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let invocations = plan(&cli, &context()).unwrap();
        assert_eq!(invocations.len(), 1);
        assert_eq!(invocations[0].program, "cargo");
        assert_eq!(strings(&invocations[0]), ["check", "-p", "gwcomp"]);
    }

    #[test]
    fn unit_plan_runs_rust_and_legacy_unit_suites_without_rebuilding() {
        let cli = Cli::parse(["test", "unit"].into_iter().map(str::to_owned)).unwrap();
        let invocations = plan(&cli, &context()).unwrap();
        assert_eq!(invocations.len(), 2);
        assert!(
            invocations
                .iter()
                .all(|item| item.remove_hardware_authorization)
        );
        assert_eq!(invocations[0].program, "cargo");
        assert_eq!(strings(&invocations[0]), ["test", "--workspace", "--lib"]);
        assert_eq!(invocations[1].program, "meson");
        let meson = strings(&invocations[1]);
        assert!(meson.contains(&"--no-rebuild".to_owned()));
        assert!(
            meson
                .windows(2)
                .any(|pair| pair == ["--suite", "tier1-unit"])
        );
    }

    #[test]
    fn software_acceptance_includes_rust_format_lint_and_tests() {
        let cli = Cli::parse(
            ["test", "software-acceptance"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let invocations = plan(&cli, &context()).unwrap();
        assert_eq!(invocations.len(), 4);
        assert_eq!(strings(&invocations[0]), ["fmt", "--all", "--", "--check"]);
        assert_eq!(
            strings(&invocations[1]),
            [
                "clippy",
                "--workspace",
                "--all-targets",
                "--",
                "-D",
                "warnings"
            ]
        );
        assert_eq!(strings(&invocations[2]), ["test", "--workspace"]);
        assert!(
            strings(&invocations[3])
                .windows(2)
                .any(|pair| pair == ["--suite", "tier4-software"])
        );
    }

    #[test]
    fn command_line_legacy_build_overrides_environment() {
        let cli = Cli::parse(
            ["--legacy-build", "chosen", "test", "contract"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let mut context = context();
        context.legacy_build_env = Some(PathBuf::from("ignored"));
        let invocations = plan(&cli, &context).unwrap();
        assert_eq!(invocations[1].program, "meson");
        assert!(
            strings(&invocations[1])
                .windows(2)
                .any(|pair| pair == ["-C", "chosen"])
        );
        assert!(strings(&invocations[1]).contains(&"--no-rebuild".to_owned()));
    }

    #[test]
    fn headless_plan_is_narrow_and_uses_legacy_environment() {
        let cli = Cli::parse(
            ["test", "headless", "gwcomp"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let mut context = context();
        context.legacy_build_env = Some(PathBuf::from("legacy-out"));
        let invocations = plan(&cli, &context).unwrap();
        assert_eq!(invocations.len(), 2);
        assert_eq!(strings(&invocations[0]), ["test", "-p", "gwcomp-core"]);
        let meson = strings(&invocations[1]);
        assert!(meson.windows(2).any(|pair| pair == ["-C", "legacy-out"]));
        assert!(
            meson
                .windows(2)
                .any(|pair| pair == ["--suite", "tier3-headless-process"])
        );
        assert!(!meson.iter().any(|argument| argument.contains("drm")));
    }

    #[test]
    fn hardware_plan_fails_closed_without_exact_guard() {
        let cli = Cli::parse(
            ["test", "hardware-vrr", "--", "--config", "config.toml"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let error = plan(&cli, &context()).unwrap_err();
        assert!(error.contains("GW_ALLOW_HARDWARE_TESTS=1"));
    }

    #[test]
    fn guarded_hardware_plan_forwards_only_explicit_arguments() {
        let cli = Cli::parse(
            [
                "test",
                "hardware-vrr",
                "--",
                "--config",
                "config.toml",
                "--yes",
            ]
            .into_iter()
            .map(str::to_owned),
        )
        .unwrap();
        let mut context = context();
        context.hardware_allowed = true;
        let invocations = plan(&cli, &context).unwrap();
        assert_eq!(invocations.len(), 1);
        assert!(!invocations[0].remove_hardware_authorization);
        assert_eq!(invocations[0].program, "/workspace/tools/gw-hw");
        assert_eq!(
            strings(&invocations[0]),
            ["milestone14-vrr-test", "--config", "config.toml", "--yes"]
        );
    }

    #[test]
    fn malformed_commands_are_rejected() {
        for arguments in [
            vec!["check", "subsystem"],
            vec!["check", "subsystem", "--all"],
            vec!["test", "headless", "server"],
            vec!["test", "unit", "extra"],
        ] {
            assert!(Cli::parse(arguments.into_iter().map(str::to_owned)).is_err());
        }
    }
}
