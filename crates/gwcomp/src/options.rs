use std::collections::BTreeSet;
use std::path::PathBuf;

pub const USAGE: &str = "Usage: gwcomp [--backend headless] --ipc-socket PATH\n  headless: [--dump-dir PATH] [--headless-output NAME[:WIDTHxHEIGHT[@MILLIHZ]]]...\n            [--headless-vrr NAME=MIN-MILLIHZ-MAX-MILLIHZ]...\n  renderer: [--renderer software|auto]\n  evidence: [--scene-manifest PATH] [--vrr-report PATH]\n  common: [--once] [--max-frames N] [--help] [--version]\n";

const MAXIMUM_OUTPUTS: usize = 8;
const MAXIMUM_EXTENT: u32 = 4096;
const MAXIMUM_PIXELS: u64 = 16_777_216;
const MAXIMUM_TOTAL_PIXELS: u64 = 67_108_864;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HeadlessOutput {
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub refresh_millihertz: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HeadlessVrr {
    pub name: String,
    pub minimum_refresh_millihertz: u32,
    pub maximum_refresh_millihertz: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Options {
    pub ipc_socket: PathBuf,
    pub dump_dir: Option<PathBuf>,
    pub outputs: Vec<HeadlessOutput>,
    pub vrr: Vec<HeadlessVrr>,
    pub scene_manifest: Option<PathBuf>,
    pub vrr_report: Option<PathBuf>,
    pub once: bool,
    pub max_frames: Option<u64>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ParseOutcome {
    Run(Options),
    ExitSuccess(String),
}

/// Parses the bounded Rust transition compositor command line.
///
/// # Errors
///
/// Returns a diagnostic when an option is unknown, lacks a value, requests an
/// unavailable hardware path, or describes an invalid headless topology.
#[allow(clippy::too_many_lines)]
pub fn parse_options(arguments: impl IntoIterator<Item = String>) -> Result<ParseOutcome, String> {
    let mut arguments = arguments.into_iter();
    let _program = arguments.next();
    let mut socket = None;
    let mut dump = None;
    let mut outputs = Vec::new();
    let mut vrr = Vec::new();
    let mut scene_manifest = None;
    let mut vrr_report = None;
    let mut once = false;
    let mut max_frames = None;
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--help" => return Ok(ParseOutcome::ExitSuccess(USAGE.to_owned())),
            "--version" => {
                return Ok(ParseOutcome::ExitSuccess(format!(
                    "gwcomp {}\n",
                    env!("CARGO_PKG_VERSION")
                )));
            }
            "--backend" => {
                let value = take_value(&mut arguments, "--backend")?;
                if value != "headless" {
                    return Err("--backend requires headless; Rust DRM is not implemented".into());
                }
            }
            "--ipc-socket" => {
                socket = Some(PathBuf::from(take_value(&mut arguments, "--ipc-socket")?));
            }
            "--dump-dir" => dump = Some(PathBuf::from(take_value(&mut arguments, "--dump-dir")?)),
            "--headless-output" => {
                if outputs.len() == MAXIMUM_OUTPUTS {
                    return Err("--headless-output may be specified at most 8 times".into());
                }
                outputs.push(parse_output(&take_value(
                    &mut arguments,
                    "--headless-output",
                )?)?);
            }
            "--headless-vrr" => {
                vrr.push(parse_vrr(&take_value(&mut arguments, "--headless-vrr")?)?);
            }
            "--renderer" => {
                let value = take_value(&mut arguments, "--renderer")?;
                if value != "software" && value != "auto" {
                    return Err(
                        "--renderer requires software or auto; GLES is not implemented".into(),
                    );
                }
            }
            "--scene-manifest" => {
                scene_manifest = Some(PathBuf::from(take_value(
                    &mut arguments,
                    "--scene-manifest",
                )?));
            }
            "--vrr-report" => {
                vrr_report = Some(PathBuf::from(take_value(&mut arguments, "--vrr-report")?));
            }
            "--once" => once = true,
            "--max-frames" => {
                let value = take_value(&mut arguments, "--max-frames")?;
                max_frames = Some(
                    value
                        .parse::<u64>()
                        .ok()
                        .filter(|value| *value != 0)
                        .ok_or_else(|| "--max-frames requires a positive integer".to_owned())?,
                );
            }
            value
                if value.starts_with("--drm")
                    || matches!(
                        value,
                        "--tty"
                            | "--external-session"
                            | "--connector"
                            | "--mode"
                            | "--mirror-dump-dir"
                            | "--mirror-dump-trigger"
                    ) =>
            {
                return Err(
                    "DRM options are unavailable in the Rust headless transition process".into(),
                );
            }
            _ => return Err(format!("unknown option: {argument}")),
        }
    }
    let ipc_socket = socket.ok_or_else(|| "--ipc-socket is required".to_owned())?;
    if outputs.is_empty() {
        outputs.push(HeadlessOutput {
            name: "HEADLESS-1".into(),
            width: 1024,
            height: 768,
            refresh_millihertz: 60_000,
        });
    }
    let output_names: BTreeSet<_> = outputs.iter().map(|output| output.name.as_str()).collect();
    if output_names.len() != outputs.len() {
        return Err("--headless-output names must be unique".into());
    }
    let total_pixels = outputs.iter().try_fold(0_u64, |total, output| {
        total.checked_add(u64::from(output.width) * u64::from(output.height))
    });
    if total_pixels.is_none_or(|pixels| pixels > MAXIMUM_TOTAL_PIXELS) {
        return Err("headless topology exceeds 67108864 total physical pixels".into());
    }
    let vrr_names: BTreeSet<_> = vrr.iter().map(|request| request.name.as_str()).collect();
    if vrr_names.len() != vrr.len() {
        return Err("--headless-vrr names must be unique".into());
    }
    for request in &vrr {
        let output = outputs
            .iter()
            .find(|output| output.name == request.name)
            .ok_or_else(|| "--headless-vrr names an unknown headless output".to_owned())?;
        if request.maximum_refresh_millihertz > output.refresh_millihertz {
            return Err("--headless-vrr maximum must not exceed nominal output refresh".into());
        }
    }
    Ok(ParseOutcome::Run(Options {
        ipc_socket,
        dump_dir: dump,
        outputs,
        vrr,
        scene_manifest,
        vrr_report,
        once,
        max_frames,
    }))
}

fn take_value(
    arguments: &mut impl Iterator<Item = String>,
    option: &str,
) -> Result<String, String> {
    arguments
        .next()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| format!("{option} requires a non-empty value"))
}

