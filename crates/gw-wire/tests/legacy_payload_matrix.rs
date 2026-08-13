use std::fs;
use std::path::{Path, PathBuf};

use gw_wire::{compositor, output, vrr, *};

#[derive(Debug)]
struct Fixture {
    name: String,
    kind: String,
    file: String,
    expected_ok: bool,
}

fn fixture_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/gwipc")
}

fn fixtures() -> Vec<Fixture> {
    let manifest = fs::read_to_string(fixture_root().join("manifest.tsv")).unwrap();
    manifest
        .lines()
        .skip(1)
        .map(|line| {
            let fields: Vec<_> = line.split('\t').collect();
            assert_eq!(fields.len(), 8, "malformed fixture row: {line}");
            Fixture {
                name: fields[0].to_owned(),
                kind: fields[2].to_owned(),
                file: fields[4].to_owned(),
                expected_ok: fields[6] == "ok",
            }
        })
        .collect()
}

fn read_hex(file: &str) -> Vec<u8> {
    let text = fs::read_to_string(fixture_root().join(file)).unwrap();
    let hex = text.strip_suffix('\n').unwrap_or(&text).as_bytes();
    assert!(!hex.is_empty() && hex.len().is_multiple_of(2));
    hex.chunks_exact(2)
        .map(|pair| {
            let digit = |byte| match byte {
                b'0'..=b'9' => byte - b'0',
                b'a'..=b'f' => byte - b'a' + 10,
                _ => panic!("{file} is not strict lowercase hexadecimal"),
            };
            digit(pair[0]) << 4 | digit(pair[1])
        })
        .collect()
}

macro_rules! exact {
    ($bytes:expr, $decode:path, $encode:path) => {{
        $decode($bytes)
            .map(|value| $encode(&value))
            .map_err(|error| error.to_string())
    }};
}

