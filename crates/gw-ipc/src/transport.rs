use core::ffi::{c_int, c_void};
use core::fmt;
use core::mem::{align_of, size_of};
use core::ptr;
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd, RawFd};

use gw_wire::{
    DecodeLimits, Envelope, EnvelopeDecodeError, GWIPC_ENVELOPE_SIZE, decode_envelope,
    encode_envelope,
};

pub const HARD_MAXIMUM_PAYLOAD: u32 = 1024 * 1024;
pub const HARD_MAXIMUM_FDS: u16 = 16;

const AF_UNIX: c_int = 1;
const SOCK_SEQPACKET: c_int = 5;
const SOCK_CLOEXEC: c_int = 0o2_000_000;
const SOCK_NONBLOCK: c_int = 0o4_000;
const SOL_SOCKET: c_int = 1;
const SCM_RIGHTS: c_int = 1;
const MSG_CTRUNC: c_int = 0x08;
const MSG_TRUNC: c_int = 0x20;
const MSG_DONTWAIT: c_int = 0x40;
const MSG_NOSIGNAL: c_int = 0x4000;
const MSG_CMSG_CLOEXEC: c_int = 0x4000_0000;

#[repr(C)]
struct Iovec {
    iov_base: *mut c_void,
    iov_len: usize,
}

#[repr(C)]
struct MessageHeader {
    msg_name: *mut c_void,
    msg_namelen: u32,
    msg_iov: *mut Iovec,
    msg_iovlen: usize,
    msg_control: *mut c_void,
    msg_controllen: usize,
    msg_flags: c_int,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct ControlHeader {
    cmsg_len: usize,
    cmsg_level: c_int,
    cmsg_type: c_int,
}

unsafe extern "C" {
    fn socketpair(domain: c_int, kind: c_int, protocol: c_int, sockets: *mut c_int) -> c_int;
    fn sendmsg(socket: c_int, message: *const MessageHeader, flags: c_int) -> isize;
    fn recvmsg(socket: c_int, message: *mut MessageHeader, flags: c_int) -> isize;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TransportLimits {
    pub maximum_payload: u32,
    pub maximum_fd_count: u16,
}

impl TransportLimits {
    pub const DEFAULT: Self = Self {
        maximum_payload: 65_536,
        maximum_fd_count: 4,
    };

    pub fn new(maximum_payload: u32, maximum_fd_count: u16) -> Result<Self, TransportError> {
        if maximum_payload == 0
            || maximum_payload > HARD_MAXIMUM_PAYLOAD
            || maximum_fd_count > HARD_MAXIMUM_FDS
        {
            return Err(TransportError::InvalidLimits);
        }
        Ok(Self {
            maximum_payload,
            maximum_fd_count,
        })
    }

    pub(crate) const fn new_unchecked(maximum_payload: u32, maximum_fd_count: u16) -> Self {
        Self {
            maximum_payload,
            maximum_fd_count,
        }
    }
}

impl Default for TransportLimits {
    fn default() -> Self {
        Self::DEFAULT
    }
}

#[derive(Debug)]
pub struct ReceivedRecord {
    pub envelope: Envelope,
    pub payload: Vec<u8>,
    pub fds: Vec<OwnedFd>,
}

#[derive(Debug)]
pub enum TransportError {
    Io(io::Error),
    InvalidLimits,
    Disconnected,
    RecordTruncated,
    AncillaryTruncated,
    InvalidAncillaryData,
    DescriptorLimitExceeded,
    MalformedEnvelope(EnvelopeDecodeError),
    ShortWrite,
}

impl fmt::Display for TransportError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "GWIPC transport I/O failed: {error}"),
            Self::InvalidLimits => formatter.write_str("GWIPC transport limits are invalid"),
            Self::Disconnected => formatter.write_str("GWIPC peer disconnected"),
            Self::RecordTruncated => formatter.write_str("GWIPC record was truncated"),
            Self::AncillaryTruncated => {
                formatter.write_str("GWIPC ancillary descriptor data was truncated")
            }
            Self::InvalidAncillaryData => {
                formatter.write_str("GWIPC record contains invalid ancillary data")
            }
            Self::DescriptorLimitExceeded => {
                formatter.write_str("GWIPC record exceeds the negotiated descriptor limit")
            }
            Self::MalformedEnvelope(error) => write!(formatter, "{error}"),
            Self::ShortWrite => formatter.write_str("GWIPC record was not sent atomically"),
        }
    }
}

