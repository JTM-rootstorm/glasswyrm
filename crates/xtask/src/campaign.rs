use crate::{Cli, Context, Invocation, Task, TestTier, plan};
use gw_test_support::{sha256, sha256_file};
use std::collections::{BTreeMap, BTreeSet};
use std::ffi::{OsStr, OsString};
use std::fmt::Write as _;
use std::fs::{self, File};
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct CampaignOptions {
    pub list: bool,
    pub only: Vec<String>,
    pub from: Option<String>,
    pub resume: bool,
    pub rerun_failed: bool,
    pub inject_failure: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ReplayOptions {
    M14Probe {
        report: PathBuf,
        config: PathBuf,
    },
    M14Fixture {
        fixture_dir: PathBuf,
        config: PathBuf,
        required_base: String,
        tested_commit: String,
    },
}

#[derive(Clone, Debug)]
pub struct CampaignStep {
    pub name: String,
    pub dependencies: Vec<String>,
    pub invocation: Invocation,
    pub source_inputs: Vec<PathBuf>,
    pub timeout_seconds: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum CachedStatus {
    Passed,
    Failed,
}

#[derive(Clone, Debug)]
struct CacheState {
    status: CachedStatus,
    input_hash: String,
    evidence_run: Option<String>,
    stdout_sha256: Option<String>,
    stderr_sha256: Option<String>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum StepStatus {
    Passed,
    Failed,
    Blocked,
    Cached,
}

impl StepStatus {
    fn as_str(self) -> &'static str {
        match self {
            Self::Passed => "passed",
            Self::Failed => "failed",
            Self::Blocked => "blocked",
            Self::Cached => "cached",
        }
    }
    fn satisfies_dependency(self) -> bool {
        matches!(self, Self::Passed | Self::Cached)
    }
}

#[derive(Clone, Debug)]
struct StepResult {
    status: StepStatus,
    input_hash: String,
    detail: String,
    evidence_run: Option<String>,
    stdout_sha256: Option<String>,
    stderr_sha256: Option<String>,
}

pub fn parse_campaign(
    arguments: &mut impl Iterator<Item = String>,
) -> Result<CampaignOptions, String> {
    let mut options = CampaignOptions::default();
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--list" => options.list = true,
            "--resume" => options.resume = true,
            "--rerun-failed" => options.rerun_failed = true,
            "--only" => {
                let value = arguments
                    .next()
                    .ok_or_else(|| "--only requires a step name".to_owned())?;
                push_only(&mut options.only, &value)?;
            }
            "--from" => {
                let value = arguments
                    .next()
                    .ok_or_else(|| "--from requires a step name".to_owned())?;
                if value.is_empty() {
                    return Err("--from requires a non-empty step name".to_owned());
                }
                options.from = Some(value);
            }
            "--inject-failure" => {
                let value = arguments
                    .next()
                    .ok_or_else(|| "--inject-failure requires a step name".to_owned())?;
                if value.is_empty() {
                    return Err("--inject-failure requires a non-empty step name".to_owned());
                }
                options.inject_failure = Some(value);
            }
            value if value.starts_with("--only=") => push_only(&mut options.only, &value[7..])?,
            value if value.starts_with("--from=") => {
                options.from = Some(nonempty(value[7..].to_owned(), "--from")?)
            }
            value if value.starts_with("--inject-failure=") => {
                options.inject_failure = Some(nonempty(value[17..].to_owned(), "--inject-failure")?)
            }
            _ => return Err(format!("unknown campaign option `{argument}`")),
        }
    }
    if options.list
        && (options.resume
            || options.rerun_failed
            || options.from.is_some()
            || !options.only.is_empty()
            || options.inject_failure.is_some())
    {
        return Err("--list cannot be combined with campaign execution selectors".to_owned());
    }
    Ok(options)
}

fn nonempty(value: String, option: &str) -> Result<String, String> {
    if value.is_empty() {
        Err(format!("{option} requires a non-empty value"))
    } else {
        Ok(value)
    }
}

fn push_only(output: &mut Vec<String>, value: &str) -> Result<(), String> {
    for item in value.split(',') {
        if item.is_empty() {
            return Err("--only contains an empty step name".to_owned());
        }
        output.push(item.to_owned());
    }
    Ok(())
}

pub fn parse_replay(arguments: &mut impl Iterator<Item = String>) -> Result<ReplayOptions, String> {
    let mode = arguments
        .next()
        .ok_or_else(|| "replay requires `m14-probe` or `m14-fixture`".to_owned())?;
    let mut values = BTreeMap::<String, String>::new();
    while let Some(argument) = arguments.next() {
        if !argument.starts_with("--") {
            return Err(format!("unexpected replay argument `{argument}`"));
        }
        let key = argument.trim_start_matches("--").to_owned();
        let value = arguments
            .next()
            .ok_or_else(|| format!("--{key} requires a value"))?;
        if value.is_empty() || values.insert(key.clone(), value).is_some() {
            return Err(format!("invalid or duplicate replay option --{key}"));
        }
    }
    let take_path =
        |name: &str, values: &mut BTreeMap<String, String>| -> Result<PathBuf, String> {
            values
                .remove(name)
                .map(PathBuf::from)
                .ok_or_else(|| format!("replay requires --{name}"))
        };
    let result = match mode.as_str() {
        "m14-probe" => ReplayOptions::M14Probe {
            report: take_path("report", &mut values)?,
            config: take_path("config", &mut values)?,
        },
        "m14-fixture" => ReplayOptions::M14Fixture {
            fixture_dir: take_path("fixture-dir", &mut values)?,
            config: take_path("config", &mut values)?,
            required_base: values
                .remove("required-base")
                .ok_or_else(|| "replay requires --required-base".to_owned())?,
            tested_commit: values
                .remove("tested-commit")
                .ok_or_else(|| "replay requires --tested-commit".to_owned())?,
        },
        _ => return Err(format!("unknown replay mode `{mode}`")),
    };
    if let Some(name) = values.keys().next() {
        return Err(format!("unknown replay option --{name}"));
    }
    Ok(result)
}

