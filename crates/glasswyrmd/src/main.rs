use glasswyrmd::options::{ParseResult, parse};
use std::process::ExitCode;

fn main() -> ExitCode {
    match parse(std::env::args_os()) {
        ParseResult::Run(options) => match glasswyrmd::run(options) {
            Ok(()) => ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("glasswyrmd: {error}");
                ExitCode::FAILURE
            }
        },
        ParseResult::ExitSuccess(message) => {
            print!("{message}");
            ExitCode::SUCCESS
        }
        ParseResult::ExitFailure(message) => {
            eprint!("{message}");
            ExitCode::from(2)
        }
    }
}
