use crate::request_loop::{RequestLoop, RequestWorkBudget, ServerState};
use glasswyrm_core::resource_id::ResourceBase;
use glasswyrm_x11::{
    ByteOrder, CoreClient, ParseStatus, SCREEN_MODEL, SetupDecision, SetupParser, SetupReplyConfig,
    encode_setup_failure, encode_setup_success, evaluate_setup_request,
};
use std::io::{self, Write};
use std::sync::{Arc, Mutex};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum SetupProgress {
    NeedMore,
    Accepted { order: ByteOrder },
    Rejected,
    InvalidByteOrder,
    MessageTooLarge,
    LengthOverflow,
    TruncatedInput,
}

#[derive(Debug)]
struct RejectedOutput {
    bytes: Vec<u8>,
    offset: usize,
}

impl RejectedOutput {
    fn write_to(&mut self, writer: &mut impl Write) -> io::Result<bool> {
        let mut wrote = false;
        while self.offset < self.bytes.len() {
            match writer.write(&self.bytes[self.offset..]) {
                Ok(0) => {
                    return Err(io::Error::new(
                        io::ErrorKind::WriteZero,
                        "zero-length X11 write",
                    ));
                }
                Ok(count) => {
                    self.offset += count;
                    wrote = true;
                }
                Err(error) if error.kind() == io::ErrorKind::Interrupted => {}
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(error) => return Err(error),
            }
        }
        Ok(wrote)
    }

    fn finished(&self) -> bool {
        self.offset == self.bytes.len()
    }
}

enum State {
    Setup(SetupParser),
    Established(RequestLoop),
    Rejected(RejectedOutput),
    Closed,
}

/// Byte-driven X11 session state independent of listeners, credentials, socket
/// paths, nonblocking flags, and shutdown syscalls.
pub(crate) struct ClientSession {
    identifier: u64,
    resource_base: ResourceBase,
    server_state: Arc<Mutex<ServerState>>,
    state: State,
}

impl ClientSession {
    pub(crate) fn new(
        identifier: u64,
        resource_base: ResourceBase,
        server_state: Arc<Mutex<ServerState>>,
    ) -> Self {
        Self {
            identifier,
            resource_base,
            server_state,
            state: State::Setup(SetupParser::default()),
        }
    }

    pub(crate) fn is_setup(&self) -> bool {
        matches!(self.state, State::Setup(_))
    }
    pub(crate) fn is_established(&self) -> bool {
        matches!(self.state, State::Established(_))
    }
    pub(crate) fn is_closed(&self) -> bool {
        matches!(self.state, State::Closed)
    }

    pub(crate) fn feed_setup(
        &mut self,
        input: &[u8],
        budget: &mut RequestWorkBudget,
    ) -> SetupProgress {
        let State::Setup(parser) = &mut self.state else {
            return SetupProgress::NeedMore;
        };
        let result = parser.consume(input);
        match result.status {
            ParseStatus::NeedMore => SetupProgress::NeedMore,
            ParseStatus::Complete => {
                let request = parser.request();
                let order = request.byte_order;
                match evaluate_setup_request(request) {
                    SetupDecision::Accepted => {
                        let config = SetupReplyConfig {
                            resource_id_base: self.resource_base.get(),
                            ..Default::default()
                        };
                        let reply = encode_setup_success(order, &config)
                            .expect("the fixed setup profile fits the X11 length field");
                        let mut loop_ = RequestLoop::new_for_client(
                            order,
                            config.screen.maximum_request_length,
                            config.screen.root_window,
                            reply,
                            CoreClient::new(
                                self.identifier,
                                self.resource_base.get(),
                                SCREEN_MODEL.resource_id_mask,
                            ),
                            Arc::clone(&self.server_state),
                        );
                        if result.consumed < input.len() {
                            loop_.feed(&input[result.consumed..], budget);
                        }
                        self.state = State::Established(loop_);
                        SetupProgress::Accepted { order }
                    }
                    SetupDecision::UnsupportedVersion => {
                        self.reject(
                            encode_setup_failure(order, b"X11 protocol 11.0 required")
                                .expect("the fixed failure reason fits the X11 length field"),
                        );
                        SetupProgress::Rejected
                    }
                    SetupDecision::UnsupportedAuthorization => {
                        self.reject(
                            encode_setup_failure(
                                order,
                                b"authorization is not supported in Milestone 2",
                            )
                            .expect("the fixed failure reason fits the X11 length field"),
                        );
                        SetupProgress::Rejected
                    }
                }
            }
            ParseStatus::InvalidByteOrder => {
                self.state = State::Closed;
                SetupProgress::InvalidByteOrder
            }
            ParseStatus::MessageTooLarge => {
                self.state = State::Closed;
                SetupProgress::MessageTooLarge
            }
            ParseStatus::LengthOverflow => {
                self.state = State::Closed;
                SetupProgress::LengthOverflow
            }
            ParseStatus::TruncatedInput => {
                self.state = State::Closed;
                SetupProgress::TruncatedInput
            }
        }
    }