pub fn campaign_steps(cli: &Cli, context: &Context) -> Result<Vec<CampaignStep>, String> {
    let mut acceptance_cli = cli.clone();
    acceptance_cli.task = Task::Test(TestTier::SoftwareAcceptance);
    let invocations = plan(&acceptance_cli, context)?;
    let mut names = BTreeSet::new();
    let mut steps = Vec::new();
    for invocation in invocations {
        let name = classify(&invocation)?;
        if !names.insert(name.clone()) {
            return Err(format!("campaign step name is not unique: {name}"));
        }
        steps.push(CampaignStep {
            dependencies: Vec::new(),
            source_inputs: source_inputs(&name),
            timeout_seconds: timeout_for(&name),
            name,
            invocation,
        });
    }
    let inventory_name = "transition-inventory-check".to_owned();
    steps.push(CampaignStep {
        name: inventory_name.clone(),
        dependencies: Vec::new(),
        invocation: Invocation::new(
            "python3",
            ["scripts/generate-rust-transition-inventory.py", "--check"],
        ),
        source_inputs: vec![
            PathBuf::from("scripts/generate-rust-transition-inventory.py"),
            PathBuf::from("docs/maintenance/RUST_TRANSITION_WP0_INVENTORY.json"),
            PathBuf::from("crates/glasswyrm-x11/src/core.rs"),
            PathBuf::from("crates/glasswyrm-x11/src/dispatch.rs"),
            PathBuf::from("src/glasswyrmd/request_dispatcher.cpp"),
            PathBuf::from("src/glasswyrmd/extension_registry.hpp"),
        ],
        timeout_seconds: 30,
    });
    names.insert(inventory_name);
    let harness_name = "hardware-harness-selftest".to_owned();
    steps.push(CampaignStep {
        name: harness_name.clone(),
        dependencies: Vec::new(),
        invocation: Invocation::new(
            context.workspace_root.join("tools/gw-hw").into_os_string(),
            ["self-test"],
        ),
        source_inputs: vec![
            PathBuf::from("tools/gw-hw"),
            PathBuf::from("tools/gw-hw.d"),
            PathBuf::from("tests/hardware/m14"),
        ],
        timeout_seconds: 60,
    });
    names.insert(harness_name);
    for step in &mut steps {
        let dependency = if step.name.starts_with("gwm-")
            && step.name != "gwm-build"
            && names.contains("gwm-build")
        {
            Some("gwm-build")
        } else if step.name.starts_with("gwcomp-")
            && step.name != "gwcomp-build"
            && names.contains("gwcomp-build")
        {
            Some("gwcomp-build")
        } else if step.name == "tools-output" && names.contains("tools-build") {
            Some("tools-build")
        } else {
            None
        };
        if let Some(dependency) = dependency {
            step.dependencies.push(dependency.to_owned());
        }
    }
    Ok(steps)
}

fn classify(invocation: &Invocation) -> Result<String, String> {
    let program = Path::new(&invocation.program);
    let leaf = program
        .file_name()
        .and_then(OsStr::to_str)
        .unwrap_or_default();
    let args = invocation
        .args
        .iter()
        .map(|value| value.to_string_lossy())
        .collect::<Vec<_>>();
    let contains = |needle: &str| {
        args.iter()
            .any(|value| value.as_ref() == needle || value.contains(needle))
    };
    let result = if leaf == "cargo" && args.first().is_some_and(|v| v.as_ref() == "fmt") {
        "rust-fmt"
    } else if leaf == "cargo" && args.first().is_some_and(|v| v.as_ref() == "clippy") {
        "rust-clippy"
    } else if leaf == "cargo"
        && args.first().is_some_and(|v| v.as_ref() == "test")
        && contains("--workspace")
    {
        "rust-workspace-test"
    } else if leaf == "meson" && contains("tier4-software") {
        "legacy-tier4"
    } else if leaf == "cargo" && contains("legacy-output-restart") {
        "legacy-restart"
    } else if leaf == "cargo"
        && contains("gwm")
        && args.first().is_some_and(|v| v.as_ref() == "build")
    {
        "gwm-build"
    } else if leaf == "gwm_process_test" {
        "gwm-process"
    } else if leaf == "gwm_vrr_process_test" {
        "gwm-vrr"
    } else if leaf == "gwm_scenario_matrix_test" {
        "gwm-scenario"
    } else if leaf == "gwm_robustness_test" {
        "gwm-robustness"
    } else if leaf == "cargo"
        && contains("gwcomp")
        && args.first().is_some_and(|v| v.as_ref() == "build")
    {
        "gwcomp-build"
    } else if leaf == "gwcomp_process_test" {
        "gwcomp-process"
    } else if leaf == "gwcomp_metadata_process_test" {
        "gwcomp-metadata"
    } else if leaf == "gwcomp_output_inventory_process_test" {
        "gwcomp-output-inventory"
    } else if leaf == "gwcomp_golden_test" {
        "gwcomp-golden"
    } else if leaf == "gwcomp_scenario_matrix_test" {
        "gwcomp-scenario"
    } else if leaf == "m14_vrr_client_runtime_test.sh" {
        "gwcomp-m14-runtime"
    } else if leaf == "cargo"
        && contains("gw-tools")
        && args.first().is_some_and(|v| v.as_ref() == "build")
    {
        "tools-build"
    } else if leaf == "output_tools_test.sh" {
        "tools-output"
    } else {
        return Err(format!(
            "software acceptance invocation has no campaign identity: {}",
            invocation.display()
        ));
    };
    Ok(result.to_owned())
}

