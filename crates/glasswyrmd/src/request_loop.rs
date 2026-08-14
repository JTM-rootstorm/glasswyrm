use glasswyrm_x11::{
    ByteOrder, CoreError, CoreErrorCode, InitialCoreDispatch, RequestFrameStatus, RequestFramer,
    dispatch_initial_core_request, encode_core_error,
};
use std::collections::VecDeque;
use std::io::{self, Write};
use std::os::unix::net::UnixStream;

pub(crate) const MAXIMUM_REQUESTS_PER_TURN: usize = 64;
pub(crate) const MAXIMUM_REQUEST_BYTES_PER_TURN: usize = 256 * 1024;
pub(crate) const MAXIMUM_QUEUED_OUTPUT: usize = 1024 * 1024;

#[derive(Debug, Default)]
pub(crate) struct RequestWorkBudget {
    requests: usize,
    bytes: usize,
}

impl RequestWorkBudget {
    pub(crate) fn available(&self) -> bool {
        self.requests < MAXIMUM_REQUESTS_PER_TURN && self.bytes < MAXIMUM_REQUEST_BYTES_PER_TURN
    }

    fn record(&mut self, bytes: usize) {
        self.requests += 1;
        self.bytes += bytes;
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SessionState {
    Established,
    DrainThenClose,
    Closed,
}

#[derive(Debug)]
struct OutputPacket {
    bytes: Vec<u8>,
    offset: usize,
}

#[derive(Debug, Default)]
struct OutputQueue {
    packets: VecDeque<OutputPacket>,
    queued_bytes: usize,
}

impl OutputQueue {
    fn enqueue(&mut self, bytes: Vec<u8>) -> bool {
        if bytes.len() > MAXIMUM_QUEUED_OUTPUT - self.queued_bytes {
            return false;
        }
        self.queued_bytes += bytes.len();
        self.packets.push_back(OutputPacket { bytes, offset: 0 });
        true
    }

    fn write_to(&mut self, stream: &mut UnixStream) -> io::Result<bool> {
        let mut wrote_any = false;
        while let Some(packet) = self.packets.front_mut() {
            match stream.write(&packet.bytes[packet.offset..]) {
                Ok(0) => {
                    return Err(io::Error::new(
                        io::ErrorKind::WriteZero,
                        "zero-length X11 socket write",
                    ));
                }
                Ok(count) => {
                    packet.offset += count;
                    self.queued_bytes -= count;
                    wrote_any = true;
                    if packet.offset == packet.bytes.len() {
                        self.packets.pop_front();
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::Interrupted => {}
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(wrote_any),
                Err(error) => return Err(error),
            }
        }
        Ok(wrote_any)
    }

    fn clear(&mut self) {
        self.packets.clear();
        self.queued_bytes = 0;
    }

    fn is_empty(&self) -> bool {
        self.packets.is_empty()
    }
}

pub(crate) struct RequestLoop {
    order: ByteOrder,
    focused_window: u32,
    framer: RequestFramer,
    request_sequence: u64,
    pending_input: Vec<u8>,
    output: OutputQueue,
    state: SessionState,
}

impl RequestLoop {
    pub(crate) fn new(
        order: ByteOrder,
        maximum_request_length: u16,
        focused_window: u32,
        setup_reply: Vec<u8>,
    ) -> Self {
        let mut output = OutputQueue::default();
        if !setup_reply.is_empty() {
            let enqueued = output.enqueue(setup_reply);
            debug_assert!(enqueued, "the bounded setup reply fits the output queue");
        }
        Self {
            order,
            focused_window,
            framer: RequestFramer::new(order, maximum_request_length)
                .expect("the advertised setup request limit is nonzero"),
            request_sequence: 0,
            pending_input: Vec::new(),
            output,
            state: SessionState::Established,
        }
    }

    pub(crate) fn accepts_input(&self) -> bool {
        self.state == SessionState::Established
    }

    pub(crate) fn is_closed(&self) -> bool {
        self.state == SessionState::Closed
    }

    pub(crate) fn has_pending_input(&self) -> bool {
        !self.pending_input.is_empty()
    }

    pub(crate) fn feed(&mut self, input: &[u8], budget: &mut RequestWorkBudget) -> usize {
        if !self.accepts_input() {
            return 0;
        }
        self.pending_input.extend_from_slice(input);
        self.process_pending(budget)
    }

    pub(crate) fn process_pending(&mut self, budget: &mut RequestWorkBudget) -> usize {
        if !self.accepts_input() {
            return 0;
        }
        let mut consumed = 0;
        let mut completed = 0;
        while consumed < self.pending_input.len() && budget.available() && self.accepts_input() {
            let result = self.framer.consume(&self.pending_input[consumed..]);
            consumed += result.consumed;
            match result.status {
                RequestFrameStatus::NeedMore => break,
                RequestFrameStatus::Complete => {
                    self.request_sequence = self.request_sequence.wrapping_add(1);
                    let request_size = self.framer.request().bytes.len();
                    budget.record(request_size);
                    completed += 1;
                    let packet = dispatch_initial_core_request(
                        self.order,
                        self.request_sequence,
                        self.focused_window,
                        self.framer.request(),
                    );
                    if let InitialCoreDispatch::Packet(bytes) = packet
                        && !self.output.enqueue(bytes)
                    {
                        self.close_now();
                        break;
                    }
                    self.framer.reset();
                }
                RequestFrameStatus::ZeroLength | RequestFrameStatus::TooLarge => {
                    self.reject_bad_length();
                    break;
                }
                RequestFrameStatus::TruncatedInput => {
                    self.close_now();
                    break;
                }
            }
        }

        if self.state == SessionState::Established {
            self.pending_input.drain(..consumed);
        } else {
            self.pending_input.clear();
        }
        completed
    }

    pub(crate) fn end_of_input(&mut self) {
        if !self.accepts_input() {
            return;
        }
        if self.framer.eof() == RequestFrameStatus::TruncatedInput || !self.pending_input.is_empty()
        {
            self.close_now();
        } else {
            self.state = SessionState::DrainThenClose;
            self.finish_draining();
        }
    }

    pub(crate) fn write_output(&mut self, stream: &mut UnixStream) -> io::Result<bool> {
        let wrote = self.output.write_to(stream)?;
        self.finish_draining();
        Ok(wrote)
    }

    pub(crate) fn close_now(&mut self) {
        self.state = SessionState::Closed;
        self.pending_input.clear();
        self.output.clear();
    }

    fn reject_bad_length(&mut self) {
        self.request_sequence = self.request_sequence.wrapping_add(1);
        let opcode = self.framer.request().opcode;
        let error = encode_core_error(
            self.order,
            CoreError {
                code: CoreErrorCode::BadLength,
                sequence: self.request_sequence,
                bad_value: 0,
                major_opcode: opcode,
                minor_opcode: 0,
            },
        );
        if self.output.enqueue(error) {
            self.state = SessionState::DrainThenClose;
        } else {
            self.close_now();
        }
    }

    fn finish_draining(&mut self) {
        if self.state == SessionState::DrainThenClose && self.output.is_empty() {
            self.state = SessionState::Closed;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glasswyrm_x11::CoreOpcode;

    fn request(order: ByteOrder, opcode: u8, units: u16) -> Vec<u8> {
        let mut bytes = vec![opcode, 0];
        let encoded_units = match order {
            ByteOrder::LittleEndian => units.to_le_bytes(),
            ByteOrder::BigEndian => units.to_be_bytes(),
        };
        bytes.extend_from_slice(&encoded_units);
        bytes.resize(usize::from(units) * 4, 0);
        bytes
    }

    fn session(order: ByteOrder) -> RequestLoop {
        RequestLoop::new(order, u16::MAX, 1, Vec::new())
    }

    fn packets(loop_: &RequestLoop) -> Vec<&[u8]> {
        loop_
            .output
            .packets
            .iter()
            .map(|packet| packet.bytes.as_slice())
            .collect()
    }

    fn sequence(order: ByteOrder, packet: &[u8]) -> u16 {
        match order {
            ByteOrder::LittleEndian => u16::from_le_bytes([packet[2], packet[3]]),
            ByteOrder::BigEndian => u16::from_be_bytes([packet[2], packet[3]]),
        }
    }

    #[test]
    fn fragmented_pipeline_dispatches_in_order_for_both_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut bytes = request(order, CoreOpcode::NoOperation as u8, 1);
            bytes.extend_from_slice(&request(order, 250, 1));
            bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
            let mut loop_ = session(order);
            let mut budget = RequestWorkBudget::default();
            assert_eq!(loop_.feed(&bytes[..3], &mut budget), 0);
            assert_eq!(loop_.feed(&bytes[3..], &mut budget), 3);
            assert_eq!(loop_.request_sequence, 3);
            let packets = packets(&loop_);
            assert_eq!(packets.len(), 2);
            assert_eq!(packets[0][1], CoreErrorCode::BadRequest as u8);
            assert_eq!(sequence(order, packets[0]), 2);
            assert_eq!(packets[1][0], 1);
            assert_eq!(sequence(order, packets[1]), 3);
        }
    }

    #[test]
    fn request_sequence_wraps_only_on_the_wire() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut loop_ = session(order);
            loop_.request_sequence = u64::from(u16::MAX) - 1;
            let mut bytes = request(order, 250, 1);
            bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
            assert_eq!(loop_.feed(&bytes, &mut RequestWorkBudget::default()), 2);
            let packets = packets(&loop_);
            assert_eq!(sequence(order, packets[0]), u16::MAX);
            assert_eq!(sequence(order, packets[1]), 0);
            assert_eq!(loop_.request_sequence, u64::from(u16::MAX) + 1);
        }
    }

    #[test]
    fn per_turn_request_and_byte_budgets_preserve_pending_work() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let mut pipeline = Vec::new();
        for _ in 0..=MAXIMUM_REQUESTS_PER_TURN {
            pipeline.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        }
        let mut first = RequestWorkBudget::default();
        assert_eq!(loop_.feed(&pipeline, &mut first), MAXIMUM_REQUESTS_PER_TURN);
        assert!(!first.available());
        assert!(loop_.has_pending_input());
        assert_eq!(loop_.process_pending(&mut RequestWorkBudget::default()), 1);

        let mut loop_ = session(order);
        let mut large = request(order, CoreOpcode::NoOperation as u8, u16::MAX);
        large.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        large.extend_from_slice(&request(order, CoreOpcode::NoOperation as u8, 1));
        let mut bytes = RequestWorkBudget::default();
        assert_eq!(loop_.feed(&large, &mut bytes), 2);
        assert_eq!(bytes.bytes, MAXIMUM_REQUEST_BYTES_PER_TURN);
        assert!(loop_.has_pending_input());
    }

    #[test]
    fn malformed_framing_sends_bad_length_then_closes() {
        let order = ByteOrder::LittleEndian;
        let mut zero = session(order);
        assert_eq!(
            zero.feed(&[43, 0, 0, 0], &mut RequestWorkBudget::default()),
            0
        );
        assert_eq!(zero.state, SessionState::DrainThenClose);
        let zero_packets = packets(&zero);
        assert_eq!(zero_packets.len(), 1);
        assert_eq!(zero_packets[0][1], CoreErrorCode::BadLength as u8);
        assert_eq!(zero_packets[0][10], CoreOpcode::GetInputFocus as u8);

        let mut oversized = RequestLoop::new(order, 2, 1, Vec::new());
        assert_eq!(
            oversized.feed(
                &request(order, CoreOpcode::NoOperation as u8, 3),
                &mut RequestWorkBudget::default(),
            ),
            0
        );
        assert_eq!(oversized.state, SessionState::DrainThenClose);
        assert_eq!(packets(&oversized)[0][1], CoreErrorCode::BadLength as u8);
    }

    #[test]
    fn complete_bad_length_is_recoverable_and_partial_eof_is_clean() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let mut bytes = request(order, CoreOpcode::GetInputFocus as u8, 2);
        bytes.extend_from_slice(&request(order, CoreOpcode::GetInputFocus as u8, 1));
        assert_eq!(loop_.feed(&bytes, &mut RequestWorkBudget::default()), 2);
        let packets = packets(&loop_);
        assert_eq!(packets[0][1], CoreErrorCode::BadLength as u8);
        assert_eq!(packets[1][0], 1);
        assert!(loop_.accepts_input());

        let mut truncated = session(order);
        assert_eq!(
            truncated.feed(&[43, 0, 1], &mut RequestWorkBudget::default()),
            0
        );
        truncated.end_of_input();
        assert!(truncated.is_closed());
        assert_eq!(truncated.output.queued_bytes, 0);
    }

    #[test]
    fn output_cap_fails_closed_without_retaining_queued_data() {
        let order = ByteOrder::LittleEndian;
        let mut loop_ = session(order);
        let unsupported = request(order, 250, 1);
        for _ in 0..=(MAXIMUM_QUEUED_OUTPUT / 32) {
            let mut budget = RequestWorkBudget::default();
            for _ in 0..MAXIMUM_REQUESTS_PER_TURN {
                if loop_.is_closed() {
                    break;
                }
                loop_.feed(&unsupported, &mut budget);
            }
            if loop_.is_closed() {
                break;
            }
        }
        assert!(loop_.is_closed());
        assert_eq!(loop_.output.queued_bytes, 0);
        assert!(loop_.output.is_empty());
    }
}