    pub(crate) fn feed_established(
        &mut self,
        input: &[u8],
        budget: &mut RequestWorkBudget,
    ) -> usize {
        match &mut self.state {
            State::Established(loop_) => loop_.feed(input, budget),
            _ => 0,
        }
    }

    pub(crate) fn process_pending(&mut self, budget: &mut RequestWorkBudget) -> usize {
        match &mut self.state {
            State::Established(loop_) => loop_.process_pending(budget),
            _ => 0,
        }
    }

    pub(crate) fn accepts_input(&self) -> bool {
        matches!(&self.state, State::Established(loop_) if loop_.accepts_input())
    }

    pub(crate) fn has_pending_input(&self) -> bool {
        matches!(&self.state, State::Established(loop_) if loop_.has_pending_input())
    }

    pub(crate) fn end_of_input(&mut self) {
        match &mut self.state {
            State::Setup(_) => self.state = State::Closed,
            State::Established(loop_) => loop_.end_of_input(),
            State::Rejected(_) | State::Closed => {}
        }
    }

    pub(crate) fn close_now(&mut self) {
        if let State::Established(loop_) = &mut self.state {
            loop_.close_now();
        }
        self.state = State::Closed;
    }

    pub(crate) fn write_output(&mut self, writer: &mut impl Write) -> io::Result<bool> {
        match &mut self.state {
            State::Established(loop_) => {
                let wrote = loop_.write_output(writer)?;
                if loop_.is_closed() {
                    self.state = State::Closed;
                }
                Ok(wrote)
            }
            State::Rejected(output) => {
                let wrote = output.write_to(writer)?;
                if output.finished() {
                    self.state = State::Closed;
                }
                Ok(wrote)
            }
            State::Setup(_) | State::Closed => Ok(false),
        }
    }

