use glasswyrm_x11::{
    ByteOrder, ParseStatus, RequestFrameStatus, RequestFramer, RequestFramerConfigError,
    SetupDecision, SetupEncodeError, SetupParser, SetupReplyConfig, encode_setup_failure,
    encode_setup_success, evaluate_setup_request,
};

fn push_u16(bytes: &mut Vec<u8>, order: ByteOrder, value: u16) {
    let encoded = match order {
        ByteOrder::LittleEndian => value.to_le_bytes(),
        ByteOrder::BigEndian => value.to_be_bytes(),
    };
    bytes.extend_from_slice(&encoded);
}

fn setup_request(order: ByteOrder, major: u16, minor: u16, name: &[u8], data: &[u8]) -> Vec<u8> {
    let mut bytes = vec![order.marker(), 0];
    push_u16(&mut bytes, order, major);
    push_u16(&mut bytes, order, minor);
    push_u16(&mut bytes, order, name.len() as u16);
    push_u16(&mut bytes, order, data.len() as u16);
    push_u16(&mut bytes, order, 0);
    bytes.extend_from_slice(name);
    bytes.resize((bytes.len() + 3) & !3, 0);
    bytes.extend_from_slice(data);
    bytes.resize((bytes.len() + 3) & !3, 0);
    bytes
}

fn ordinary_request(order: ByteOrder, opcode: u8, units: u16) -> Vec<u8> {
    let mut bytes = vec![opcode, 0];
    push_u16(&mut bytes, order, units);
    bytes.resize(usize::from(units) * 4, 0);
    bytes
}

fn big_header(order: ByteOrder, opcode: u8, units: u32) -> Vec<u8> {
    let mut bytes = vec![opcode, 0, 0, 0];
    let encoded = match order {
        ByteOrder::LittleEndian => units.to_le_bytes(),
        ByteOrder::BigEndian => units.to_be_bytes(),
    };
    bytes.extend_from_slice(&encoded);
    bytes
}

#[test]
fn setup_parser_handles_every_split_padding_and_post_setup_bytes() {
    for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
        let bytes = setup_request(order, 11, 0, b"abc", &[0x55, 0xaa]);
        for boundary in 0..=bytes.len() {
            let mut parser = SetupParser::default();
            assert_eq!(parser.consume(&bytes[..boundary]).consumed, boundary);
            assert_eq!(
                parser.consume(&bytes[boundary..]).status,
                ParseStatus::Complete
            );
            assert_eq!(parser.request().authorization_name, b"abc");
            assert_eq!(parser.request().authorization_data, [0x55, 0xaa]);
            assert_eq!(
                evaluate_setup_request(parser.request()),
                SetupDecision::UnsupportedAuthorization
            );
        }

        let bare = setup_request(order, 11, 0, b"", b"");
        let mut pipelined = bare.clone();
        pipelined.extend_from_slice(&[127, 0, 1, 0]);
        let mut parser = SetupParser::default();
        let result = parser.consume(&pipelined);
        assert_eq!(result.status, ParseStatus::Complete);
        assert_eq!(result.consumed, bare.len());
    }
}

#[test]
fn setup_parser_rejects_malformed_and_bounded_inputs_in_legacy_order() {
    let mut invalid = SetupParser::default();
    assert_eq!(
        invalid.consume(b"x00").status,
        ParseStatus::InvalidByteOrder
    );
    assert_eq!(invalid.consume(b"more").consumed, 0);

    let complete = setup_request(ByteOrder::LittleEndian, 11, 0, b"", b"");
    for length in 0..complete.len() {
        let mut parser = SetupParser::default();
        assert_eq!(
            parser.consume(&complete[..length]).status,
            ParseStatus::NeedMore
        );
        assert_eq!(parser.eof(), ParseStatus::TruncatedInput);
    }

    let with_auth = setup_request(ByteOrder::LittleEndian, 11, 0, b"abc", b"abc");
    let mut capped = SetupParser::new(15);
    assert_eq!(
        capped.consume(&with_auth).status,
        ParseStatus::MessageTooLarge
    );
    let mut below_header = SetupParser::new(11);
    assert_eq!(
        below_header.consume(&complete).status,
        ParseStatus::MessageTooLarge
    );
    assert_eq!(below_header.consume(&complete).consumed, 0);

    for (major, minor) in [(10, 0), (11, 1)] {
        let bytes = setup_request(ByteOrder::BigEndian, major, minor, b"", b"");
        let mut parser = SetupParser::default();
        assert_eq!(parser.consume(&bytes).status, ParseStatus::Complete);
        assert_eq!(
            evaluate_setup_request(parser.request()),
            SetupDecision::UnsupportedVersion
        );
    }
}

