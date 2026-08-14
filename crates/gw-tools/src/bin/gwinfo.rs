use std::ffi::OsString;
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::ExitCode;

use gw_tools::DiagnosticQuery;

const USAGE: &str = "Usage:\n  gwinfo --socket PATH outputs [--vrr] [--json]\n  gwinfo --socket PATH windows [--vrr] [--json]\n  gwinfo --socket PATH all [--vrr] [--json]\n  gwinfo --socket PATH vrr [OUTPUT] [--json]\n  gwinfo --help\n  gwinfo --version\n";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Report {
    Outputs,
    Windows,
    All,
    Vrr,
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum Command {
    Help,
    Version,
    Query {
        socket: PathBuf,
        report: Report,
        selector: Option<String>,
        json: bool,
        include_vrr: bool,
    },
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum ArgumentError {
    Invalid(OsString),
    MissingCommand,
    VrrModifier,
}

fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Command, ArgumentError> {
    let arguments: Vec<_> = arguments.into_iter().collect();
    if arguments.as_slice() == [OsString::from("--help")] {
        return Ok(Command::Help);
    }
    if arguments.as_slice() == [OsString::from("--version")] {
        return Ok(Command::Version);
    }

    let mut socket = None;
    let mut report = None;
    let mut selector = None;
    let mut json = false;
    let mut include_vrr = false;
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].to_string_lossy();
        if argument == "--socket" && index + 1 < arguments.len() {
            index += 1;
            socket = Some(PathBuf::from(&arguments[index]));
        } else if argument == "--json" {
            json = true;
        } else if argument == "--vrr" {
            include_vrr = true;
        } else if report.is_none() {
            report = match argument.as_ref() {
                "outputs" => Some(Report::Outputs),
                "windows" => Some(Report::Windows),
                "all" => Some(Report::All),
                "vrr" => Some(Report::Vrr),
                _ => return Err(ArgumentError::Invalid(arguments[index].clone())),
            };
        } else if report == Some(Report::Vrr) && selector.is_none() && !argument.starts_with("--") {
            selector = Some(argument.into_owned());
        } else {
            return Err(ArgumentError::Invalid(arguments[index].clone()));
        }
        index += 1;
    }
    let (Some(socket), Some(report)) = (socket, report) else {
        return Err(ArgumentError::MissingCommand);
    };
    if socket.as_os_str().is_empty() {
        return Err(ArgumentError::MissingCommand);
    }
    if report == Report::Vrr && include_vrr {
        return Err(ArgumentError::VrrModifier);
    }
    Ok(Command::Query {
        socket,
        report,
        selector,
        json,
        include_vrr,
    })
}

fn run(arguments: impl IntoIterator<Item = OsString>) -> ExitCode {
    match parse(arguments) {
        Ok(Command::Help) => {
            print!("{USAGE}");
            ExitCode::SUCCESS
        }
        Ok(Command::Version) => {
            println!("gwinfo {}", env!("CARGO_PKG_VERSION"));
            ExitCode::SUCCESS
        }
        Ok(Command::Query {
            socket,
            report,
            selector,
            json,
            include_vrr,
        }) => {
            let query = match report {
                Report::Outputs => DiagnosticQuery::Outputs { include_vrr },
                Report::Windows => DiagnosticQuery::Windows { include_vrr },
                Report::All => DiagnosticQuery::All { include_vrr },
                Report::Vrr => DiagnosticQuery::Vrr,
            };
            match gw_tools::query_diagnostics(&socket, query) {
                Ok(snapshot) => {
                    let output = match report {
                        Report::Outputs => gw_tools::format_outputs(&snapshot, json),
                        Report::Windows => gw_tools::format_windows(&snapshot, json),
                        Report::All => gw_tools::format_all(&snapshot, json),
                        Report::Vrr => gw_tools::format_vrr(&snapshot, selector.as_deref(), json),
                    };
                    print!("{output}");
                    ExitCode::SUCCESS
                }
                Err(error) => {
                    eprintln!("gwinfo: {error}");
                    ExitCode::FAILURE
                }
            }
        }
        Err(ArgumentError::Invalid(argument)) => {
            let _ = writeln!(
                io::stderr(),
                "gwinfo: invalid argument '{}'",
                argument.to_string_lossy()
            );
            eprint!("{USAGE}");
            ExitCode::from(2)
        }
        Err(ArgumentError::MissingCommand) => {
            eprintln!("gwinfo: --socket PATH and a command are required");
            eprint!("{USAGE}");
            ExitCode::from(2)
        }
        Err(ArgumentError::VrrModifier) => {
            eprintln!("gwinfo: --vrr modifies outputs, windows, or all");
            eprint!("{USAGE}");
            ExitCode::from(2)
        }
    }
}

fn main() -> ExitCode {
    run(std::env::args_os().skip(1))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(values: &[&str]) -> Vec<OsString> {
        values.iter().map(OsString::from).collect()
    }

    #[test]
    fn accepts_each_legacy_report_shape() {
        for report in ["outputs", "windows", "all"] {
            assert!(matches!(
                parse(args(&[
                    "--socket",
                    "/tmp/control.sock",
                    report,
                    "--vrr",
                    "--json"
                ])),
                Ok(Command::Query {
                    include_vrr: true,
                    json: true,
                    ..
                })
            ));
        }
        assert!(matches!(
            parse(args(&[
                "--socket",
                "/tmp/control.sock",
                "vrr",
                "RIGHT",
                "--json"
            ])),
            Ok(Command::Query {
                report: Report::Vrr,
                selector: Some(selector),
                ..
            }) if selector == "RIGHT"
        ));
    }

    #[test]
    fn dedicated_vrr_rejects_the_vrr_modifier() {
        assert_eq!(
            parse(args(&["--socket", "/tmp/control.sock", "vrr", "--vrr"])),
            Err(ArgumentError::VrrModifier)
        );
    }

    #[test]
    fn help_version_and_missing_command_match_legacy() {
        assert_eq!(parse(args(&["--help"])), Ok(Command::Help));
        assert_eq!(parse(args(&["--version"])), Ok(Command::Version));
        assert_eq!(
            parse(args(&["outputs"])),
            Err(ArgumentError::MissingCommand)
        );
        assert_eq!(
            parse(args(&["--socket", "/tmp/control.sock"])),
            Err(ArgumentError::MissingCommand)
        );
    }
}