    fn reject(&mut self, bytes: Vec<u8>) {
        self.state = State::Rejected(RejectedOutput { bytes, offset: 0 });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn setup(order: ByteOrder) -> Vec<u8> {
        let marker = order.marker();
        let mut bytes = vec![marker, 0];
        let major = 11_u16;
        let minor = 0_u16;
        let encode = |value: u16| match order {
            ByteOrder::LittleEndian => value.to_le_bytes(),
            ByteOrder::BigEndian => value.to_be_bytes(),
        };
        bytes.extend_from_slice(&encode(major));
        bytes.extend_from_slice(&encode(minor));
        bytes.extend_from_slice(&encode(0));
        bytes.extend_from_slice(&encode(0));
        bytes.extend_from_slice(&[0, 0]);
        bytes
    }

    fn session() -> ClientSession {
        ClientSession::new(
            7,
            ResourceBase::new(0x0040_0000),
            Arc::new(Mutex::new(ServerState::default())),
        )
    }

    struct ShortWouldBlockWriter {
        bytes: Vec<u8>,
        maximum_write: usize,
        would_block_next: bool,
    }

    impl ShortWouldBlockWriter {
        fn new(maximum_write: usize) -> Self {
            Self {
                bytes: Vec::new(),
                maximum_write,
                would_block_next: false,
            }
        }
    }

    impl Write for ShortWouldBlockWriter {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            if self.would_block_next {
                self.would_block_next = false;
                return Err(io::Error::from(io::ErrorKind::WouldBlock));
            }
            let count = self.maximum_write.min(bytes.len());
            self.bytes.extend_from_slice(&bytes[..count]);
            self.would_block_next = true;
            Ok(count)
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    #[test]
    fn invalid_byte_order_and_incomplete_setup_close_without_output() {
        for invalid in [0_u8, b'x', 0xff] {
            let mut session = session();
            assert_eq!(
                session.feed_setup(&[invalid], &mut RequestWorkBudget::default()),
                SetupProgress::InvalidByteOrder
            );
            assert!(session.is_closed());
            let mut output = Vec::new();
            assert!(!session.write_output(&mut output).unwrap());
            assert!(output.is_empty());
        }

        let mut truncated = session();
        let bytes = setup(ByteOrder::LittleEndian);
        assert_eq!(
            truncated.feed_setup(&bytes[..7], &mut RequestWorkBudget::default()),
            SetupProgress::NeedMore
        );
        truncated.end_of_input();
        assert!(truncated.is_closed());
    }

    #[test]
    fn setup_rejection_and_success_survive_short_writes_and_would_block() {
        let order = ByteOrder::BigEndian;
        let mut rejected_bytes = setup(order);
        rejected_bytes[2..4].copy_from_slice(&10_u16.to_be_bytes());
        let mut rejected = session();
        assert_eq!(
            rejected.feed_setup(&rejected_bytes, &mut RequestWorkBudget::default()),
            SetupProgress::Rejected
        );
        let expected = encode_setup_failure(order, b"X11 protocol 11.0 required").unwrap();
        let mut writer = ShortWouldBlockWriter::new(5);
        for _ in 0..expected.len() + 2 {
            rejected.write_output(&mut writer).unwrap();
            if rejected.is_closed() {
                break;
            }
        }
        assert!(rejected.is_closed());
        assert_eq!(writer.bytes, expected);

        let mut accepted = session();
        assert_eq!(
            accepted.feed_setup(&setup(order), &mut RequestWorkBudget::default()),
            SetupProgress::Accepted { order }
        );
        let expected = encode_setup_success(
            order,
            &SetupReplyConfig {
                resource_id_base: 0x0040_0000,
                ..Default::default()
            },
        )
        .unwrap();
        let mut writer = ShortWouldBlockWriter::new(7);
        for _ in 0..expected.len() + 2 {
            if !accepted.write_output(&mut writer).unwrap() {
                break;
            }
        }
        assert!(accepted.is_established());
        assert_eq!(writer.bytes, expected);
    }

    #[test]
    fn explicit_cancellation_discards_pending_session_work() {
        let order = ByteOrder::LittleEndian;
        let mut session = session();
        assert_eq!(
            session.feed_setup(&setup(order), &mut RequestWorkBudget::default()),
            SetupProgress::Accepted { order }
        );
        session.feed_established(&[43, 0, 1], &mut RequestWorkBudget::default());
        session.close_now();
        assert!(session.is_closed());
        assert!(!session.accepts_input());
        assert!(!session.has_pending_input());
        let mut output = Vec::new();
        assert!(!session.write_output(&mut output).unwrap());
        assert!(output.is_empty());
    }

    #[test]
    fn setup_and_established_io_are_driven_entirely_by_byte_buffers() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut session = session();
            let bytes = setup(order);
            let split = 5;
            assert_eq!(
                session.feed_setup(&bytes[..split], &mut RequestWorkBudget::default()),
                SetupProgress::NeedMore
            );
            assert_eq!(
                session.feed_setup(&bytes[split..], &mut RequestWorkBudget::default()),
                SetupProgress::Accepted { order }
            );
            assert!(session.is_established());
            let mut output = Vec::new();
            assert!(session.write_output(&mut output).unwrap());
            assert_eq!(output[0], 1, "setup success marker");
        }
    }

    #[test]
    fn pipelined_request_is_preserved_across_setup_transition() {
        let order = ByteOrder::LittleEndian;
        let mut bytes = setup(order);
        bytes.extend_from_slice(&[127, 0, 1, 0]);
        let mut session = session();
        let mut budget = RequestWorkBudget::default();
        assert_eq!(
            session.feed_setup(&bytes, &mut budget),
            SetupProgress::Accepted { order }
        );
        assert!(!session.has_pending_input());
    }
}