fn source_inputs(name: &str) -> Vec<PathBuf> {
    let common = ["Cargo.toml", "Cargo.lock", "rust-toolchain.toml"].map(PathBuf::from);
    let mut result = Vec::from(common);
    if name.starts_with("rust-") {
        result.push("crates".into());
    } else if name.starts_with("legacy-") {
        result.extend(
            [
                "meson.build",
                "meson_options.txt",
                "include",
                "src",
                "tests",
            ]
            .map(PathBuf::from),
        );
    } else if name.starts_with("gwm-") {
        result.extend(
            [
                "crates/gwm",
                "crates/gwm-core",
                "crates/gw-ipc",
                "crates/gw-wire",
                "crates/gw-types",
                "tests/fixtures/m5",
            ]
            .map(PathBuf::from),
        );
    } else if name.starts_with("gwcomp-") {
        result.extend(
            [
                "crates/gwcomp",
                "crates/gwcomp-core",
                "crates/gw-ipc",
                "crates/gw-wire",
                "crates/gw-types",
                "tests/fixtures/m4",
                "tests/compat/m14",
            ]
            .map(PathBuf::from),
        );
    } else if name.starts_with("tools-") {
        result.extend(
            [
                "crates/gw-tools",
                "crates/gw-ipc",
                "crates/gw-wire",
                "crates/gw-types",
                "tests/tools",
            ]
            .map(PathBuf::from),
        );
    }
    result
}

fn timeout_for(name: &str) -> u64 {
    match name {
        "legacy-tier4" | "rust-workspace-test" | "rust-clippy" => 900,
        _ => 180,
    }
}

pub fn run_campaign(cli: &Cli, context: &Context, options: &CampaignOptions) -> Result<(), String> {
    let steps = campaign_steps(cli, context)?;
    validate_requested_steps(&steps, options)?;
    if options.list {
        for step in &steps {
            let dependencies = if step.dependencies.is_empty() {
                "-".to_owned()
            } else {
                step.dependencies.join(",")
            };
            println!(
                "{}\tdeps={}\ttimeout={}s\t{}",
                step.name,
                dependencies,
                step.timeout_seconds,
                relative_command(&step.invocation, &context.workspace_root)
            );
        }
        return Ok(());
    }

    let development_root = context
        .workspace_root
        .join("artifacts/rust-transition/development");
    let cache_root = development_root.join("cache");
    fs::create_dir_all(&cache_root).map_err(io_error("create campaign cache"))?;
    let run_id = run_id();
    let run_root = development_root.join("runs").join(&run_id);
    fs::create_dir_all(run_root.join("steps"))
        .map_err(io_error("create campaign run directory"))?;
    fs::create_dir_all(run_root.join("failures"))
        .map_err(io_error("create campaign failure directory"))?;
    let short_tmp = context
        .workspace_root
        .parent()
        .unwrap_or(&context.workspace_root)
        .join(".t");
    fs::create_dir_all(&short_tmp).map_err(io_error("create short project-local temp root"))?;

    let selected = select_steps(&steps, options, &cache_root)?;
    let from_index = options
        .from
        .as_ref()
        .and_then(|name| steps.iter().position(|step| &step.name == name));
    let revision = git_output(&context.workspace_root, &["rev-parse", "HEAD"])
        .unwrap_or_else(|_| "unknown".to_owned());
    let dirty = git_output(&context.workspace_root, &["status", "--porcelain"])
        .map(|value| !value.is_empty())
        .unwrap_or(true);
    let mut results = BTreeMap::<String, StepResult>::new();
    let mut fingerprints = BTreeMap::<String, String>::new();
    let mut any_failure = false;

    for (index, step) in steps.iter().enumerate() {
        if !selected.contains(&step.name) {
            continue;
        }
        let blocked_by = step
            .dependencies
            .iter()
            .find(|dependency| {
                results
                    .get(*dependency)
                    .is_some_and(|result| !result.status.satisfies_dependency())
            })
            .cloned();
        if let Some(dependency) = blocked_by {
            results.insert(
                step.name.clone(),
                StepResult {
                    status: StepStatus::Blocked,
                    input_hash: "blocked".to_owned(),
                    detail: format!("dependency {dependency} did not pass"),
                    evidence_run: None,
                    stdout_sha256: None,
                    stderr_sha256: None,
                },
            );
            any_failure = true;
            continue;
        }

        let (input_hash, input_manifest) = fingerprint_step(step, context, &fingerprints)?;
        fingerprints.insert(step.name.clone(), input_hash.clone());
        let cached = read_cache(&cache_root, &step.name)?;
        let before_from = from_index.is_some_and(|from| index < from);
        let may_reuse = options.resume || options.rerun_failed || before_from;
        if may_reuse
            && required_outputs_present(step, context)
            && let Some(state) = cached.as_ref()
            && state.status == CachedStatus::Passed
            && state.input_hash == input_hash
            && cached_evidence_valid(&development_root, step, state)
        {
            eprintln!("xtask: campaign {} cached", step.name);
            results.insert(
                step.name.clone(),
                StepResult {
                    status: StepStatus::Cached,
                    input_hash,
                    detail: "matching successful inputs and retained evidence reused".to_owned(),
                    evidence_run: state.evidence_run.clone(),
                    stdout_sha256: state.stdout_sha256.clone(),
                    stderr_sha256: state.stderr_sha256.clone(),
                },
            );
            continue;
        }
        if before_from {
            results.insert(
                step.name.clone(),
                StepResult {
                    status: StepStatus::Blocked,
                    input_hash,
                    detail: "--from requires a matching cached prerequisite".to_owned(),
                    evidence_run: None,
                    stdout_sha256: None,
                    stderr_sha256: None,
                },
            );
            any_failure = true;
            continue;
        }

        let step_dir = run_root.join("steps");
        let stdout_path = step_dir.join(format!("{}.stdout", step.name));
        let stderr_path = step_dir.join(format!("{}.stderr", step.name));
        let injected = options.inject_failure.as_deref() == Some(step.name.as_str());
        eprintln!(
            "xtask: campaign {} running {}",
            step.name,
            relative_command(&step.invocation, &context.workspace_root)
        );
        let execution = if injected {
            fs::write(&stdout_path, b"").map_err(io_error("write injected stdout"))?;
            fs::write(
                &stderr_path,
                b"xtask: deliberately injected campaign failure\n",
            )
            .map_err(io_error("write injected stderr"))?;
            ExecutionOutcome {
                passed: false,
                status: "injected-failure".to_owned(),
            }
        } else {
            execute_captured(step, context, &short_tmp, &stdout_path, &stderr_path)?
        };
        let stdout_sha256 = sha256_hex(&stdout_path)?;
        let stderr_sha256 = sha256_hex(&stderr_path)?;
        if execution.passed {
            write_cache(
                &cache_root,
                &step.name,
                CachedStatus::Passed,
                &input_hash,
                &run_id,
                &stdout_sha256,
                &stderr_sha256,
            )?;
            results.insert(
                step.name.clone(),
                StepResult {
                    status: StepStatus::Passed,
                    input_hash,
                    detail: execution.status,
                    evidence_run: Some(run_id.clone()),
                    stdout_sha256: Some(stdout_sha256),
                    stderr_sha256: Some(stderr_sha256),
                },
            );
        } else {
            write_cache(
                &cache_root,
                &step.name,
                CachedStatus::Failed,
                &input_hash,
                &run_id,
                &stdout_sha256,
                &stderr_sha256,
            )?;
            let evidence = FailureEvidence {
                input_hash: &input_hash,
                input_manifest: &input_manifest,
                status: &execution.status,
                stdout_path: &stdout_path,
                stderr_path: &stderr_path,
                stdout_sha256: &stdout_sha256,
                stderr_sha256: &stderr_sha256,
            };
            write_failure_bundle(&run_root, step, context, &evidence)?;
            results.insert(
                step.name.clone(),
                StepResult {
                    status: StepStatus::Failed,
                    input_hash,
                    detail: execution.status,
                    evidence_run: Some(run_id.clone()),
                    stdout_sha256: Some(stdout_sha256),
                    stderr_sha256: Some(stderr_sha256),
                },
            );
            any_failure = true;
        }
    }

    let summary = RunSummary {
        run_id: &run_id,
        revision: &revision,
        dirty,
        selected: &selected,
        results: &results,
    };
    let manifest = write_run_manifest(&run_root, &steps, context, &summary)?;
    if any_failure {
        Err(format!(
            "campaign did not pass; development manifest: {}",
            relative_to(&manifest, &context.workspace_root)
        ))
    } else {
        eprintln!(
            "xtask: campaign passed; manifest {}",
            relative_to(&manifest, &context.workspace_root)
        );
        Ok(())
    }
}

