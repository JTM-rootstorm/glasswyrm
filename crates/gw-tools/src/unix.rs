use core::ffi::{c_char, c_int, c_short, c_void};
use core::mem::size_of;
use std::ffi::OsStr;
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd, RawFd};
use std::os::unix::ffi::OsStrExt;
use std::time::{Duration, Instant};

const AF_UNIX: c_int = 1;
const SOCK_SEQPACKET: c_int = 5;
const SOCK_CLOEXEC: c_int = 0o2_000_000;
const SOCK_NONBLOCK: c_int = 0o4_000;
const SOL_SOCKET: c_int = 1;
const SO_ERROR: c_int = 4;
const EINPROGRESS: i32 = 115;
const POLLIN: c_short = 0x001;
const POLLOUT: c_short = 0x004;

#[repr(C)]
struct SocketAddressUnix {
    family: u16,
    path: [c_char; 108],
}

#[repr(C)]
struct PollDescriptor {
    fd: c_int,
    events: c_short,
    revents: c_short,
}

unsafe extern "C" {
    fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    fn connect(fd: c_int, address: *const c_void, length: u32) -> c_int;
    fn getsockopt(
        fd: c_int,
        level: c_int,
        option: c_int,
        value: *mut c_void,
        length: *mut u32,
    ) -> c_int;
    fn poll(descriptors: *mut PollDescriptor, count: usize, timeout: c_int) -> c_int;
}

pub(crate) fn connect_seqpacket(path: &OsStr, deadline: Instant) -> io::Result<OwnedFd> {
    let bytes = path.as_bytes();
    if bytes.is_empty() || bytes.len() >= 108 || bytes.contains(&0) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Unix socket path is invalid or too long",
        ));
    }
    // SAFETY: `socket` takes no borrowed pointers and returns a new descriptor
    // on success. The descriptor is immediately wrapped in `OwnedFd`.
    let raw = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0) };
    if raw < 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: a nonnegative `socket` result is a fresh descriptor owned here.
    let fd = unsafe { OwnedFd::from_raw_fd(raw) };
    let mut address = SocketAddressUnix {
        family: AF_UNIX as u16,
        path: [0; 108],
    };
    for (target, source) in address.path.iter_mut().zip(bytes) {
        *target = *source as c_char;
    }
    let length =
        u32::try_from(size_of::<u16>() + bytes.len() + 1).expect("sockaddr_un length fits u32");
    // SAFETY: `address` is a Linux `sockaddr_un`; `length` includes the family,
    // pathname, and trailing NUL and cannot exceed the initialized object.
    let status = unsafe {
        connect(
            fd.as_raw_fd(),
            (&raw const address).cast::<c_void>(),
            length,
        )
    };
    if status == 0 {
        return Ok(fd);
    }
    let error = io::Error::last_os_error();
    if error.raw_os_error() != Some(EINPROGRESS) {
        return Err(error);
    }
    wait_writable(fd.as_raw_fd(), deadline)?;
    let mut socket_error: c_int = 0;
    let mut socket_error_size = size_of::<c_int>() as u32;
    // SAFETY: the output pointers refer to writable storage of the advertised
    // size for the duration of `getsockopt`.
    if unsafe {
        getsockopt(
            fd.as_raw_fd(),
            SOL_SOCKET,
            SO_ERROR,
            (&raw mut socket_error).cast::<c_void>(),
            &raw mut socket_error_size,
        )
    } != 0
    {
        return Err(io::Error::last_os_error());
    }
    if socket_error != 0 {
        return Err(io::Error::from_raw_os_error(socket_error));
    }
    Ok(fd)
}

pub(crate) fn wait_readable(fd: RawFd, deadline: Instant) -> io::Result<()> {
    wait(fd, POLLIN, deadline)
}

pub(crate) fn wait_writable(fd: RawFd, deadline: Instant) -> io::Result<()> {
    wait(fd, POLLOUT, deadline)
}

fn wait(fd: RawFd, events: c_short, deadline: Instant) -> io::Result<()> {
    loop {
        let timeout = deadline.saturating_duration_since(Instant::now());
        if timeout.is_zero() {
            return Err(io::Error::new(io::ErrorKind::TimedOut, "deadline expired"));
        }
        let milliseconds = timeout.as_millis().max(1).min(c_int::MAX as u128) as c_int;
        let mut descriptor = PollDescriptor {
            fd,
            events,
            revents: 0,
        };
        // SAFETY: the pointer refers to one initialized poll descriptor for the
        // whole call, and `count` accurately describes that storage.
        let result = unsafe { poll(&raw mut descriptor, 1, milliseconds) };
        if result > 0 {
            return Ok(());
        }
        if result == 0 {
            return Err(io::Error::new(io::ErrorKind::TimedOut, "deadline expired"));
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

pub(crate) fn pause(duration: Duration, deadline: Instant) {
    std::thread::sleep(duration.min(deadline.saturating_duration_since(Instant::now())));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_paths_are_rejected_before_socket_io() {
        assert_eq!(
            connect_seqpacket(OsStr::new(""), Instant::now() + Duration::from_secs(1))
                .unwrap_err()
                .kind(),
            io::ErrorKind::InvalidInput
        );
        let oversized = "x".repeat(108);
        assert_eq!(
            connect_seqpacket(
                OsStr::new(&oversized),
                Instant::now() + Duration::from_secs(1)
            )
            .unwrap_err()
            .kind(),
            io::ErrorKind::InvalidInput
        );
    }
}