fn canonical_payload(name: &str, bytes: &[u8]) -> Result<Vec<u8>, String> {
    match name {
        "hello" => decode_hello(bytes)
            .and_then(|value| encode_hello(&value).map_err(|_| ControlDecodeError::LimitExceeded))
            .map_err(|error| error.to_string()),
        "welcome" => decode_welcome(bytes)
            .map(|value| encode_welcome(&value))
            .map_err(|error| error.to_string()),
        "reject" => decode_reject(bytes)
            .and_then(|value| encode_reject(&value).map_err(|_| ControlDecodeError::LimitExceeded))
            .map_err(|error| error.to_string()),
        "ping" => decode_ping(bytes)
            .map(encode_ping)
            .map_err(|error| error.to_string()),
        "pong" => decode_pong(bytes)
            .map(encode_pong)
            .map_err(|error| error.to_string()),
        "protocol-error" => decode_protocol_error(bytes)
            .and_then(|value| {
                encode_protocol_error(&value).map_err(|_| ControlDecodeError::LimitExceeded)
            })
            .map_err(|error| error.to_string()),
        "snapshot-begin" => decode_snapshot_begin(bytes)
            .map(encode_snapshot_begin)
            .map_err(|error| error.to_string()),
        "snapshot-end" => decode_snapshot_end(bytes)
            .map(encode_snapshot_end)
            .map_err(|error| error.to_string()),
        "snapshot-abort" => decode_snapshot_abort(bytes)
            .and_then(|value| {
                encode_snapshot_abort(&value).map_err(|_| ControlDecodeError::LimitExceeded)
            })
            .map_err(|error| error.to_string()),

        "output-upsert" => exact!(
            bytes,
            compositor::decode_output_upsert,
            compositor::encode_output_upsert
        ),
        "output-remove" => exact!(
            bytes,
            compositor::decode_output_remove,
            compositor::encode_output_remove
        ),
        "surface-upsert" => exact!(
            bytes,
            compositor::decode_surface_upsert,
            compositor::encode_surface_upsert
        ),
        "surface-remove" => exact!(
            bytes,
            compositor::decode_surface_remove,
            compositor::encode_surface_remove
        ),
        "buffer-attach" => exact!(
            bytes,
            compositor::decode_buffer_attach,
            compositor::encode_buffer_attach
        ),
        "buffer-detach" => exact!(
            bytes,
            compositor::decode_buffer_detach,
            compositor::encode_buffer_detach
        ),
        "buffer-release" => exact!(
            bytes,
            compositor::decode_buffer_release,
            compositor::encode_buffer_release
        ),
        "surface-damage" => compositor::decode_surface_damage(bytes)
            .and_then(|value| compositor::encode_surface_damage(&value))
            .map_err(|error| error.to_string()),
        "frame-commit" => exact!(
            bytes,
            compositor::decode_frame_commit,
            compositor::encode_frame_commit
        ),
        "frame-acknowledged" => exact!(
            bytes,
            compositor::decode_frame_acknowledged,
            compositor::encode_frame_acknowledged
        ),

        "output-descriptor-upsert" => output::decode_output_descriptor_upsert(bytes)
            .and_then(|value| output::encode_output_descriptor_upsert(&value))
            .map_err(|error| error.to_string()),
        "output-mode-upsert" => exact!(
            bytes,
            output::decode_output_mode_upsert,
            output::encode_output_mode_upsert
        ),
        "surface-output-state" | "rust-malformed-surface-output-count" => {
            output::decode_surface_output_state(bytes)
                .and_then(|value| output::encode_surface_output_state(&value))
                .map_err(|error| error.to_string())
        }
        "policy-output-upsert" => exact!(
            bytes,
            output::decode_policy_output_upsert,
            output::encode_policy_output_upsert
        ),
        "policy-window-output-hint" => exact!(
            bytes,
            output::decode_policy_window_output_hint,
            output::encode_policy_window_output_hint
        ),
        "output-state-query" => exact!(
            bytes,
            output::decode_output_state_query,
            output::encode_output_state_query
        ),
        "output-configuration-commit" => exact!(
            bytes,
            output::decode_output_configuration_commit,
            output::encode_output_configuration_commit
        ),
        "output-configuration-acknowledged" => exact!(
            bytes,
            output::decode_output_configuration_acknowledged,
            output::encode_output_configuration_acknowledged
        ),

        "policy-context-upsert" => exact!(
            bytes,
            decode_policy_context_upsert,
            encode_policy_context_upsert
        ),
        "policy-window-upsert" => exact!(
            bytes,
            decode_policy_window_upsert,
            encode_policy_window_upsert
        ),
        "policy-window-remove" => exact!(
            bytes,
            decode_policy_window_remove,
            encode_policy_window_remove
        ),
        "policy-commit" => exact!(bytes, decode_policy_commit, encode_policy_commit),
        "policy-window-state" => exact!(
            bytes,
            decode_policy_window_state,
            encode_policy_window_state
        ),
        "policy-acknowledged" => exact!(
            bytes,
            decode_policy_acknowledged,
            encode_policy_acknowledged
        ),
        "policy-bindings-upsert" => exact!(
            bytes,
            decode_policy_bindings_upsert,
            encode_policy_bindings_upsert
        ),
        "policy-lifecycle-window-upsert" => exact!(
            bytes,
            decode_policy_lifecycle_window_upsert,
            encode_policy_lifecycle_window_upsert
        ),
        "surface-policy-upsert" => exact!(
            bytes,
            decode_surface_policy_upsert,
            encode_surface_policy_upsert
        ),

        "synthetic-motion" => exact!(bytes, decode_synthetic_motion, encode_synthetic_motion),
        "synthetic-button" | "rust-malformed-synthetic-button-boolean" => {
            exact!(bytes, decode_synthetic_button, encode_synthetic_button)
        }
        "synthetic-key" => exact!(bytes, decode_synthetic_key, encode_synthetic_key),
        "synthetic-barrier" => exact!(bytes, decode_synthetic_barrier, encode_synthetic_barrier),
        "synthetic-input-acknowledged" => exact!(
            bytes,
            decode_synthetic_input_acknowledged,
            encode_synthetic_input_acknowledged
        ),
        "session-state-change" => exact!(
            bytes,
            decode_session_state_change,
            encode_session_state_change
        ),
        "session-state-acknowledged" => exact!(
            bytes,
            decode_session_state_acknowledged,
            encode_session_state_acknowledged
        ),

        "output-vrr-capability-upsert" | "malformed-vrr-capability-boolean" => exact!(
            bytes,
            vrr::decode_output_vrr_capability_upsert,
            vrr::encode_output_vrr_capability_upsert
        ),
        "output-vrr-policy-upsert" => exact!(
            bytes,
            vrr::decode_output_vrr_policy_upsert,
            vrr::encode_output_vrr_policy_upsert
        ),
        "output-vrr-state-upsert" => exact!(
            bytes,
            vrr::decode_output_vrr_state_upsert,
            vrr::encode_output_vrr_state_upsert
        ),
        "surface-vrr-state" => exact!(
            bytes,
            vrr::decode_surface_vrr_state,
            vrr::encode_surface_vrr_state
        ),
        "policy-window-vrr-upsert" => exact!(
            bytes,
            vrr::decode_policy_window_vrr_upsert,
            vrr::encode_policy_window_vrr_upsert
        ),
        "policy-output-vrr-upsert" => exact!(
            bytes,
            vrr::decode_policy_output_vrr_upsert,
            vrr::encode_policy_output_vrr_upsert
        ),
        "policy-window-vrr-state" => exact!(
            bytes,
            vrr::decode_policy_window_vrr_state,
            vrr::encode_policy_window_vrr_state
        ),
        "policy-output-vrr-state" | "rust-malformed-policy-vrr-reserved" => exact!(
            bytes,
            vrr::decode_policy_output_vrr_state,
            vrr::encode_policy_output_vrr_state
        ),
        "presentation-timing" => exact!(
            bytes,
            vrr::decode_presentation_timing,
            vrr::encode_presentation_timing
        ),
        other => Err(format!("unregistered payload fixture {other}")),
    }
}