fn required_output_paths(step: &CampaignStep) -> &'static [&'static str] {
    match step.name.as_str() {
        "gwm-build" => &["build/cargo/debug/gwm"],
        "gwcomp-build" => &["build/cargo/debug/gwcomp"],
        "tools-build" => &["build/cargo/debug/gwinfo", "build/cargo/debug/gwout"],
        _ => &[],
    }
}

fn required_outputs_present(step: &CampaignStep, context: &Context) -> bool {
    required_output_paths(step)
        .iter()
        .all(|path| context.workspace_root.join(path).is_file())
}

fn sha256_hex(path: &Path) -> Result<String, String> {
    sha256_file(path)
        .map(|digest| digest.to_hex())
        .map_err(|error| format!("hash campaign evidence {}: {error}", path.display()))
}

fn cached_evidence_valid(development_root: &Path, step: &CampaignStep, state: &CacheState) -> bool {
    let (Some(run), Some(stdout_hash), Some(stderr_hash)) = (
        state.evidence_run.as_deref(),
        state.stdout_sha256.as_deref(),
        state.stderr_sha256.as_deref(),
    ) else {
        return false;
    };
    let step_root = development_root.join("runs").join(run).join("steps");
    let stdout = step_root.join(format!("{}.stdout", step.name));
    let stderr = step_root.join(format!("{}.stderr", step.name));
    sha256_file(&stdout).is_ok_and(|digest| digest.to_hex() == stdout_hash)
        && sha256_file(&stderr).is_ok_and(|digest| digest.to_hex() == stderr_hash)
}

fn validate_requested_steps(
    steps: &[CampaignStep],
    options: &CampaignOptions,
) -> Result<(), String> {
    let names = steps
        .iter()
        .map(|step| step.name.as_str())
        .collect::<BTreeSet<_>>();
    for name in options
        .only
        .iter()
        .chain(options.from.iter())
        .chain(options.inject_failure.iter())
    {
        if !names.contains(name.as_str()) {
            return Err(format!("unknown campaign step `{name}`"));
        }
    }
    Ok(())
}

fn select_steps(
    steps: &[CampaignStep],
    options: &CampaignOptions,
    cache_root: &Path,
) -> Result<BTreeSet<String>, String> {
    let mut selected = BTreeSet::new();
    if options.rerun_failed {
        for step in steps {
            if read_cache(cache_root, &step.name)?
                .is_some_and(|state| state.status == CachedStatus::Failed)
            {
                selected.insert(step.name.clone());
            }
        }
        if selected.is_empty() {
            return Err("--rerun-failed found no cached failed steps".to_owned());
        }
        add_descendants(steps, &mut selected);
    } else if !options.only.is_empty() {
        selected.extend(options.only.iter().cloned());
    } else if let Some(from) = &options.from {
        let index = steps
            .iter()
            .position(|step| &step.name == from)
            .expect("validated step");
        selected.extend(steps[index..].iter().map(|step| step.name.clone()));
    } else {
        selected.extend(steps.iter().map(|step| step.name.clone()));
    }
    add_dependencies(steps, &mut selected);
    Ok(selected)
}

fn add_dependencies(steps: &[CampaignStep], selected: &mut BTreeSet<String>) {
    loop {
        let before = selected.len();
        for step in steps {
            if selected.contains(&step.name) {
                selected.extend(step.dependencies.iter().cloned());
            }
        }
        if selected.len() == before {
            break;
        }
    }
}