fn valid_name(name: &str) -> bool {
    (1..=63).contains(&name.len())
        && name
            .as_bytes()
            .first()
            .is_some_and(u8::is_ascii_alphanumeric)
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
}

fn parse_output(value: &str) -> Result<HeadlessOutput, String> {
    let (name, mode) = value
        .split_once(':')
        .map_or((value, None), |(name, mode)| (name, Some(mode)));
    if !valid_name(name) {
        return Err("--headless-output requires a 1-63 byte ASCII identifier".into());
    }
    let (width, height, refresh) = if let Some(mode) = mode {
        let (extent, refresh) = mode
            .split_once('@')
            .map_or((mode, 60_000), |(extent, refresh)| {
                (extent, refresh.parse().unwrap_or(0))
            });
        let (width, height) = extent
            .split_once('x')
            .ok_or_else(|| "--headless-output requires NAME[:WIDTHxHEIGHT[@MILLIHZ]]".to_owned())?;
        (
            width.parse().unwrap_or(0),
            height.parse().unwrap_or(0),
            refresh,
        )
    } else {
        (1024, 768, 60_000)
    };
    let pixels = u64::from(width) * u64::from(height);
    if width == 0
        || height == 0
        || refresh == 0
        || width > MAXIMUM_EXTENT
        || height > MAXIMUM_EXTENT
        || pixels > MAXIMUM_PIXELS
    {
        return Err(
            "--headless-output dimensions must be within 1..4096 and at most 16777216 pixels"
                .into(),
        );
    }
    Ok(HeadlessOutput {
        name: name.into(),
        width,
        height,
        refresh_millihertz: refresh,
    })
}

