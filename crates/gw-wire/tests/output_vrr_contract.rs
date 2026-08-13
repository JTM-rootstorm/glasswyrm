use gw_wire::{compositor, output, vrr};

fn hex_bytes(text: &str) -> Vec<u8> {
    let compact: Vec<_> = text
        .bytes()
        .filter(|byte| !byte.is_ascii_whitespace())
        .collect();
    assert_eq!(compact.len() % 2, 0);
    compact
        .chunks_exact(2)
        .map(|pair| {
            let digit = |byte: u8| match byte {
                b'0'..=b'9' => byte - b'0',
                b'a'..=b'f' => byte - b'a' + 10,
                b'A'..=b'F' => byte - b'A' + 10,
                _ => panic!("invalid fixture hex"),
            };
            digit(pair[0]) << 4 | digit(pair[1])
        })
        .collect()
}

#[test]
fn canonical_output_descriptor_matches_legacy_fixture() {
    let bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/output-descriptor-upsert.hex"
    ));
    let value = output::decode_output_descriptor_upsert(&bytes).unwrap();
    assert_eq!(value.output_id, 0x0102_0304_0506_0708);
    assert_eq!(value.name, "DP-1");
    assert_eq!(
        output::encode_output_descriptor_upsert(&value).unwrap(),
        bytes
    );
}

#[test]
fn canonical_compositor_payloads_round_trip() {
    let output_bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/output-upsert.hex"
    ));
    let output_value = compositor::decode_output_upsert(&output_bytes).unwrap();
    assert_eq!(
        compositor::encode_output_upsert(&output_value),
        output_bytes
    );

    let buffer_bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/buffer-attach.hex"
    ));
    let buffer_value = compositor::decode_buffer_attach(&buffer_bytes).unwrap();
    assert_eq!(
        compositor::encode_buffer_attach(&buffer_value),
        buffer_bytes
    );
}

#[test]
fn canonical_output_acknowledgement_matches_legacy_fixture() {
    let bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/output-configuration-acknowledged.hex"
    ));
    let value = output::decode_output_configuration_acknowledged(&bytes).unwrap();
    assert_eq!(value.result, output::OutputConfigurationResult::Accepted);
    assert_eq!(
        output::encode_output_configuration_acknowledged(&value),
        bytes
    );
}

#[test]
fn canonical_vrr_payloads_match_legacy_fixtures() {
    let capability_bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/output-vrr-capability-upsert.hex"
    ));
    let capability = vrr::decode_output_vrr_capability_upsert(&capability_bytes).unwrap();
    assert!(capability.hardware_capable);
    assert_eq!(
        vrr::encode_output_vrr_capability_upsert(&capability),
        capability_bytes
    );

    let timing_bytes = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/presentation-timing.hex"
    ));
    let timing = vrr::decode_presentation_timing(&timing_bytes).unwrap();
    assert!(timing.timestamp_available);
    assert_eq!(vrr::encode_presentation_timing(&timing), timing_bytes);
}

#[test]
fn malformed_values_fail_closed() {
    let malformed_bool = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/malformed-vrr-capability-boolean.hex"
    ));
    assert_eq!(
        vrr::decode_output_vrr_capability_upsert(&malformed_bool),
        Err(compositor::ContractDecodeError::InvalidValue)
    );

    let mut trailing = hex_bytes(include_str!(
        "../../../tests/fixtures/gwipc/presentation-timing.hex"
    ));
    trailing.push(0);
    assert_eq!(
        vrr::decode_presentation_timing(&trailing),
        Err(compositor::ContractDecodeError::TrailingData)
    );

    let bounded_count = {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&1_u64.to_le_bytes());
        bytes.extend_from_slice(&u32::MAX.to_le_bytes());
        bytes.extend_from_slice(&0_u32.to_le_bytes());
        bytes
    };
    assert_eq!(
        compositor::decode_surface_damage(&bounded_count),
        Err(compositor::ContractDecodeError::LimitExceeded)
    );
}