fn add_descendants(steps: &[CampaignStep], selected: &mut BTreeSet<String>) {
    loop {
        let before = selected.len();
        for step in steps {
            if step
                .dependencies
                .iter()
                .any(|dependency| selected.contains(dependency))
            {
                selected.insert(step.name.clone());
            }
        }
        if selected.len() == before {
            break;
        }
    }
}

#[derive(Debug)]
struct ExecutionOutcome {
    passed: bool,
    status: String,
}

fn execute_captured(
    step: &CampaignStep,
    context: &Context,
    short_tmp: &Path,
    stdout_path: &Path,
    stderr_path: &Path,
) -> Result<ExecutionOutcome, String> {
    let stdout = File::create(stdout_path).map_err(io_error("create step stdout"))?;
    let stderr = File::create(stderr_path).map_err(io_error("create step stderr"))?;
    let mut command = Command::new("timeout");
    command
        .arg("--signal=TERM")
        .arg("--kill-after=5s")
        .arg(format!("{}s", step.timeout_seconds))
        .arg(&step.invocation.program)
        .args(&step.invocation.args)
        .current_dir(&context.workspace_root)
        .env("TMPDIR", short_tmp)
        .env("CARGO_NET_OFFLINE", "true")
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr));
    if step.invocation.remove_hardware_authorization {
        command.env_remove("GW_ALLOW_HARDWARE_TESTS");
    }
    let status = command
        .status()
        .map_err(|error| format!("could not start campaign step {}: {error}", step.name))?;
    let description = if status.code() == Some(124) {
        format!("timed out after {}s", step.timeout_seconds)
    } else {
        status.to_string()
    };
    Ok(ExecutionOutcome {
        passed: status.success(),
        status: description,
    })
}

fn fingerprint_step(
    step: &CampaignStep,
    context: &Context,
    upstream: &BTreeMap<String, String>,
) -> Result<(String, String), String> {
    let mut lines = Vec::<String>::new();
    lines.push(format!(
        "command\t{}",
        relative_command(&step.invocation, &context.workspace_root)
    ));
    if let Some(tool) = resolve_program(&step.invocation.program, &context.workspace_root)
        && tool.is_file()
    {
        let digest =
            sha256_file(&tool).map_err(|error| format!("hash tool {}: {error}", tool.display()))?;
        lines.push(format!(
            "tool\t{}\t{digest}",
            step.invocation.program.to_string_lossy()
        ));
    }
    let mut declared = step.source_inputs.clone();
    for argument in &step.invocation.args {
        let value = PathBuf::from(argument);
        let candidate = if value.is_absolute() {
            value
        } else {
            context.workspace_root.join(value)
        };
        if candidate.exists() && candidate.starts_with(&context.workspace_root) {
            declared.push(
                candidate
                    .strip_prefix(&context.workspace_root)
                    .unwrap()
                    .to_path_buf(),
            );
        }
    }
    declared.sort();
    declared.dedup();
    for path in declared {
        hash_input_path(&context.workspace_root, &path, &mut lines)?;
    }
    for dependency in &step.dependencies {
        let hash = upstream.get(dependency).ok_or_else(|| {
            format!(
                "step {} has no fingerprint for dependency {dependency}",
                step.name
            )
        })?;
        lines.push(format!("upstream\t{dependency}\t{hash}"));
    }
    lines.sort();
    let manifest = lines.join("\n") + "\n";
    Ok((sha256(manifest.as_bytes()).to_hex(), manifest))
}

fn hash_input_path(root: &Path, relative: &Path, lines: &mut Vec<String>) -> Result<(), String> {
    let absolute = root.join(relative);
    let metadata = match fs::symlink_metadata(&absolute) {
        Ok(value) => value,
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            lines.push(format!("missing\t{}", relative.display()));
            return Ok(());
        }
        Err(error) => return Err(format!("inspect input {}: {error}", relative.display())),
    };
    if metadata.file_type().is_symlink() {
        let target = fs::read_link(&absolute)
            .map_err(|error| format!("read input symlink {}: {error}", relative.display()))?;
        lines.push(format!(
            "symlink\t{}\t{}",
            relative.display(),
            target.display()
        ));
    } else if metadata.is_file() {
        let digest = sha256_file(&absolute)
            .map_err(|error| format!("hash input {}: {error}", relative.display()))?;
        lines.push(format!(
            "file\t{}\t{}\t{digest}",
            relative.display(),
            metadata.len()
        ));
    } else if metadata.is_dir() {
        hash_directory(root, relative, lines)?;
    } else {
        lines.push(format!("special\t{}", relative.display()));
    }
    Ok(())
}

fn hash_directory(root: &Path, relative: &Path, lines: &mut Vec<String>) -> Result<(), String> {
    let directory = root.join(relative);
    let mut children = fs::read_dir(&directory)
        .map_err(|error| format!("read input directory {}: {error}", relative.display()))?
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("read input directory entry {}: {error}", relative.display()))?;
    children.sort_by_key(|entry| entry.file_name());
    for child in children {
        let name = child.file_name();
        if ignored_input_name(&name) {
            continue;
        }
        hash_input_path(root, &relative.join(name), lines)?;
    }
    Ok(())
}

fn ignored_input_name(name: &OsStr) -> bool {
    matches!(
        name.to_str(),
        Some(".git" | ".codex" | "Plans" | "target" | "__pycache__" | ".cache")
    ) || name.to_str().is_some_and(|value| value.ends_with(".pyc"))
}

fn resolve_program(program: &OsStr, workspace_root: &Path) -> Option<PathBuf> {
    let path = PathBuf::from(program);
    if path.is_absolute() {
        return Some(path);
    }
    if path.components().count() > 1 {
        return Some(workspace_root.join(path));
    }
    std::env::var_os("PATH").and_then(|paths| {
        std::env::split_paths(&paths)
            .map(|root| root.join(&path))
            .find(|candidate| candidate.is_file())
    })
}

