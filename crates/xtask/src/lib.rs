//! Developer orchestration for Glasswyrm's tiered Rust transition checks.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;

pub const HELP: &str = "\
Glasswyrm transition task runner

Usage:
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] check subsystem NAME
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test unit
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test contract
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test software-acceptance
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test headless gwcomp
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test checkpoint gwcomp
  cargo xtask [--legacy-build PATH] [--rust-bin-dir PATH] test mixed legacy-restart|gwm|gwcomp|tools|all
  cargo xtask test hardware-vrr -- HARNESS_ARGS...

The legacy Meson build defaults to ./build. Set GW_LEGACY_BUILD_DIR or pass
--legacy-build to select an already configured build directory. Meson tests
are always run with --no-rebuild. Rust process binaries default to
./build/cargo/debug and are built by the mixed gates. Set GW_RUST_BIN_DIR or
pass --rust-bin-dir to select an already-built candidate directory instead.

The hardware VRR task fails closed unless GW_ALLOW_HARDWARE_TESTS=1. Arguments
after -- are passed to `tools/gw-hw milestone14-vrr-test`.
";

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Cli {
    pub legacy_build: Option<PathBuf>,
    pub rust_bin_dir: Option<PathBuf>,
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
    MixedLegacyRestart,
    MixedGwm,
    MixedGwcomp,
    MixedTools,
    MixedAll,
    HardwareVrr(Vec<String>),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Context {
    pub workspace_root: PathBuf,
    pub legacy_build_env: Option<PathBuf>,
    pub rust_bin_dir_env: Option<PathBuf>,
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
        let mut rust_bin_dir = None;

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
            } else if argument == "--rust-bin-dir" {
                arguments.next();
                let path = arguments
                    .next()
                    .ok_or_else(|| "--rust-bin-dir requires a path".to_owned())?;
                if path.is_empty() {
                    return Err("--rust-bin-dir requires a non-empty path".to_owned());
                }
                rust_bin_dir = Some(PathBuf::from(path));
            } else if let Some(path) = argument.strip_prefix("--rust-bin-dir=") {
                if path.is_empty() {
                    return Err("--rust-bin-dir requires a non-empty path".to_owned());
                }
                rust_bin_dir = Some(PathBuf::from(path));
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
        Ok(Self {
            legacy_build,
            rust_bin_dir,
            task,
        })
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
        "mixed" => {
            let component = arguments.next().ok_or_else(|| {
                "test mixed requires legacy-restart, gwm, gwcomp, tools, or all".to_owned()
            })?;
            ensure_finished(arguments)?;
            match component.as_str() {
                "legacy-restart" => TestTier::MixedLegacyRestart,
                "gwm" => TestTier::MixedGwm,
                "gwcomp" => TestTier::MixedGwcomp,
                "tools" => TestTier::MixedTools,
                "all" => TestTier::MixedAll,
                _ => {
                    return Err(
                        "test mixed requires legacy-restart, gwm, gwcomp, tools, or all".to_owned(),
                    );
                }
            }
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
    let rust_bin_dir = cli
        .rust_bin_dir
        .clone()
        .or_else(|| context.rust_bin_dir_env.clone())
        .unwrap_or_else(|| PathBuf::from("build/cargo/debug"));
    let build_default_rust_binaries =
        cli.rust_bin_dir.is_none() && context.rust_bin_dir_env.is_none();
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
    let gwcomp_headless = || {
        meson(&[
            "gwcomp-metadata-process",
            "gwcomp-process",
            "gwcomp-output-inventory-process",
            "gwcomp-golden",
            "gwcomp-scenario-matrix",
        ])
    };
    let legacy_restart = || {
        let mut invocation = cargo(&[
            "run",
            "--locked",
            "-p",
            "gw-transition-tests",
            "--bin",
            "legacy-output-restart",
            "--",
            "--build-dir",
        ]);
        invocation.args.push(legacy_build.as_os_str().to_owned());
        invocation
    };
    let rust_gwm = || rust_bin_dir.join("gwm");
    let rust_gwcomp = || rust_bin_dir.join("gwcomp");
    let rust_gwinfo = || rust_bin_dir.join("gwinfo");
    let rust_gwout = || rust_bin_dir.join("gwout");
    let legacy = |path: &str| legacy_build.join(path);
    let rust_gwm_gate = || {
        let mut invocations = Vec::new();
        if build_default_rust_binaries {
            invocations.push(cargo(&["build", "--locked", "-p", "gwm", "--bin", "gwm"]));
        }
        invocations.extend([
            Invocation::new(
                legacy("tests/manifest/foundation/wm/gwm_process_test").into_os_string(),
                [rust_gwm().into_os_string()],
            ),
            Invocation::new(
                legacy("tests/manifest/foundation/wm/gwm_vrr_process_test").into_os_string(),
                [rust_gwm().into_os_string()],
            ),
            Invocation::new(
                legacy("tests/manifest/foundation/wm/gwm_scenario_matrix_test").into_os_string(),
                [
                    rust_gwm().into_os_string(),
                    legacy("src/gwm_m5_producer").into_os_string(),
                    context
                        .workspace_root
                        .join("tests/fixtures/m5")
                        .into_os_string(),
                ],
            ),
            Invocation::new(
                legacy("tests/manifest/foundation/wm/gwm_robustness_test").into_os_string(),
                [rust_gwm().into_os_string()],
            ),
        ]);
        invocations
    };
    let rust_tools_gate = || {
        let mut invocations = Vec::new();
        if build_default_rust_binaries {
            invocations.push(cargo(&["build", "--locked", "-p", "gw-tools", "--bins"]));
        }
        invocations.push(Invocation::new(
            context
                .workspace_root
                .join("tests/tools/output_tools_test.sh")
                .into_os_string(),
            [
                legacy("tests/manifest/x11_tools/output_tools_fake_server").into_os_string(),
                rust_gwinfo().into_os_string(),
                rust_gwout().into_os_string(),
            ],
        ));
        invocations
    };
    let rust_gwcomp_gate = || {
        let mut invocations = Vec::new();
        if build_default_rust_binaries {
            invocations.push(cargo(&[
                "build", "--locked", "-p", "gwcomp", "--bin", "gwcomp",
            ]));
        }
        let candidate = || [rust_gwcomp().into_os_string()];
        invocations.extend([
            Invocation::new(
                legacy("tests/manifest/graphics/headless/gwcomp_process_test").into_os_string(),
                candidate(),
            ),
            Invocation::new(
                legacy("tests/manifest/graphics/headless/gwcomp_metadata_process_test")
                    .into_os_string(),
                candidate(),
            ),
            Invocation::new(
                legacy("tests/manifest/graphics/headless/gwcomp_output_inventory_process_test")
                    .into_os_string(),
                candidate(),
            ),
            Invocation::new(
                legacy("tests/manifest/graphics/headless/gwcomp_golden_test").into_os_string(),
                [
                    rust_gwcomp().into_os_string(),
                    legacy("src/gwcomp_m4_producer").into_os_string(),
                ],
            ),
            Invocation::new(
                legacy("tests/manifest/graphics/headless/gwcomp_scenario_matrix_test")
                    .into_os_string(),
                [
                    rust_gwcomp().into_os_string(),
                    legacy("src/gwcomp_m4_producer").into_os_string(),
                ],
            ),
            Invocation::new(
                context
                    .workspace_root
                    .join("tests/apps/m14_vrr_client_runtime_test.sh")
                    .into_os_string(),
                [
                    legacy("src/gwm").into_os_string(),
                    rust_gwcomp().into_os_string(),
                    legacy("src/glasswyrmd").into_os_string(),
                    legacy("tests/manifest/m14/m14_vrr_client").into_os_string(),
                    context
                        .workspace_root
                        .join("tests/compat/m14/validate_client_state.py")
                        .into_os_string(),
                    legacy("tools/gwinfo").into_os_string(),
                    legacy("tools/gwout").into_os_string(),
                ],
            ),
        ]);
        invocations
    };
    let mixed_all = || {
        let mut invocations = vec![legacy_restart()];
        invocations.extend(rust_gwm_gate());
        invocations.extend(rust_gwcomp_gate());
        invocations.extend(rust_tools_gate());
        invocations
    };

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
        Task::Test(TestTier::SoftwareAcceptance) => {
            let mut invocations = vec![
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
            ];
            invocations.extend(mixed_all());
            invocations
        }
        Task::Test(TestTier::HeadlessGwcomp) => vec![gwcomp_cargo(), gwcomp_headless()],
        Task::Test(TestTier::CheckpointGwcomp) => vec![
            gwcomp_cargo(),
            gwcomp_headless(),
            meson(&["--suite", "m14-runtime"]),
        ],
        Task::Test(TestTier::MixedLegacyRestart) => vec![legacy_restart()],
        Task::Test(TestTier::MixedGwm) => rust_gwm_gate(),
        Task::Test(TestTier::MixedGwcomp) => rust_gwcomp_gate(),
        Task::Test(TestTier::MixedTools) => rust_tools_gate(),
        Task::Test(TestTier::MixedAll) => mixed_all(),
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
            rust_bin_dir_env: None,
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
            (
                vec!["test", "mixed", "legacy-restart"],
                TestTier::MixedLegacyRestart,
            ),
            (vec!["test", "mixed", "gwm"], TestTier::MixedGwm),
            (vec!["test", "mixed", "gwcomp"], TestTier::MixedGwcomp),
            (vec!["test", "mixed", "tools"], TestTier::MixedTools),
            (vec!["test", "mixed", "all"], TestTier::MixedAll),
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
        assert_eq!(invocations.len(), 19);
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
        assert_eq!(invocations[4].program, "cargo");
        assert!(strings(&invocations[4]).contains(&"legacy-output-restart".to_owned()));
        assert_eq!(
            invocations[18].program,
            "/workspace/tests/tools/output_tools_test.sh"
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
        assert!(meson.contains(&"gwcomp-process".to_owned()));
        assert!(meson.contains(&"gwcomp-golden".to_owned()));
        assert!(!meson.contains(&"--suite".to_owned()));
        assert!(!meson.iter().any(|argument| argument.starts_with("gwm-")));
        assert!(!meson.iter().any(|argument| argument.contains("drm")));
    }

    #[test]
    fn mixed_gwm_plan_uses_configurable_candidate_and_legacy_paths() {
        let cli = Cli::parse(
            [
                "--legacy-build",
                "legacy-out",
                "--rust-bin-dir",
                "rust-out",
                "test",
                "mixed",
                "gwm",
            ]
            .into_iter()
            .map(str::to_owned),
        )
        .unwrap();
        let invocations = plan(&cli, &context()).unwrap();
        assert_eq!(invocations.len(), 4);
        assert_eq!(
            invocations[0].program,
            "legacy-out/tests/manifest/foundation/wm/gwm_process_test"
        );
        assert_eq!(strings(&invocations[0]), ["rust-out/gwm"]);
        assert_eq!(
            invocations[2].program,
            "legacy-out/tests/manifest/foundation/wm/gwm_scenario_matrix_test"
        );
        assert_eq!(
            strings(&invocations[2]),
            [
                "rust-out/gwm",
                "legacy-out/src/gwm_m5_producer",
                "/workspace/tests/fixtures/m5"
            ]
        );
        assert!(
            invocations
                .iter()
                .all(|invocation| invocation.remove_hardware_authorization)
        );
    }

    #[test]
    fn mixed_tools_plan_runs_legacy_fake_server_against_rust_binaries() {
        let cli = Cli::parse(["test", "mixed", "tools"].into_iter().map(str::to_owned)).unwrap();
        let mut context = context();
        context.legacy_build_env = Some(PathBuf::from("legacy-env"));
        context.rust_bin_dir_env = Some(PathBuf::from("rust-env"));
        let invocations = plan(&cli, &context).unwrap();
        assert_eq!(invocations.len(), 1);
        assert_eq!(
            invocations[0].program,
            "/workspace/tests/tools/output_tools_test.sh"
        );
        assert_eq!(
            strings(&invocations[0]),
            [
                "legacy-env/tests/manifest/x11_tools/output_tools_fake_server",
                "rust-env/gwinfo",
                "rust-env/gwout"
            ]
        );
    }

    #[test]
    fn mixed_gwcomp_plan_runs_retained_legacy_probes_against_rust_candidate() {
        let cli = Cli::parse(
            [
                "--legacy-build",
                "legacy-out",
                "--rust-bin-dir",
                "rust-out",
                "test",
                "mixed",
                "gwcomp",
            ]
            .into_iter()
            .map(str::to_owned),
        )
        .unwrap();
        let invocations = plan(&cli, &context()).unwrap();
        assert_eq!(invocations.len(), 6);
        assert_eq!(
            invocations[0].program,
            "legacy-out/tests/manifest/graphics/headless/gwcomp_process_test"
        );
        assert_eq!(strings(&invocations[0]), ["rust-out/gwcomp"]);
        assert_eq!(
            invocations[1].program,
            "legacy-out/tests/manifest/graphics/headless/gwcomp_metadata_process_test"
        );
        assert_eq!(strings(&invocations[1]), ["rust-out/gwcomp"]);
        assert_eq!(
            invocations[2].program,
            "legacy-out/tests/manifest/graphics/headless/gwcomp_output_inventory_process_test"
        );
        assert_eq!(strings(&invocations[2]), ["rust-out/gwcomp"]);
        assert_eq!(
            invocations[3].program,
            "legacy-out/tests/manifest/graphics/headless/gwcomp_golden_test"
        );
        assert_eq!(
            strings(&invocations[3]),
            ["rust-out/gwcomp", "legacy-out/src/gwcomp_m4_producer"]
        );
        assert_eq!(
            invocations[4].program,
            "legacy-out/tests/manifest/graphics/headless/gwcomp_scenario_matrix_test"
        );
        assert_eq!(
            strings(&invocations[4]),
            ["rust-out/gwcomp", "legacy-out/src/gwcomp_m4_producer"]
        );
        assert_eq!(
            invocations[5].program,
            "/workspace/tests/apps/m14_vrr_client_runtime_test.sh"
        );
        assert_eq!(
            strings(&invocations[5]),
            [
                "legacy-out/src/gwm",
                "rust-out/gwcomp",
                "legacy-out/src/glasswyrmd",
                "legacy-out/tests/manifest/m14/m14_vrr_client",
                "/workspace/tests/compat/m14/validate_client_state.py",
                "legacy-out/tools/gwinfo",
                "legacy-out/tools/gwout"
            ]
        );
        assert!(
            invocations
                .iter()
                .all(|invocation| invocation.remove_hardware_authorization)
        );
    }

    #[test]
    fn mixed_legacy_restart_preserves_the_legacy_oracle_boundary() {
        let cli = Cli::parse(
            ["test", "mixed", "legacy-restart"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        let mut context = context();
        context.legacy_build_env = Some(PathBuf::from("legacy-env"));
        let invocations = plan(&cli, &context).unwrap();
        assert_eq!(invocations.len(), 1);
        assert_eq!(invocations[0].program, "cargo");
        assert_eq!(
            strings(&invocations[0]),
            [
                "run",
                "--locked",
                "-p",
                "gw-transition-tests",
                "--bin",
                "legacy-output-restart",
                "--",
                "--build-dir",
                "legacy-env"
            ]
        );
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
            vec!["test", "mixed"],
            vec!["test", "mixed", "unknown"],
            vec!["test", "unit", "extra"],
            vec!["--rust-bin-dir", "", "test", "mixed", "gwm"],
        ] {
            assert!(Cli::parse(arguments.into_iter().map(str::to_owned)).is_err());
        }
    }
}
