use std::ffi::OsString;
use std::path::PathBuf;

const USAGE: &str = "Usage: glasswyrmd [--display N] [--socket-dir PATH] [--help] [--version]";

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Options {
    pub display: u16,
    pub socket_dir: PathBuf,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            display: 0,
            socket_dir: PathBuf::from("/tmp/.X11-unix"),
        }
    }
}

impl Options {
    pub fn socket_path(&self) -> PathBuf {
        self.socket_dir.join(format!("X{}", self.display))
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ParseResult {
    Run(Options),
    ExitSuccess(String),
    ExitFailure(String),
}

pub fn parse(arguments: impl IntoIterator<Item = OsString>) -> ParseResult {
    let mut options = Options::default();
    let mut arguments = arguments.into_iter();
    let _program = arguments.next();
    while let Some(argument) = arguments.next() {
        let Some(argument) = argument.to_str() else {
            return failure("glasswyrmd: option is not valid UTF-8");
        };
        match argument {
            "--help" => return ParseResult::ExitSuccess(format!("{USAGE}\n")),
            "--version" => {
                return ParseResult::ExitSuccess(format!(
                    "glasswyrmd {}\n",
                    env!("CARGO_PKG_VERSION")
                ));
            }
            "--display" => {
                let Some(value) = arguments.next() else {
                    return failure("glasswyrmd: --display requires an integer from 0 to 65535");
                };
                let Some(value) = value.to_str() else {
                    return failure("glasswyrmd: --display requires an integer from 0 to 65535");
                };
                let Ok(display) = value.parse::<u16>() else {
                    return failure("glasswyrmd: --display requires an integer from 0 to 65535");
                };
                options.display = display;
            }
            "--socket-dir" => {
                let Some(value) = arguments.next() else {
                    return failure("glasswyrmd: --socket-dir requires a non-empty path");
                };
                if value.is_empty() {
                    return failure("glasswyrmd: --socket-dir requires a non-empty path");
                }
                options.socket_dir = PathBuf::from(value);
            }
            unknown => {
                return ParseResult::ExitFailure(format!(
                    "glasswyrmd: unknown option: {unknown}\n{USAGE}\n"
                ));
            }
        }
    }
    ParseResult::Run(options)
}

fn failure(message: &str) -> ParseResult {
    ParseResult::ExitFailure(format!("{message}\n"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(values: &[&str]) -> Vec<OsString> {
        values.iter().map(OsString::from).collect()
    }

    #[test]
    fn parses_the_setup_daemon_options() {
        assert_eq!(
            parse(args(&[
                "glasswyrmd",
                "--display",
                "99",
                "--socket-dir",
                "/tmp/gw"
            ])),
            ParseResult::Run(Options {
                display: 99,
                socket_dir: PathBuf::from("/tmp/gw"),
            })
        );
        assert!(matches!(
            parse(args(&["glasswyrmd", "--display", "65536"])),
            ParseResult::ExitFailure(_)
        ));
        assert!(matches!(
            parse(args(&["glasswyrmd", "--socket-dir", ""])),
            ParseResult::ExitFailure(_)
        ));
    }

    #[test]
    fn help_version_and_unknown_options_exit_without_running() {
        assert!(matches!(
            parse(args(&["glasswyrmd", "--help"])),
            ParseResult::ExitSuccess(_)
        ));
        assert!(matches!(
            parse(args(&["glasswyrmd", "--version"])),
            ParseResult::ExitSuccess(_)
        ));
        assert!(matches!(
            parse(args(&["glasswyrmd", "--request-dispatch"])),
            ParseResult::ExitFailure(_)
        ));
    }
}