fn parse_vrr(value: &str) -> Result<HeadlessVrr, String> {
    let (name, range) = value
        .split_once('=')
        .ok_or_else(|| "--headless-vrr requires NAME=MIN-MILLIHZ-MAX-MILLIHZ".to_owned())?;
    let (minimum, maximum) = range
        .split_once('-')
        .ok_or_else(|| "--headless-vrr requires NAME=MIN-MILLIHZ-MAX-MILLIHZ".to_owned())?;
    let minimum = minimum.parse().unwrap_or(0);
    let maximum = maximum.parse().unwrap_or(0);
    if !valid_name(name) || minimum == 0 || minimum >= maximum || range.matches('-').count() != 1 {
        return Err(
            "--headless-vrr requires NAME=MIN-MILLIHZ-MAX-MILLIHZ with 0 < MIN < MAX".into(),
        );
    }
    Ok(HeadlessVrr {
        name: name.into(),
        minimum_refresh_millihertz: minimum,
        maximum_refresh_millihertz: maximum,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_legacy_headless_shape() {
        let result = parse_options(
            [
                "gwcomp",
                "--ipc-socket",
                "/tmp/gw.sock",
                "--dump-dir",
                "/tmp/dumps",
                "--headless-output",
                "LEFT:640x480@60000",
                "--headless-vrr",
                "LEFT=40000-60000",
            ]
            .map(str::to_owned),
        )
        .unwrap();
        let ParseOutcome::Run(options) = result else {
            panic!("expected run")
        };
        assert_eq!(options.outputs[0].width, 640);
        assert_eq!(options.vrr[0].minimum_refresh_millihertz, 40_000);
    }

    #[test]
    fn rejects_hardware_and_bad_vrr() {
        assert!(parse_options(["gwcomp", "--backend", "drm"].map(str::to_owned)).is_err());
        assert!(
            parse_options(
                [
                    "gwcomp",
                    "--ipc-socket",
                    "s",
                    "--dump-dir",
                    "d",
                    "--headless-vrr",
                    "missing=40-60"
                ]
                .map(str::to_owned)
            )
            .is_err()
        );
    }

    #[test]
    fn parses_evidence_paths_and_rejects_excessive_topology() {
        let ParseOutcome::Run(options) = parse_options(
            [
                "gwcomp",
                "--ipc-socket",
                "s",
                "--dump-dir",
                "d",
                "--scene-manifest",
                "scene.jsonl",
                "--vrr-report",
                "vrr.jsonl",
            ]
            .map(str::to_owned),
        )
        .unwrap() else {
            panic!("expected run")
        };
        assert_eq!(options.scene_manifest, Some(PathBuf::from("scene.jsonl")));
        assert_eq!(options.vrr_report, Some(PathBuf::from("vrr.jsonl")));

        let mut arguments = vec![
            "gwcomp".to_owned(),
            "--ipc-socket".to_owned(),
            "s".to_owned(),
            "--dump-dir".to_owned(),
            "d".to_owned(),
        ];
        for index in 0..5 {
            arguments.extend([
                "--headless-output".to_owned(),
                format!("H{index}:4096x4096"),
            ]);
        }
        assert!(parse_options(arguments).is_err());
    }

    #[test]
    fn dump_output_is_explicit_and_frame_stop_remains_optional() {
        let ParseOutcome::Run(options) =
            parse_options(["gwcomp", "--ipc-socket", "s"].map(str::to_owned)).unwrap()
        else {
            panic!("expected run")
        };
        assert_eq!(options.dump_dir, None);
        assert_eq!(options.max_frames, None);

        let ParseOutcome::Run(options) = parse_options(
            [
                "gwcomp",
                "--ipc-socket",
                "s",
                "--max-frames",
                "18446744073709551615",
            ]
            .map(str::to_owned),
        )
        .unwrap() else {
            panic!("expected run")
        };
        assert_eq!(options.max_frames, Some(u64::MAX));
    }
}
