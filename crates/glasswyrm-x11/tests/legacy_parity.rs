use glasswyrm_x11::{
    ByteOrder, CoreError, CoreErrorCode, ReplyBuildError, ReplyBuilder, RequestFrameStatus,
    RequestFramer, SetupDecision, SetupParser, SetupReplyConfig, encode_core_error,
    encode_setup_failure, encode_setup_success, evaluate_setup_request,
};

fn decode_hex(text: &str) -> Vec<u8> {
    let digits: Vec<_> = text
        .bytes()
        .filter(|byte| !byte.is_ascii_whitespace())
        .collect();
    assert_eq!(digits.len() & 1, 0);
    digits
        .chunks_exact(2)
        .map(|pair| (nibble(pair[0]) << 4) | nibble(pair[1]))
        .collect()
}

fn nibble(byte: u8) -> u8 {
    match byte {
        b'0'..=b'9' => byte - b'0',
        b'a'..=b'f' => byte - b'a' + 10,
        _ => panic!("invalid fixture hex digit"),
    }
}

fn fixture(text: &str) -> Vec<u8> {
    decode_hex(text)
}

#[test]
fn setup_requests_and_replies_match_legacy_goldens() {
    let cases = [
        (
            ByteOrder::LittleEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-request-le.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-success-le.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-failure-le.hex"
            )),
        ),
        (
            ByteOrder::BigEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-request-be.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-success-be.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/setup-failure-be.hex"
            )),
        ),
    ];

    for (order, request, success, failure) in cases {
        let mut parser = SetupParser::default();
        let result = parser.consume(&request);
        assert_eq!(result.status, glasswyrm_x11::ParseStatus::Complete);
        assert_eq!(result.consumed, request.len());
        assert_eq!(parser.request().byte_order, order);
        assert_eq!(
            evaluate_setup_request(parser.request()),
            SetupDecision::Accepted
        );
        assert_eq!(
            encode_setup_success(order, &SetupReplyConfig::default()).unwrap(),
            success
        );
        assert_eq!(
            encode_setup_failure(order, b"Unsupported protocol version").unwrap(),
            failure
        );
    }
}

#[test]
fn ordinary_and_big_request_frames_match_legacy_goldens() {
    let cases = [
        (
            ByteOrder::LittleEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/request-ordinary-le.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/request-big-le.hex"
            )),
        ),
        (
            ByteOrder::BigEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/request-ordinary-be.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/request-big-be.hex"
            )),
        ),
    ];

    for (order, ordinary, big) in cases {
        for boundary in 0..=ordinary.len() {
            let mut framer = RequestFramer::with_default_limit(order);
            let first = framer.consume(&ordinary[..boundary]);
            assert_eq!(first.consumed, boundary);
            let second = framer.consume(&ordinary[boundary..]);
            assert_eq!(second.status, RequestFrameStatus::Complete);
            assert_eq!(framer.request().bytes, ordinary);
            assert_eq!(framer.request().opcode, 18);
            assert_eq!(framer.request().data, 2);
            assert_eq!(framer.request().length_units, 6);
            assert_eq!(framer.request().header_size, 4);
        }

        for boundary in 0..=big.len() {
            let mut framer = RequestFramer::with_default_limit(order);
            framer.enable_big_requests_with_default_limit();
            let first = framer.consume(&big[..boundary]);
            assert_eq!(first.consumed, boundary);
            let second = framer.consume(&big[boundary..]);
            assert_eq!(second.status, RequestFrameStatus::Complete);
            assert_eq!(framer.request().bytes, big);
            assert_eq!(framer.request().opcode, 72);
            assert_eq!(framer.request().data, 2);
            assert_eq!(framer.request().length_units, 6);
            assert_eq!(framer.request().header_size, 8);
            assert_eq!(framer.request().body().len(), 16);
            assert_eq!(framer.request().core_size(), 20);
        }
    }
}

#[test]
fn replies_and_errors_match_legacy_goldens() {
    let cases = [
        (
            ByteOrder::LittleEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/reply-le.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/error-le.hex"
            )),
        ),
        (
            ByteOrder::BigEndian,
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/reply-be.hex"
            )),
            fixture(include_str!(
                "../../../tests/fixtures/x11-transition/error-be.hex"
            )),
        ),
    ];

    for (order, reply_fixture, error_fixture) in cases {
        let mut reply = ReplyBuilder::new(order, 0x1_0001, 24);
        reply.write_u32(1).unwrap();
        reply.write_u16(1024).unwrap();
        reply.write_u16(768).unwrap();
        reply.write_payload_u32(0x1122_3344);
        assert_eq!(reply.finish().unwrap(), reply_fixture);

        assert_eq!(
            encode_core_error(
                order,
                CoreError {
                    code: CoreErrorCode::BadAtom,
                    sequence: 0x1_0002,
                    bad_value: 0xa1b2_c3d4,
                    major_opcode: 17,
                    minor_opcode: 0,
                },
            ),
            error_fixture
        );
    }
}

#[test]
fn reply_builder_preserves_fixed_and_payload_limits() {
    let mut overfull = ReplyBuilder::new(ByteOrder::LittleEndian, 1, 0);
    assert_eq!(
        overfull.write_padding(25),
        Err(ReplyBuildError::FixedFieldsTooLarge)
    );

    let mut padded = ReplyBuilder::new(ByteOrder::LittleEndian, 5, 0);
    padded.write_payload(&[1, 2, 3]);
    let bytes = padded.finish().unwrap();
    assert_eq!(bytes.len(), 36);
    assert_eq!(&bytes[4..8], &[1, 0, 0, 0]);
    assert_eq!(&bytes[32..], &[1, 2, 3, 0]);
}
