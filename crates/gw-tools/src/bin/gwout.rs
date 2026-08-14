use std::ffi::OsString;
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::ExitCode;

use gw_tools::OutputEdit;
use gw_wire::OutputConfigurationResult;

const USAGE: &str = "Usage:\n  gwout --socket PATH list [--json]\n  gwout --socket PATH set OUTPUT [OPTIONS] [--json]\nOptions:\n  --enable | --disable\n  --mode WIDTHxHEIGHT[@MILLIHZ]\n  --position X,Y\n  --scale NUM/DEN\n  --transform NAME\n  --vrr off|fullscreen|focused|app-requested|always-eligible\n  --primary\n  --help | --version\n";

#[derive(Clone, Debug, Eq, PartialEq)]
enum Command {
    Help,
    Version,
    List {
        socket: PathBuf,
        json: bool,
    },
    Set {
        socket: PathBuf,
        selector: String,
        edit: OutputEdit,
        json: bool,
    },
}

fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Command, ()> {
    let arguments: Vec<_> = arguments.into_iter().collect();
    if arguments.as_slice() == [OsString::from("--help")] {
        return Ok(Command::Help);
    }
    if arguments.as_slice() == [OsString::from("--version")] {
        return Ok(Command::Version);
    }

    let mut socket = None;
    let mut command = None;
    let mut selector = None;
    let mut edit = OutputEdit::default();
    let mut json = false;
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].to_string_lossy();
        let take_value = |index: &mut usize| -> Option<String> {
            *index += 1;
            arguments
                .get(*index)
                .map(|value| value.to_string_lossy().into_owned())
        };
        match argument.as_ref() {
            "--socket" => socket = Some(PathBuf::from(take_value(&mut index).ok_or(())?)),
            "--json" => json = true,
            "list" | "set" if command.is_none() => command = Some(argument.into_owned()),
            value
                if command.as_deref() == Some("set")
                    && selector.is_none()
                    && !value.starts_with("--") =>
            {
                selector = Some(value.to_owned());
            }
            "--enable" if edit.enabled.is_none() => edit.enabled = Some(true),
            "--disable" if edit.enabled.is_none() => edit.enabled = Some(false),
            "--primary" if !edit.primary => edit.primary = true,
            "--position" => {
                edit.position = gw_tools::parse_position(&take_value(&mut index).ok_or(())?);
                if edit.position.is_none() {
                    return Err(());
                }
            }
            "--scale" => {
                edit.scale = gw_tools::parse_scale(&take_value(&mut index).ok_or(())?);
                if edit.scale.is_none() {
                    return Err(());
                }
            }
            "--transform" => {
                edit.transform = gw_tools::parse_transform(&take_value(&mut index).ok_or(())?);
                if edit.transform.is_none() {
                    return Err(());
                }
            }
            "--mode" => {
                let (mode, refresh) =
                    gw_tools::parse_mode(&take_value(&mut index).ok_or(())?).ok_or(())?;
                edit.mode = Some(mode);
                edit.refresh_millihertz = refresh;
            }
            "--vrr" => {
                edit.vrr_policy = gw_tools::parse_vrr_policy(&take_value(&mut index).ok_or(())?);
                if edit.vrr_policy.is_none() {
                    return Err(());
                }
            }
            _ => return Err(()),
        }
        index += 1;
    }
    let socket = socket
        .filter(|path| !path.as_os_str().is_empty())
        .ok_or(())?;
    match command.as_deref() {
        Some("list") if selector.is_none() && !has_edit(&edit) => {
            Ok(Command::List { socket, json })
        }
        Some("set") if selector.is_some() && has_edit(&edit) => Ok(Command::Set {
            socket,
            selector: selector.expect("checked above"),
            edit,
            json,
        }),
        _ => Err(()),
    }
}

fn has_edit(edit: &OutputEdit) -> bool {
    edit.enabled.is_some()
        || edit.mode.is_some()
        || edit.position.is_some()
        || edit.scale.is_some()
        || edit.transform.is_some()
        || edit.vrr_policy.is_some()
        || edit.primary
}

