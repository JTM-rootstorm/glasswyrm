//! Window-manager policy process and legacy GWIPC compatibility boundary.

mod policy;
mod runtime;
mod socket;

use std::ffi::OsString;
use std::path::PathBuf;

pub use policy::{DispatchError, PeerPolicy};
pub use runtime::run;

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Options {
    pub ipc_socket: PathBuf,
    pub once: bool,
    pub max_commits: Option<u64>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ParseResult {
    Run(Options),
    ExitSuccess(String),
    ExitFailure(String),
}

const USAGE: &str =
    "Usage: gwm --ipc-socket PATH [--once] [--max-commits N] [--help] [--version]\n";

pub fn parse_options<I, S>(arguments: I) -> ParseResult
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    let mut arguments = arguments.into_iter().map(Into::into);
    let _program = arguments.next();
    let mut options = Options::default();
    while let Some(argument) = arguments.next() {
        let Some(argument) = argument.to_str() else {
            return ParseResult::ExitFailure(format!("gwm: option is not valid UTF-8\n{USAGE}"));
        };
        match argument {
            "--help" => return ParseResult::ExitSuccess(USAGE.to_owned()),
            "--version" => {
                return ParseResult::ExitSuccess(format!("gwm {}\n", env!("CARGO_PKG_VERSION")));
            }
            "--once" => options.once = true,
            "--ipc-socket" => {
                let Some(path) = arguments.next() else {
                    return ParseResult::ExitFailure(
                        "gwm: --ipc-socket requires a non-empty path\n".to_owned(),
                    );
                };
                if path.is_empty() {
                    return ParseResult::ExitFailure(
                        "gwm: --ipc-socket requires a non-empty path\n".to_owned(),
                    );
                }
                options.ipc_socket = PathBuf::from(path);
            }
            "--max-commits" => {
                let Some(value) = arguments.next() else {
                    return ParseResult::ExitFailure(
                        "gwm: --max-commits requires a positive integer\n".to_owned(),
                    );
                };
                let value = value.to_str().and_then(|value| value.parse::<u64>().ok());
                match value {
                    Some(value) if value != 0 => options.max_commits = Some(value),
                    _ => {
                        return ParseResult::ExitFailure(
                            "gwm: --max-commits requires a positive integer\n".to_owned(),
                        );
                    }
                }
            }
            unknown => {
                return ParseResult::ExitFailure(format!(
                    "gwm: unknown option: {unknown}\n{USAGE}"
                ));
            }
        }
    }
    if options.ipc_socket.as_os_str().is_empty() {
        return ParseResult::ExitFailure(format!("gwm: --ipc-socket is required\n{USAGE}"));
    }
    ParseResult::Run(options)
}

pub fn main_entry<I, S>(arguments: I) -> i32
where
    I: IntoIterator<Item = S>,
    S: Into<OsString>,
{
    match parse_options(arguments) {
        ParseResult::Run(options) => match run(&options) {
            Ok(()) => 0,
            Err(error) => {
                eprintln!("gwm: {error}");
                1
            }
        },
        ParseResult::ExitSuccess(output) => {
            print!("{output}");
            0
        }
        ParseResult::ExitFailure(error) => {
            eprint!("{error}");
            2
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_legacy_process_options() {
        assert_eq!(
            parse_options(["gwm", "--ipc-socket", "/tmp/gwm.sock", "--max-commits", "2"]),
            ParseResult::Run(Options {
                ipc_socket: "/tmp/gwm.sock".into(),
                once: false,
                max_commits: Some(2),
            })
        );
        assert!(matches!(
            parse_options(["gwm", "--max-commits", "0"]),
            ParseResult::ExitFailure(_)
        ));
        assert!(matches!(
            parse_options(["gwm", "--ipc-socket", "/tmp/gwm.sock", "--once"]),
            ParseResult::Run(Options { once: true, .. })
        ));
    }
}