impl std::error::Error for TransportError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::MalformedEnvelope(error) => Some(error),
            _ => None,
        }
    }
}

impl From<io::Error> for TransportError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<EnvelopeDecodeError> for TransportError {
    fn from(error: EnvelopeDecodeError) -> Self {
        Self::MalformedEnvelope(error)
    }
}

#[derive(Debug)]
pub struct Transport {
    fd: OwnedFd,
    limits: TransportLimits,
}

impl Transport {
    pub fn pair(limits: TransportLimits) -> Result<(Self, Self), TransportError> {
        // Revalidate callers that constructed the public value directly.
        let limits = TransportLimits::new(limits.maximum_payload, limits.maximum_fd_count)?;
        let mut sockets = [-1; 2];
        // SAFETY: `sockets` points at space for exactly two `c_int` values. On
        // success each returned descriptor is uniquely owned by this function.
        let result = unsafe {
            socketpair(
                AF_UNIX,
                SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                0,
                sockets.as_mut_ptr(),
            )
        };
        if result != 0 {
            return Err(io::Error::last_os_error().into());
        }
        // SAFETY: successful `socketpair` returned two fresh, valid descriptors
        // and ownership has not been transferred anywhere else.
        let first = unsafe { OwnedFd::from_raw_fd(sockets[0]) };
        // SAFETY: same ownership argument as `first`, for the other descriptor.
        let second = unsafe { OwnedFd::from_raw_fd(sockets[1]) };
        Ok((Self { fd: first, limits }, Self { fd: second, limits }))
    }

    /// Adopts an already connected `SOCK_SEQPACKET` descriptor.
    ///
    /// The caller retains responsibility for creating the descriptor with the
    /// nonblocking and close-on-exec flags required by GWIPC.
    pub fn from_owned_fd(fd: OwnedFd, limits: TransportLimits) -> Result<Self, TransportError> {
        let limits = TransportLimits::new(limits.maximum_payload, limits.maximum_fd_count)?;
        Ok(Self { fd, limits })
    }

    #[must_use]
    pub const fn limits(&self) -> TransportLimits {
        self.limits
    }

    pub fn set_limits(&mut self, limits: TransportLimits) -> Result<(), TransportError> {
        self.limits = TransportLimits::new(limits.maximum_payload, limits.maximum_fd_count)?;
        Ok(())
    }