fn cache_path(cache_root: &Path, name: &str) -> PathBuf {
    cache_root.join(format!("{name}.state"))
}

fn read_cache(cache_root: &Path, name: &str) -> Result<Option<CacheState>, String> {
    let path = cache_path(cache_root, name);
    let contents = match fs::read_to_string(&path) {
        Ok(value) => value,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(format!("read campaign cache {}: {error}", path.display())),
    };
    let mut status = None;
    let mut input_hash = None;
    let mut evidence_run = None;
    let mut stdout_sha256 = None;
    let mut stderr_sha256 = None;
    for line in contents.lines() {
        if let Some(value) = line.strip_prefix("status=") {
            status = match value {
                "passed" => Some(CachedStatus::Passed),
                "failed" => Some(CachedStatus::Failed),
                _ => None,
            };
        } else if let Some(value) = line.strip_prefix("input_hash=") {
            input_hash = Some(value.to_owned());
        } else if let Some(value) = line.strip_prefix("evidence_run=") {
            evidence_run = (!value.is_empty()).then(|| value.to_owned());
        } else if let Some(value) = line.strip_prefix("stdout_sha256=") {
            stdout_sha256 = (!value.is_empty()).then(|| value.to_owned());
        } else if let Some(value) = line.strip_prefix("stderr_sha256=") {
            stderr_sha256 = (!value.is_empty()).then(|| value.to_owned());
        }
    }
    match (status, input_hash) {
        (Some(status), Some(input_hash)) => Ok(Some(CacheState {
            status,
            input_hash,
            evidence_run,
            stdout_sha256,
            stderr_sha256,
        })),
        _ => Ok(None),
    }
}

fn write_cache(
    cache_root: &Path,
    name: &str,
    status: CachedStatus,
    input_hash: &str,
    evidence_run: &str,
    stdout_sha256: &str,
    stderr_sha256: &str,
) -> Result<(), String> {
    fs::create_dir_all(cache_root).map_err(io_error("create campaign cache"))?;
    let path = cache_path(cache_root, name);
    let temporary = cache_root.join(format!(".{name}.{}.tmp", std::process::id()));
    let status = match status {
        CachedStatus::Passed => "passed",
        CachedStatus::Failed => "failed",
    };
    fs::write(
        &temporary,
        format!(
            "version=2\nstatus={status}\ninput_hash={input_hash}\nevidence_run={evidence_run}\nstdout_sha256={stdout_sha256}\nstderr_sha256={stderr_sha256}\n"
        ),
    )
    .map_err(io_error("write campaign cache"))?;
    fs::rename(&temporary, &path).map_err(io_error("publish campaign cache"))
}

struct FailureEvidence<'a> {
    input_hash: &'a str,
    input_manifest: &'a str,
    status: &'a str,
    stdout_path: &'a Path,
    stderr_path: &'a Path,
    stdout_sha256: &'a str,
    stderr_sha256: &'a str,
}

fn write_failure_bundle(
    run_root: &Path,
    step: &CampaignStep,
    context: &Context,
    evidence: &FailureEvidence<'_>,
) -> Result<(), String> {
    let root = run_root.join("failures").join(&step.name);
    fs::create_dir_all(&root).map_err(io_error("create failure bundle"))?;
    fs::write(
        root.join("command-lines.txt"),
        format!(
            "{}\n",
            relative_command(&step.invocation, &context.workspace_root)
        ),
    )
    .map_err(io_error("write failure command"))?;
    fs::write(root.join("input-manifest.txt"), evidence.input_manifest)
        .map_err(io_error("write failure inputs"))?;
    fs::copy(evidence.stdout_path, root.join("stdout.log"))
        .map_err(io_error("copy failure stdout"))?;
    fs::copy(evidence.stderr_path, root.join("stderr.log"))
        .map_err(io_error("copy failure stderr"))?;
    let json = format!(
        "{{\n  \"schema\": \"glasswyrm-rust-transition-failure-v2\",\n  \"step\": {},\n  \"input_hash\": {},\n  \"status\": {},\n  \"timeout_seconds\": {},\n  \"command\": {},\n  \"outputs\": {{\n    \"stdout_sha256\": {},\n    \"stderr_sha256\": {}\n  }}\n}}\n",
        json_string(&step.name),
        json_string(evidence.input_hash),
        json_string(evidence.status),
        step.timeout_seconds,
        json_string(&relative_command(&step.invocation, &context.workspace_root)),
        json_string(evidence.stdout_sha256),
        json_string(evidence.stderr_sha256),
    );
    fs::write(root.join("manifest.json"), json).map_err(io_error("write failure manifest"))
}

struct RunSummary<'a> {
    run_id: &'a str,
    revision: &'a str,
    dirty: bool,
    selected: &'a BTreeSet<String>,
    results: &'a BTreeMap<String, StepResult>,
}

