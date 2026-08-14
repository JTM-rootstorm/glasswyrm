use crate::ResourceBaseLease;
use glasswyrm_x11::{
    ParseStatus, SetupDecision, SetupParser, SetupReplyConfig, encode_setup_failure,
    encode_setup_success, evaluate_setup_request,
};
use std::io::{Read, Write};
use std::net::Shutdown;
use std::os::unix::net::UnixStream;

pub(crate) fn serve(mut stream: UnixStream, identifier: u64, resource_base: ResourceBaseLease) {
    let mut parser = SetupParser::default();
    let mut input = [0_u8; 4096];
    loop {
        let count = match stream.read(&mut input) {
            Ok(0) => return,
            Ok(count) => count,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error) => {
                eprintln!("glasswyrmd: client {identifier}: read failed: {error}");
                return;
            }
        };
        let result = parser.consume(&input[..count]);
        match result.status {
            ParseStatus::NeedMore => {}
            ParseStatus::Complete => {
                if complete_setup(&mut stream, identifier, resource_base.base(), &parser) {
                    if result.consumed == count {
                        wait_after_setup(&mut stream, identifier);
                    } else {
                        eprintln!(
                            "glasswyrmd: client {identifier}: request dispatch is not available in the setup-only Rust daemon"
                        );
                    }
                }
                return;
            }
            ParseStatus::InvalidByteOrder => {
                eprintln!("glasswyrmd: client {identifier}: invalid X11 byte-order marker");
                return;
            }
            ParseStatus::MessageTooLarge => {
                eprintln!(
                    "glasswyrmd: client {identifier}: X11 setup message exceeds configured limit"
                );
                return;
            }
            ParseStatus::LengthOverflow => {
                eprintln!("glasswyrmd: client {identifier}: X11 setup message length overflow");
                return;
            }
            ParseStatus::TruncatedInput => return,
        }
    }
}

fn complete_setup(
    stream: &mut UnixStream,
    identifier: u64,
    resource_base: glasswyrm_core::resource_id::ResourceBase,
    parser: &SetupParser,
) -> bool {
    let request = parser.request();
    eprintln!(
        "glasswyrmd: client {identifier}: setup byte_order={} protocol={}.{}",
        char::from(request.byte_order.marker()),
        request.protocol_major,
        request.protocol_minor
    );
    let (reply, accepted) = match evaluate_setup_request(request) {
        SetupDecision::Accepted => {
            let config = SetupReplyConfig {
                resource_id_base: resource_base.get(),
                ..Default::default()
            };
            (
                encode_setup_success(request.byte_order, &config)
                    .expect("the fixed setup profile fits the X11 length field"),
                true,
            )
        }
        SetupDecision::UnsupportedVersion => (
            encode_setup_failure(request.byte_order, b"X11 protocol 11.0 required")
                .expect("the fixed failure reason fits the X11 length field"),
            false,
        ),
        SetupDecision::UnsupportedAuthorization => (
            encode_setup_failure(
                request.byte_order,
                b"authorization is not supported in Milestone 2",
            )
            .expect("the fixed failure reason fits the X11 length field"),
            false,
        ),
    };
    if let Err(error) = stream.write_all(&reply) {
        eprintln!("glasswyrmd: client {identifier}: setup reply failed: {error}");
        return false;
    }
    if accepted {
        eprintln!("glasswyrmd: client {identifier}: X11 setup accepted");
        true
    } else {
        let _ = stream.shutdown(Shutdown::Both);
        false
    }
}

fn wait_after_setup(stream: &mut UnixStream, identifier: u64) {
    let mut input = [0_u8; 256];
    loop {
        match stream.read(&mut input) {
            Ok(0) => return,
            Ok(_) => {
                eprintln!(
                    "glasswyrmd: client {identifier}: request dispatch is not available in the setup-only Rust daemon"
                );
                return;
            }
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
            Err(_) => return,
        }
    }
}
