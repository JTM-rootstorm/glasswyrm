//! Headless Glasswyrm compositor process.
//!
//! This crate is intentionally a software-only transition target. It has no
//! DRM, GBM, EGL, GLES, VT, or hardware-session entry points.

mod dump;
mod inventory;
mod manifest;
mod options;
mod runtime;
mod socket;
mod vrr_report;

pub use options::{HeadlessOutput, HeadlessVrr, Options, ParseOutcome, parse_options};

pub fn main_entry(arguments: impl IntoIterator<Item = String>) -> i32 {
    match parse_options(arguments) {
        Ok(ParseOutcome::ExitSuccess(text)) => {
            print!("{text}");
            0
        }
        Ok(ParseOutcome::Run(options)) => match runtime::run(&options) {
            Ok(()) => 0,
            Err(error) => {
                eprintln!("gwcomp: {error}");
                1
            }
        },
        Err(error) => {
            eprintln!("gwcomp: {error}\n{}", options::USAGE);
            2
        }
    }
}