fn write_run_manifest(
    run_root: &Path,
    steps: &[CampaignStep],
    context: &Context,
    summary: &RunSummary<'_>,
) -> Result<PathBuf, String> {
    let mut json = String::new();
    writeln!(json, "{{").unwrap();
    writeln!(
        json,
        "  \"schema\": \"glasswyrm-rust-transition-development-v2\","
    )
    .unwrap();
    writeln!(json, "  \"run_id\": {},", json_string(summary.run_id)).unwrap();
    writeln!(
        json,
        "  \"revision\": {},",
        json_string(summary.revision.trim())
    )
    .unwrap();
    writeln!(
        json,
        "  \"dirty\": {},",
        if summary.dirty { "true" } else { "false" }
    )
    .unwrap();
    writeln!(json, "  \"steps\": [").unwrap();
    let selected_steps = steps
        .iter()
        .filter(|step| summary.selected.contains(&step.name))
        .collect::<Vec<_>>();
    for (index, step) in selected_steps.iter().enumerate() {
        let result = summary.results.get(&step.name);
        let comma = if index + 1 == selected_steps.len() {
            ""
        } else {
            ","
        };
        let (status, input_hash, detail) =
            result.map_or(("not-run", "", "not evaluated"), |value| {
                (
                    value.status.as_str(),
                    value.input_hash.as_str(),
                    value.detail.as_str(),
                )
            });
        let evidence_run = result.and_then(|value| value.evidence_run.as_deref());
        let stdout_sha256 = result.and_then(|value| value.stdout_sha256.as_deref());
        let stderr_sha256 = result.and_then(|value| value.stderr_sha256.as_deref());
        let stdout_path = evidence_run.map(|run| {
            format!(
                "artifacts/rust-transition/development/runs/{run}/steps/{}.stdout",
                step.name
            )
        });
        let stderr_path = evidence_run.map(|run| {
            format!(
                "artifacts/rust-transition/development/runs/{run}/steps/{}.stderr",
                step.name
            )
        });
        let mut declared_outputs = Vec::new();
        for path in required_output_paths(step) {
            let absolute = context.workspace_root.join(path);
            let hash = if absolute.is_file() {
                Some(sha256_hex(&absolute)?)
            } else {
                None
            };
            declared_outputs.push(format!(
                "{{\"path\": {}, \"sha256\": {}}}",
                json_string(path),
                json_optional(hash.as_deref())
            ));
        }
        writeln!(json, "    {{\"name\": {}, \"status\": {}, \"input_hash\": {}, \"detail\": {}, \"command\": {}, \"dependencies\": [{}], \"evidence_run\": {}, \"outputs\": {{\"stdout\": {{\"path\": {}, \"sha256\": {}}}, \"stderr\": {{\"path\": {}, \"sha256\": {}}}, \"declared\": [{}]}}}}{comma}",
            json_string(&step.name), json_string(status), json_string(input_hash), json_string(detail),
            json_string(&relative_command(&step.invocation, &context.workspace_root)),
            step.dependencies.iter().map(|value| json_string(value)).collect::<Vec<_>>().join(", "),
            json_optional(evidence_run), json_optional(stdout_path.as_deref()), json_optional(stdout_sha256),
            json_optional(stderr_path.as_deref()), json_optional(stderr_sha256), declared_outputs.join(", ")).unwrap();
    }
    writeln!(json, "  ]\n}}").unwrap();
    let path = run_root.join("manifest.json");
    fs::write(&path, json).map_err(io_error("write campaign manifest"))?;
    Ok(path)
}

fn relative_command(invocation: &Invocation, root: &Path) -> String {
    std::iter::once(&invocation.program)
        .chain(invocation.args.iter())
        .map(|value| {
            let path = PathBuf::from(value);
            if path.is_absolute() && path.starts_with(root) {
                path.strip_prefix(root).unwrap().display().to_string()
            } else {
                value.to_string_lossy().into_owned()
            }
        })
        .map(|value| shellish(&value))
        .collect::<Vec<_>>()
        .join(" ")
}

fn shellish(value: &str) -> String {
    if !value.is_empty()
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._/-:=,".contains(&byte))
    {
        value.to_owned()
    } else {
        format!("{:?}", value)
    }
}

fn relative_to(path: &Path, root: &Path) -> String {
    path.strip_prefix(root)
        .unwrap_or(path)
        .display()
        .to_string()
}

fn json_optional(value: Option<&str>) -> String {
    value.map_or_else(|| "null".to_owned(), json_string)
}

fn json_string(value: &str) -> String {
    let mut output = String::from("\"");
    for character in value.chars() {
        match character {
            '\\' => output.push_str("\\\\"),
            '"' => output.push_str("\\\""),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            value if value.is_control() => {
                use std::fmt::Write as _;
                write!(output, "\\u{:04x}", value as u32).unwrap();
            }
            value => output.push(value),
        }
    }
    output.push('"');
    output
}

fn run_id() -> String {
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    format!(
        "{}-{:09}-{}",
        duration.as_secs(),
        duration.subsec_nanos(),
        std::process::id()
    )
}

fn git_output(root: &Path, args: &[&str]) -> Result<String, String> {
    let output = Command::new("git")
        .args(args)
        .current_dir(root)
        .output()
        .map_err(|error| format!("run git: {error}"))?;
    if !output.status.success() {
        return Err(format!("git {} failed", args.join(" ")));
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_owned())
}

fn io_error(context: &'static str) -> impl FnOnce(io::Error) -> String {
    move |error| format!("{context}: {error}")
}