#[test]
fn setup_reply_profiles_and_limits_match_legacy_behavior() {
    for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
        let config = SetupReplyConfig {
            game_compat: true,
            screen: glasswyrm_x11::ScreenModel {
                width_pixels: 1440,
                height_pixels: 600,
                ..Default::default()
            },
            ..Default::default()
        };
        let reply = encode_setup_success(order, &config).unwrap();
        assert_eq!(reply.len(), 192);
        assert_eq!(reply[28], 1);
        assert_eq!(reply[29], 4);
        assert_eq!(&reply[64..67], &[1, 1, 32]);
        assert_eq!(&reply[72..75], &[8, 8, 32]);
        assert_eq!(&reply[80..83], &[24, 32, 32]);
        assert_eq!(&reply[88..91], &[32, 32, 32]);
    }
    assert_eq!(
        encode_setup_failure(ByteOrder::LittleEndian, &[b'x'; 256]),
        Err(SetupEncodeError::FailureReasonTooLarge)
    );
}

#[test]
fn request_framer_preserves_pipelines_limits_and_eof_status() {
    for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
        let first = ordinary_request(order, 127, 1);
        let second = ordinary_request(order, 43, 1);
        let mut pipeline = first.clone();
        pipeline.extend_from_slice(&second);
        let mut framer = RequestFramer::with_default_limit(order);
        let result = framer.consume(&pipeline);
        assert_eq!(result.status, RequestFrameStatus::Complete);
        assert_eq!(result.consumed, first.len());
        framer.reset();
        assert_eq!(
            framer.consume(&pipeline[result.consumed..]).status,
            RequestFrameStatus::Complete
        );
        assert_eq!(framer.request().opcode, 43);

        let too_large = ordinary_request(order, 18, 3);
        let mut capped = RequestFramer::new(order, 2).unwrap();
        let result = capped.consume(&too_large);
        assert_eq!(result.status, RequestFrameStatus::TooLarge);
        assert_eq!(result.consumed, 4);
        assert_eq!(capped.expected_size(), 4);

        let mut partial_header = RequestFramer::with_default_limit(order);
        assert_eq!(
            partial_header.consume(&too_large[..3]).status,
            RequestFrameStatus::NeedMore
        );
        assert_eq!(partial_header.eof(), RequestFrameStatus::TruncatedInput);
        let mut partial_body = RequestFramer::with_default_limit(order);
        assert_eq!(
            partial_body.consume(&too_large[..7]).status,
            RequestFrameStatus::NeedMore
        );
        assert_eq!(partial_body.eof(), RequestFrameStatus::TruncatedInput);

        let mut zero = RequestFramer::with_default_limit(order);
        assert_eq!(
            zero.consume(&[18, 0, 0, 0]).status,
            RequestFrameStatus::ZeroLength
        );

        let mut big = RequestFramer::with_default_limit(order);
        big.enable_big_requests(4).unwrap();
        assert_eq!(
            big.consume(&big_header(order, 72, 5)).status,
            RequestFrameStatus::TooLarge
        );
        assert_eq!(big.expected_size(), 8);

        let mut too_short = RequestFramer::with_default_limit(order);
        too_short.enable_big_requests_with_default_limit();
        assert_eq!(
            too_short.consume(&big_header(order, 72, 1)).status,
            RequestFrameStatus::TooLarge
        );
    }

    assert_eq!(
        RequestFramer::new(ByteOrder::LittleEndian, 0).unwrap_err(),
        RequestFramerConfigError::ZeroOrdinaryLimit
    );
    let mut framer = RequestFramer::with_default_limit(ByteOrder::LittleEndian);
    assert_eq!(
        framer.enable_big_requests(1),
        Err(RequestFramerConfigError::InvalidBigRequestsLimit)
    );
}
