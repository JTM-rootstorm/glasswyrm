use crate::ResourceBaseLease;
use crate::request_loop::{RequestLoop, RequestWorkBudget};
use glasswyrm_x11::{
    ByteOrder, ParseStatus, SetupDecision, SetupParser, SetupReplyConfig, encode_setup_failure,
    encode_setup_success, evaluate_setup_request,
};
use std::io::{Read, Write};
use std::net::Shutdown;
use std::os::unix::net::UnixStream;
use std::thread;
use std::time::Duration;

pub(crate) fn serve(mut stream: UnixStream, identifier: u64, resource_base: ResourceBaseLease) {
    let mut parser = SetupParser::default();
    let mut input = [0_u8; 4096];
    loop {
        let count = match stream.read(&mut input) {
            Ok(0) => return,
            Ok(count) => count,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error) => {
                eprintln!("glasswyrmd: client {identifier}: setup read failed: {error}");
                return;
            }
        };
        let result = parser.consume(&input[..count]);
        match result.status {
            ParseStatus::NeedMore => {}
            ParseStatus::Complete => {
                match prepare_setup(identifier, resource_base.base(), &parser) {
                    SetupCompletion::Accepted { order, reply } => {
                        let mut request_loop = RequestLoop::new(
                            order,
                            SetupReplyConfig::default().screen.maximum_request_length,
                            SetupReplyConfig::default().screen.root_window,
                            reply,
                        );
                        let pipelined = &input[result.consumed..count];
                        serve_established(&mut stream, identifier, &mut request_loop, pipelined);
                    }
                    SetupCompletion::Rejected { reply } => {
                        if let Err(error) = stream.write_all(&reply) {
                            eprintln!(
                                "glasswyrmd: client {identifier}: setup reply failed: {error}"
                            );
                        }
                        let _ = stream.shutdown(Shutdown::Both);
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

enum SetupCompletion {
    Accepted { order: ByteOrder, reply: Vec<u8> },
    Rejected { reply: Vec<u8> },
}

fn prepare_setup(
    identifier: u64,
    resource_base: glasswyrm_core::resource_id::ResourceBase,
    parser: &SetupParser,
) -> SetupCompletion {
    let request = parser.request();
    eprintln!(
        "glasswyrmd: client {identifier}: setup byte_order={} protocol={}.{}",
        char::from(request.byte_order.marker()),
        request.protocol_major,
        request.protocol_minor
    );
    match evaluate_setup_request(request) {
        SetupDecision::Accepted => {
            let config = SetupReplyConfig {
                resource_id_base: resource_base.get(),
                ..Default::default()
            };
            let reply = encode_setup_success(request.byte_order, &config)
                .expect("the fixed setup profile fits the X11 length field");
            eprintln!("glasswyrmd: client {identifier}: X11 setup accepted");
            SetupCompletion::Accepted {
                order: request.byte_order,
                reply,
            }
        }
        SetupDecision::UnsupportedVersion => SetupCompletion::Rejected {
            reply: encode_setup_failure(request.byte_order, b"X11 protocol 11.0 required")
                .expect("the fixed failure reason fits the X11 length field"),
        },
        SetupDecision::UnsupportedAuthorization => SetupCompletion::Rejected {
            reply: encode_setup_failure(
                request.byte_order,
                b"authorization is not supported in Milestone 2",
            )
            .expect("the fixed failure reason fits the X11 length field"),
        },
    }
}

fn serve_established(
    stream: &mut UnixStream,
    identifier: u64,
    request_loop: &mut RequestLoop,
    pipelined: &[u8],
) {
    if let Err(error) = stream.set_nonblocking(true) {
        eprintln!("glasswyrmd: client {identifier}: nonblocking setup failed: {error}");
        return;
    }

    let mut initial_input = Some(pipelined);
    while !request_loop.is_closed() {
        let mut progressed = false;
        let mut budget = RequestWorkBudget::default();
        progressed |= request_loop.process_pending(&mut budget) != 0;

        if request_loop.accepts_input() && budget.available() {
            if let Some(bytes) = initial_input.take()
                && !bytes.is_empty()
            {
                progressed = true;
                request_loop.feed(bytes, &mut budget);
            }

            while request_loop.accepts_input()
                && budget.available()
                && !request_loop.has_pending_input()
            {
                let mut input = [0_u8; 4096];
                match stream.read(&mut input) {
                    Ok(0) => {
                        request_loop.end_of_input();
                        progressed = true;
                        break;
                    }
                    Ok(count) => {
                        progressed = true;
                        request_loop.feed(&input[..count], &mut budget);
                    }
                    Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(error) => {
                        eprintln!("glasswyrmd: client {identifier}: request read failed: {error}");
                        request_loop.close_now();
                        break;
                    }
                }
            }
        }

        match request_loop.write_output(stream) {
            Ok(wrote) => progressed |= wrote,
            Err(error) => {
                eprintln!("glasswyrmd: client {identifier}: response write failed: {error}");
                request_loop.close_now();
            }
        }

        if !progressed && !request_loop.is_closed() {
            thread::sleep(Duration::from_millis(1));
        }
    }
    let _ = stream.shutdown(Shutdown::Both);
}
