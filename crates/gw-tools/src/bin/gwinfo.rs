use std::ffi::OsString;
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str =
    "Usage:\n  gwinfo --socket PATH outputs [--json]\n  gwinfo --help\n  gwinfo --version\n";

#[derive(Clone, Debug, Eq, PartialEq)]
enum Command {
    Help,
    Version,
    Outputs { socket: PathBuf, json: bool },
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum ArgumentError {
    Invalid(OsString),
    MissingCommand,
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
    let mut command = false;
    let mut json = false;
    let mut index = 0;
    while index < arguments.len() {
        if arguments[index] == "--socket" && index + 1 < arguments.len() {
            index += 1;
            socket = Some(PathBuf::from(&arguments[index]));
        } else if arguments[index] == "--json" {
            json = true;
        } else if arguments[index] == "outputs" && !command {
            command = true;
        } else {
            return Err(ArgumentError::Invalid(arguments[index].clone()));
        }
        index += 1;
    }
    match (socket, command) {
        (Some(socket), true) if !socket.as_os_str().is_empty() => {
            Ok(Command::Outputs { socket, json })
        }
        _ => Err(ArgumentError::MissingCommand),
    }
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
        Ok(Command::Outputs { socket, json }) => match gw_tools::query_outputs(&socket) {
            Ok(snapshot) => {
                print!("{}", gw_tools::format_outputs(&snapshot, json));
                ExitCode::SUCCESS
            }
            Err(error) => {
                eprintln!("gwinfo: {error}");
                ExitCode::FAILURE
            }
        },
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
    fn accepts_only_the_initial_outputs_slice() {
        assert_eq!(
            parse(args(&[
                "--socket",
                "/tmp/control.sock",
                "outputs",
                "--json"
            ])),
            Ok(Command::Outputs {
                socket: PathBuf::from("/tmp/control.sock"),
                json: true,
            })
        );
        assert!(matches!(
            parse(args(&["--socket", "/tmp/control.sock", "windows"])),
            Err(ArgumentError::Invalid(value)) if value == "windows"
        ));
    }

    #[test]
    fn help_and_version_must_stand_alone() {
        assert_eq!(parse(args(&["--help"])), Ok(Command::Help));
        assert_eq!(parse(args(&["--version"])), Ok(Command::Version));
        assert!(matches!(
            parse(args(&["--help", "--json"])),
            Err(ArgumentError::Invalid(value)) if value == "--help"
        ));
    }

    #[test]
    fn missing_socket_or_command_is_usage_error() {
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