#[test]
fn every_payload_codec_round_trips_the_legacy_fixture_exactly() {
    let fixtures = fixtures();
    let payloads: Vec<_> = fixtures
        .iter()
        .filter(|fixture| fixture.kind == "payload" && fixture.expected_ok)
        .collect();
    assert_eq!(
        payloads.len(),
        52,
        "one positive fixture is required per payload codec"
    );

    for fixture in payloads {
        let bytes = read_hex(&fixture.file);
        let reencoded = canonical_payload(&fixture.name, &bytes).unwrap_or_else(|error| {
            panic!("{}: Rust rejected legacy bytes: {error}", fixture.name)
        });
        assert_eq!(reencoded, bytes, "{}: Rust encoding drifted", fixture.name);
    }
}

#[test]
fn every_malformed_payload_fixture_is_rejected_by_rust() {
    for fixture in fixtures()
        .iter()
        .filter(|fixture| fixture.kind == "payload" && !fixture.expected_ok)
    {
        let bytes = read_hex(&fixture.file);
        assert!(
            canonical_payload(&fixture.name, &bytes).is_err(),
            "{}: Rust accepted malformed bytes",
            fixture.name
        );
    }
}

#[test]
fn rust_malformed_fixtures_are_derived_from_canonical_payloads() {
    let mut button = read_hex("synthetic-button.hex");
    button[13] = 2;
    assert_eq!(
        button,
        read_hex("rust-malformed-synthetic-button-boolean.hex")
    );

    let mut membership = read_hex("surface-output-state.hex");
    membership[44] = 9;
    assert_eq!(
        membership,
        read_hex("rust-malformed-surface-output-count.hex")
    );

    let mut vrr = read_hex("policy-output-vrr-state.hex");
    vrr[28] = 1;
    assert_eq!(vrr, read_hex("rust-malformed-policy-vrr-reserved.hex"));
}
