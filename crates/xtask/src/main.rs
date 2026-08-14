use std::env;
use std::path::PathBuf;
use std::process::ExitCode;

use xtask::{Cli, Context, HELP, Task, execute, plan};

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("xtask: {error}\n\n{HELP}");
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<(), String> {
    let cli = Cli::parse(env::args().skip(1))?;
    if cli.task == Task::Help {
        print!("{HELP}");
        return Ok(());
    }

    let workspace_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|path| path.parent())
        .ok_or_else(|| "could not locate the Cargo workspace root".to_owned())?
        .to_owned();
    let context = Context {
        workspace_root: workspace_root.clone(),
        legacy_build_env: env::var_os("GW_LEGACY_BUILD_DIR").map(PathBuf::from),
        rust_bin_dir_env: env::var_os("GW_RUST_BIN_DIR").map(PathBuf::from),
        hardware_allowed: env::var_os("GW_ALLOW_HARDWARE_TESTS").as_deref()
            == Some(std::ffi::OsStr::new("1")),
    };
    let invocations = plan(&cli, &context)?;
    execute(&invocations, &workspace_root)
}