    pub fn send(
        &self,
        envelope: &Envelope,
        payload: &[u8],
        fds: &[BorrowedFd<'_>],
    ) -> Result<(), TransportError> {
        if fds.len() > usize::from(self.limits.maximum_fd_count) {
            return Err(TransportError::DescriptorLimitExceeded);
        }
        let mut bytes = encode_envelope(envelope).to_vec();
        bytes.extend_from_slice(payload);
        // Apply the same validation on outbound records as inbound records. It
        // catches mismatched payload/descriptor counts before a peer sees them.
        decode_envelope(
            &bytes,
            fds.len(),
            DecodeLimits::new(self.limits.maximum_payload),
        )?;
        send_raw(self.fd.as_raw_fd(), &bytes, fds)
    }

    pub fn receive(&self) -> Result<ReceivedRecord, TransportError> {
        let capacity = GWIPC_ENVELOPE_SIZE + self.limits.maximum_payload as usize;
        let mut bytes = vec![0_u8; capacity];
        let mut control = vec![0_u8; cmsg_space(size_of::<RawFd>() * HARD_MAXIMUM_FDS as usize)];
        let mut vector = Iovec {
            iov_base: bytes.as_mut_ptr().cast(),
            iov_len: bytes.len(),
        };
        let mut message = MessageHeader {
            msg_name: ptr::null_mut(),
            msg_namelen: 0,
            msg_iov: &mut vector,
            msg_iovlen: 1,
            msg_control: control.as_mut_ptr().cast(),
            msg_controllen: control.len(),
            msg_flags: 0,
        };

        // SAFETY: every pointer in `message` refers to writable storage that
        // remains alive for the call. `recvmsg` receives no aliases to Rust
        // references and cannot write beyond the supplied lengths.
        let received = unsafe {
            recvmsg(
                self.fd.as_raw_fd(),
                &mut message,
                MSG_DONTWAIT | MSG_CMSG_CLOEXEC | MSG_TRUNC,
            )
        };
        if received < 0 {
            return Err(io::Error::last_os_error().into());
        }
        if received == 0 {
            return Err(TransportError::Disconnected);
        }

        let control_length = message.msg_controllen.min(control.len());
        let fds = parse_control(&control[..control_length])?;
        if message.msg_flags & MSG_CTRUNC != 0 {
            return Err(TransportError::AncillaryTruncated);
        }
        if fds.len() > usize::from(self.limits.maximum_fd_count) {
            return Err(TransportError::DescriptorLimitExceeded);
        }
        if message.msg_flags & MSG_TRUNC != 0 || received as usize > bytes.len() {
            return Err(TransportError::RecordTruncated);
        }
        bytes.truncate(received as usize);
        let envelope = decode_envelope(
            &bytes,
            fds.len(),
            DecodeLimits::new(self.limits.maximum_payload),
        )?;
        let payload = bytes.split_off(GWIPC_ENVELOPE_SIZE);
        Ok(ReceivedRecord {
            envelope,
            payload,
            fds,
        })
    }
}

impl AsFd for Transport {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

fn send_raw(fd: RawFd, bytes: &[u8], fds: &[BorrowedFd<'_>]) -> Result<(), TransportError> {
    let mut vector = Iovec {
        iov_base: bytes.as_ptr().cast_mut().cast(),
        iov_len: bytes.len(),
    };
    let mut control = if fds.is_empty() {
        Vec::new()
    } else {
        let data_length = size_of_val(fds);
        let mut storage = vec![0_u8; cmsg_space(data_length)];
        let header = ControlHeader {
            cmsg_len: cmsg_len(data_length),
            cmsg_level: SOL_SOCKET,
            cmsg_type: SCM_RIGHTS,
        };
        // SAFETY: the aligned allocation has enough room for the header and FD
        // payload. Both writes stay within `storage`, and the kernel consumes
        // the bytes only for the duration of `sendmsg` below.
        unsafe {
            ptr::write_unaligned(storage.as_mut_ptr().cast::<ControlHeader>(), header);
            let data = storage
                .as_mut_ptr()
                .add(cmsg_align(size_of::<ControlHeader>()));
            for (index, descriptor) in fds.iter().enumerate() {
                ptr::write_unaligned(
                    data.add(index * size_of::<RawFd>()).cast::<RawFd>(),
                    descriptor.as_raw_fd(),
                );
            }
        }
        storage
    };
    let message = MessageHeader {
        msg_name: ptr::null_mut(),
        msg_namelen: 0,
        msg_iov: &mut vector,
        msg_iovlen: 1,
        msg_control: if control.is_empty() {
            ptr::null_mut()
        } else {
            control.as_mut_ptr().cast()
        },
        msg_controllen: control.len(),
        msg_flags: 0,
    };
    // SAFETY: `message` points only at initialized readable buffers that remain
    // alive for the call; borrowed descriptors cannot be closed by `sendmsg`.
    let sent = unsafe { sendmsg(fd, &message, MSG_DONTWAIT | MSG_NOSIGNAL) };
    if sent < 0 {
        return Err(io::Error::last_os_error().into());
    }
    if sent as usize != bytes.len() {
        return Err(TransportError::ShortWrite);
    }
    Ok(())
}

fn parse_control(control: &[u8]) -> Result<Vec<OwnedFd>, TransportError> {
    let mut descriptors = Vec::new();
    let mut offset = 0_usize;
    while offset < control.len() {
        if control.len() - offset < size_of::<ControlHeader>() {
            return Err(TransportError::InvalidAncillaryData);
        }
        // SAFETY: the preceding bound check guarantees a full header. Unaligned
        // access is intentional because the kernel-provided byte buffer has no
        // Rust `ControlHeader` alignment guarantee.
        let header =
            unsafe { ptr::read_unaligned(control.as_ptr().add(offset).cast::<ControlHeader>()) };
        let minimum = cmsg_align(size_of::<ControlHeader>());
        if header.cmsg_len < minimum || header.cmsg_len > control.len() - offset {
            return Err(TransportError::InvalidAncillaryData);
        }
        if header.cmsg_level != SOL_SOCKET || header.cmsg_type != SCM_RIGHTS {
            return Err(TransportError::InvalidAncillaryData);
        }
        let data_length = header.cmsg_len - minimum;
        if !data_length.is_multiple_of(size_of::<RawFd>()) {
            return Err(TransportError::InvalidAncillaryData);
        }
        let data_offset = offset + minimum;
        for index in 0..(data_length / size_of::<RawFd>()) {
            // SAFETY: `cmsg_len` and divisibility checks keep this read inside
            // the current control message. Each received raw descriptor is a
            // fresh process-local descriptor transferred by `SCM_RIGHTS`.
            let raw = unsafe {
                ptr::read_unaligned(
                    control
                        .as_ptr()
                        .add(data_offset + index * size_of::<RawFd>())
                        .cast::<RawFd>(),
                )
            };
            if raw < 0 {
                return Err(TransportError::InvalidAncillaryData);
            }
            // SAFETY: `SCM_RIGHTS` created this descriptor for the receiving
            // process and no other Rust owner has been constructed for it.
            descriptors.push(unsafe { OwnedFd::from_raw_fd(raw) });
        }
        let next = offset
            .checked_add(cmsg_align(header.cmsg_len))
            .ok_or(TransportError::InvalidAncillaryData)?;
        if next <= offset {
            return Err(TransportError::InvalidAncillaryData);
        }
        offset = next.min(control.len());
    }
    Ok(descriptors)
}

const fn cmsg_align(length: usize) -> usize {
    let alignment = align_of::<usize>();
    (length + alignment - 1) & !(alignment - 1)
}

const fn cmsg_len(data_length: usize) -> usize {
    cmsg_align(size_of::<ControlHeader>()) + data_length
}

const fn cmsg_space(data_length: usize) -> usize {
    cmsg_align(size_of::<ControlHeader>()) + cmsg_align(data_length)
}

#[cfg(test)]
mod tests {
    use super::*;
    use gw_types::{MessageType, Sequence};
    use std::fs::File;
    use std::io::Read;

    fn receive_blocking(transport: &Transport) -> Result<ReceivedRecord, TransportError> {
        for _ in 0..1000 {
            match transport.receive() {
                Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => {
                    std::thread::yield_now();
                }
                result => return result,
            }
        }
        panic!("record did not arrive");
    }

    #[test]
    fn record_and_owned_descriptor_round_trip() {
        let (sender, receiver) = Transport::pair(TransportLimits::default()).unwrap();
        let file = File::open("/dev/zero").unwrap();
        let mut envelope = Envelope::request(MessageType::BUFFER_ATTACH, Sequence::new(1), 3);
        envelope.fd_count = 1;
        sender.send(&envelope, b"gw!", &[file.as_fd()]).unwrap();

        let mut record = receive_blocking(&receiver).unwrap();
        assert_eq!(record.envelope, envelope);
        assert_eq!(record.payload, b"gw!");
        assert_eq!(record.fds.len(), 1);
        let mut received_file = File::from(record.fds.pop().unwrap());
        let mut byte = [1_u8; 1];
        received_file.read_exact(&mut byte).unwrap();
        assert_eq!(byte, [0]);
    }

    #[test]
    fn outbound_shape_is_validated_before_send() {
        let (sender, _receiver) = Transport::pair(TransportLimits::default()).unwrap();
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 1);
        assert!(matches!(
            sender.send(&envelope, &[], &[]),
            Err(TransportError::MalformedEnvelope(
                EnvelopeDecodeError::SizeMismatch
            ))
        ));
    }

    #[test]
    fn oversized_record_is_rejected_without_partial_delivery() {
        let sender_limits = TransportLimits::new(64, 0).unwrap();
        let receiver_limits = TransportLimits::new(8, 0).unwrap();
        let (sender, receiver) = Transport::pair(sender_limits).unwrap();
        let receiver = Transport::from_owned_fd(receiver.fd, receiver_limits).unwrap();
        let envelope = Envelope::request(MessageType::PING, Sequence::new(1), 16);
        sender.send(&envelope, &[0; 16], &[]).unwrap();
        assert!(matches!(
            receive_blocking(&receiver),
            Err(TransportError::RecordTruncated)
        ));
    }

    #[test]
    fn disconnect_is_explicit() {
        let (peer, receiver) = Transport::pair(TransportLimits::default()).unwrap();
        drop(peer);
        assert!(matches!(
            receive_blocking(&receiver),
            Err(TransportError::Disconnected)
        ));
    }

    #[test]
    fn limit_constructor_rejects_hard_limit_violations() {
        assert!(matches!(
            TransportLimits::new(0, 0),
            Err(TransportError::InvalidLimits)
        ));
        assert!(matches!(
            TransportLimits::new(HARD_MAXIMUM_PAYLOAD + 1, 0),
            Err(TransportError::InvalidLimits)
        ));
        assert!(matches!(
            TransportLimits::new(1, HARD_MAXIMUM_FDS + 1),
            Err(TransportError::InvalidLimits)
        ));
    }
}
