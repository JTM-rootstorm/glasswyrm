use gw_types::{
    Capabilities, ConnectionId, GWIPC_WIRE_VERSION, MessageType, RejectReason, Role, Sequence,
};
use gw_wire::{
    DecodeLimits, EnvelopeDecodeError, Hello, Reject, Welcome, decode_envelope, decode_hello,
    decode_reject, decode_welcome, encode_hello, encode_reject, encode_welcome,
};

fn fixture(name: &str) -> Vec<u8> {
    let text = match name {
        "pong-record" => include_str!("../../../tests/fixtures/gwipc/pong-record.hex"),
        "hello" => include_str!("../../../tests/fixtures/gwipc/hello.hex"),
        "welcome" => include_str!("../../../tests/fixtures/gwipc/welcome.hex"),
        "reject" => include_str!("../../../tests/fixtures/gwipc/reject.hex"),
        "malformed-envelope-magic" => {
            include_str!("../../../tests/fixtures/gwipc/malformed-envelope-magic.hex")
        }
        "malformed-hello-truncated" => {
            include_str!("../../../tests/fixtures/gwipc/malformed-hello-truncated.hex")
        }
        _ => panic!("unknown fixture {name}"),
    };
    let hex = text.trim().as_bytes();
    assert!(hex.len().is_multiple_of(2));
    hex.chunks_exact(2)
        .map(|pair| {
            let digit = |byte: u8| match byte {
                b'0'..=b'9' => byte - b'0',
                b'a'..=b'f' => byte - b'a' + 10,
                _ => panic!("fixture contains non-lowercase-hex byte"),
            };
            (digit(pair[0]) << 4) | digit(pair[1])
        })
        .collect()
}

#[test]
fn legacy_control_payloads_decode_and_rust_reencodes_exact_bytes() {
    let hello_bytes = fixture("hello");
    let hello = decode_hello(&hello_bytes).unwrap();
    assert_eq!(hello.sender_role, Role::TestProducer);
    assert_eq!(hello.name, "producer");
    assert_eq!(encode_hello(&hello).unwrap(), hello_bytes);

    let welcome_bytes = fixture("welcome");
    let welcome = decode_welcome(&welcome_bytes).unwrap();
    assert_eq!(welcome.sender_role, Role::TestConsumer);
    assert_eq!(welcome.connection_id, ConnectionId::new(99));
    assert_eq!(encode_welcome(&welcome), welcome_bytes);

    let reject_bytes = fixture("reject");
    let reject = decode_reject(&reject_bytes).unwrap();
    assert_eq!(reject.detail, "role");
    assert_eq!(encode_reject(&reject).unwrap(), reject_bytes);
}

#[test]
fn legacy_record_envelope_decodes_without_regenerating_the_fixture() {
    let record = fixture("pong-record");
    let envelope = decode_envelope(&record, 0, DecodeLimits::new(65_536)).unwrap();
    assert_eq!(envelope.message_type, MessageType::PONG);
    assert_eq!(envelope.sequence, Sequence::new(0x0102_0304_0506_0708));
    assert_eq!(envelope.reply_to, Sequence::new(0x1112_1314_1516_1718));
}

#[test]
fn malformed_legacy_fixtures_are_rejected_by_rust() {
    assert_eq!(
        decode_envelope(
            &fixture("malformed-envelope-magic"),
            0,
            DecodeLimits::new(65_536)
        ),
        Err(EnvelopeDecodeError::InvalidValue)
    );
    assert!(decode_hello(&fixture("malformed-hello-truncated")).is_err());
}

#[test]
fn rust_values_match_the_legacy_fixture_semantics() {
    let hello = Hello {
        minimum_version: GWIPC_WIRE_VERSION,
        maximum_version: GWIPC_WIRE_VERSION,
        sender_role: Role::TestProducer,
        offered_capabilities: Capabilities::from_bits_retain(0x0102_0304_0506_0708),
        required_capabilities: Capabilities::SNAPSHOTS,
        maximum_payload: 65_536,
        maximum_fd_count: 4,
        sender_instance_id: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
        name: "producer".to_owned(),
    };
    assert_eq!(encode_hello(&hello).unwrap(), fixture("hello"));

    let welcome = Welcome {
        selected_version: GWIPC_WIRE_VERSION,
        sender_role: Role::TestConsumer,
        negotiated_capabilities: Capabilities::SNAPSHOTS,
        negotiated_maximum_payload: 32_768,
        negotiated_maximum_fd_count: 2,
        connection_id: ConnectionId::new(99),
        sender_instance_id: [0xa5; 16],
    };
    assert_eq!(encode_welcome(&welcome), fixture("welcome"));

    let reject = Reject {
        reason: RejectReason::RoleNotAllowed,
        supported_minimum_version: GWIPC_WIRE_VERSION,
        supported_maximum_version: GWIPC_WIRE_VERSION,
        detail: "role".to_owned(),
    };
    assert_eq!(encode_reject(&reject).unwrap(), fixture("reject"));
}