pub fn run_replay(context: &Context, options: &ReplayOptions) -> Result<(), String> {
    let replay_root = context
        .workspace_root
        .join("artifacts/rust-transition/development/replay")
        .join(run_id());
    fs::create_dir_all(&replay_root).map_err(io_error("create replay output"))?;
    let short_tmp = context
        .workspace_root
        .parent()
        .unwrap_or(&context.workspace_root)
        .join(".t");
    fs::create_dir_all(&short_tmp).map_err(io_error("create replay temp root"))?;
    let (args, output_path) = match options {
        ReplayOptions::M14Probe { report, config } => {
            require_workspace_input(report, context)?;
            require_workspace_input(config, context)?;
            let output = replay_root.join("m14-probe-summary.json");
            (
                vec![
                    OsString::from("analyze-milestone14-nvidia-vrr-probe"),
                    OsString::from("--report"),
                    report.as_os_str().to_owned(),
                    OsString::from("--config"),
                    config.as_os_str().to_owned(),
                    OsString::from("--output"),
                    output.as_os_str().to_owned(),
                ],
                output,
            )
        }
        ReplayOptions::M14Fixture {
            fixture_dir,
            config,
            required_base,
            tested_commit,
        } => {
            require_workspace_input(fixture_dir, context)?;
            require_workspace_input(config, context)?;
            let output = replay_root.join("m14-fixture");
            (
                vec![
                    OsString::from("milestone14-vrr-test"),
                    OsString::from("--config"),
                    config.as_os_str().to_owned(),
                    OsString::from("--required-base"),
                    OsString::from(required_base),
                    OsString::from("--tested-commit"),
                    OsString::from(tested_commit),
                    OsString::from("--yes"),
                    OsString::from("--dry-run"),
                    OsString::from("--fixture-dir"),
                    fixture_dir.as_os_str().to_owned(),
                    OsString::from("--artifact-dir"),
                    output.as_os_str().to_owned(),
                ],
                output,
            )
        }
    };
    let stdout = replay_root.join("stdout.log");
    let stderr = replay_root.join("stderr.log");
    let step = CampaignStep {
        name: "artifact-replay".to_owned(),
        dependencies: Vec::new(),
        invocation: Invocation::new(
            context.workspace_root.join("tools/gw-hw").into_os_string(),
            args,
        ),
        source_inputs: Vec::new(),
        timeout_seconds: 120,
    };
    let outcome = execute_captured(&step, context, &short_tmp, &stdout, &stderr)?;
    let output_sha256 = if output_path.is_file() {
        Some(sha256_hex(&output_path)?)
    } else {
        None
    };
    let stdout_sha256 = sha256_hex(&stdout)?;
    let stderr_sha256 = sha256_hex(&stderr)?;
    let manifest = format!(
        "{{\n  \"schema\": \"glasswyrm-artifact-replay-v2\",\n  \"passed\": {},\n  \"status\": {},\n  \"output\": {{\n    \"path\": {},\n    \"sha256\": {}\n  }},\n  \"stdout_sha256\": {},\n  \"stderr_sha256\": {},\n  \"command\": {}\n}}\n",
        if outcome.passed { "true" } else { "false" },
        json_string(&outcome.status),
        json_string(&relative_to(&output_path, &context.workspace_root)),
        json_optional(output_sha256.as_deref()),
        json_string(&stdout_sha256),
        json_string(&stderr_sha256),
        json_string(&relative_command(&step.invocation, &context.workspace_root))
    );
    fs::write(replay_root.join("manifest.json"), manifest)
        .map_err(io_error("write replay manifest"))?;
    if outcome.passed {
        eprintln!(
            "xtask: artifact replay passed: {}",
            relative_to(&output_path, &context.workspace_root)
        );
        Ok(())
    } else {
        Err(format!(
            "artifact replay failed; see {}",
            relative_to(&replay_root, &context.workspace_root)
        ))
    }
}

fn require_workspace_input(path: &Path, context: &Context) -> Result<(), String> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        context.workspace_root.join(path)
    };
    let canonical = absolute
        .canonicalize()
        .map_err(|error| format!("replay input {}: {error}", path.display()))?;
    let root = context
        .workspace_root
        .canonicalize()
        .map_err(|error| format!("canonicalize workspace root: {error}"))?;
    if !canonical.starts_with(&root) {
        return Err(format!(
            "replay input must remain inside the Glasswyrm workspace: {}",
            path.display()
        ));
    }
    Ok(())
}

#[cfg(test)]
mod campaign_tests {
    use super::*;

    fn context() -> Context {
        Context {
            workspace_root: PathBuf::from("/workspace"),
            legacy_build_env: None,
            rust_bin_dir_env: None,
            hardware_allowed: false,
        }
    }

    fn cli() -> Cli {
        Cli {
            legacy_build: None,
            rust_bin_dir: None,
            task: Task::Campaign(CampaignOptions::default()),
        }
    }

    #[test]
    fn campaign_parser_accepts_resume_selectors_and_rejects_list_execution_mix() {
        let options = parse_campaign(
            &mut ["--only", "rust-fmt,gwm-build", "--resume"]
                .into_iter()
                .map(str::to_owned),
        )
        .unwrap();
        assert_eq!(options.only, ["rust-fmt", "gwm-build"]);
        assert!(options.resume);
        assert!(
            parse_campaign(&mut ["--list", "--resume"].into_iter().map(str::to_owned)).is_err()
        );
    }

    #[test]
    fn cached_evidence_requires_untampered_stdout_and_stderr() {
        let root =
            std::env::temp_dir().join(format!("gw-xtask-cache-evidence-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let steps = root.join("runs/run-1/steps");
        fs::create_dir_all(&steps).unwrap();
        let stdout = steps.join("probe.stdout");
        let stderr = steps.join("probe.stderr");
        fs::write(&stdout, b"passed\n").unwrap();
        fs::write(&stderr, b"").unwrap();
        let state = CacheState {
            status: CachedStatus::Passed,
            input_hash: "input".to_owned(),
            evidence_run: Some("run-1".to_owned()),
            stdout_sha256: Some(sha256_hex(&stdout).unwrap()),
            stderr_sha256: Some(sha256_hex(&stderr).unwrap()),
        };
        let step = CampaignStep {
            name: "probe".to_owned(),
            dependencies: Vec::new(),
            invocation: Invocation::new("true", std::iter::empty::<&str>()),
            source_inputs: Vec::new(),
            timeout_seconds: 1,
        };
        assert!(cached_evidence_valid(&root, &step, &state));
        fs::write(&stdout, b"tampered\n").unwrap();
        assert!(!cached_evidence_valid(&root, &step, &state));
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn campaign_graph_has_unique_inventory_and_bounded_dependency_edges() {
        let steps = campaign_steps(&cli(), &context()).unwrap();
        assert_eq!(steps.len(), 21);
        let names = steps
            .iter()
            .map(|step| step.name.as_str())
            .collect::<BTreeSet<_>>();
        assert_eq!(names.len(), steps.len());
        assert!(names.contains("transition-inventory-check"));
        let gwm = steps
            .iter()
            .find(|step| step.name == "gwm-process")
            .unwrap();
        assert_eq!(gwm.dependencies, ["gwm-build"]);
        let inventory = steps
            .iter()
            .find(|step| step.name == "transition-inventory-check")
            .unwrap();
        assert!(inventory.dependencies.is_empty());
        assert!(inventory.invocation.remove_hardware_authorization);
    }
}