fn run(arguments: impl IntoIterator<Item = OsString>) -> ExitCode {
    match parse(arguments) {
        Ok(Command::Help) => {
            print!("{USAGE}");
            ExitCode::SUCCESS
        }
        Ok(Command::Version) => {
            println!("gwout {}", env!("CARGO_PKG_VERSION"));
            ExitCode::SUCCESS
        }
        Ok(Command::List { socket, json }) => match gw_tools::control_outputs(&socket, false) {
            Ok((_, snapshot)) => {
                print!("{}", gw_tools::format_outputs(&snapshot, json));
                ExitCode::SUCCESS
            }
            Err(error) => fail(error),
        },
        Ok(Command::Set {
            socket,
            selector,
            edit,
            json,
        }) => set(socket, selector, edit, json),
        Err(()) => {
            eprintln!("gwout: invalid or incomplete command line");
            eprint!("{USAGE}");
            ExitCode::from(2)
        }
    }
}

fn set(socket: PathBuf, selector: String, edit: OutputEdit, json: bool) -> ExitCode {
    let mut session_and_snapshot =
        match gw_tools::control_outputs(&socket, edit.vrr_policy.is_some()) {
            Ok(value) => value,
            Err(error) => return fail(error),
        };
    if let Err(error) = gw_tools::apply_output_edit(&mut session_and_snapshot.1, &selector, &edit) {
        return fail(error);
    }
    if let Some(mode) = edit.vrr_policy
        && let Err(error) = gw_tools::apply_vrr_edit(&mut session_and_snapshot.1, &selector, mode)
    {
        return fail(error);
    }
    let acknowledgement = match session_and_snapshot.0.commit(&session_and_snapshot.1) {
        Ok(value) => value,
        Err(error) => return fail(error),
    };
    if acknowledgement.result == OutputConfigurationResult::Accepted {
        if edit.vrr_policy.is_some() {
            let applied = match session_and_snapshot.0.query_vrr() {
                Ok(value) => value,
                Err(error) => {
                    eprintln!(
                        "gwout: accepted configuration but could not query effective VRR state: {error}"
                    );
                    return ExitCode::FAILURE;
                }
            };
            if json {
                let acknowledgement = gw_tools::format_acknowledgement(&acknowledgement, true);
                let state = gw_tools::format_vrr(&applied, &selector, true);
                println!(
                    "{{\"acknowledgement\":{},\"state\":{}}}",
                    acknowledgement.trim_end(),
                    state.trim_end()
                );
            } else {
                print!(
                    "{}{}",
                    gw_tools::format_acknowledgement(&acknowledgement, false),
                    gw_tools::format_vrr(&applied, &selector, false)
                );
            }
        } else {
            print!(
                "{}",
                gw_tools::format_acknowledgement(&acknowledgement, json)
            );
        }
        return ExitCode::SUCCESS;
    }

    print!(
        "{}",
        gw_tools::format_acknowledgement(&acknowledgement, json)
    );
    if acknowledgement.result == OutputConfigurationResult::StaleGeneration {
        eprintln!(
            "gwout: stale layout generation; current generation is {}",
            acknowledgement.applied_generation
        );
    } else {
        eprintln!(
            "gwout: output configuration rejected with result {}",
            acknowledgement.result as u16
        );
    }
    ExitCode::FAILURE
}

fn fail(error: impl std::fmt::Display) -> ExitCode {
    let _ = writeln!(io::stderr(), "gwout: {error}");
    ExitCode::FAILURE
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
    fn parses_the_legacy_list_and_set_shapes() {
        assert_eq!(
            parse(args(&["--socket", "/tmp/control.sock", "list", "--json"])),
            Ok(Command::List {
                socket: PathBuf::from("/tmp/control.sock"),
                json: true,
            })
        );
        assert!(matches!(
            parse(args(&[
                "--socket",
                "/tmp/control.sock",
                "set",
                "RIGHT",
                "--position",
                "640,0",
                "--scale",
                "5/4",
            ])),
            Ok(Command::Set { selector, .. }) if selector == "RIGHT"
        ));
    }

    #[test]
    fn incomplete_and_conflicting_commands_are_usage_errors() {
        assert_eq!(
            parse(args(&["--socket", "/tmp/x", "set", "RIGHT"])),
            Err(())
        );
        assert_eq!(
            parse(args(&["--socket", "/tmp/x", "list", "--scale", "5/4"])),
            Err(())
        );
        assert_eq!(
            parse(args(&[
                "--socket",
                "/tmp/x",
                "set",
                "RIGHT",
                "--enable",
                "--disable",
            ])),
            Err(())
        );
    }
}
