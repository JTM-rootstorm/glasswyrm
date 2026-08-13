use core::ffi::{c_char, c_int, c_void};
use std::io;
use std::mem::size_of;
use std::os::fd::{FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

const AF_UNIX: c_int = 1;
const SOCK_SEQPACKET: c_int = 5;
const SOCK_CLOEXEC: c_int = 0o2_000_000;
const F_GETFL: c_int = 3;
const F_SETFL: c_int = 4;
const O_NONBLOCK: c_int = 0o4_000;

#[repr(C)]
struct SockAddrUn {
    family: u16,
    path: [c_char; 108],
}

unsafe extern "C" {
    fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    fn connect(socket: c_int, address: *const c_void, length: u32) -> c_int;
    fn fcntl(descriptor: c_int, command: c_int, ...) -> c_int;
    fn close(descriptor: c_int) -> c_int;
}

pub fn connect_seqpacket(path: &Path) -> io::Result<OwnedFd> {
    let path_bytes = path.as_os_str().as_bytes();
    if path_bytes.is_empty() || path_bytes.len() >= 108 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Unix socket path is empty or exceeds sockaddr_un",
        ));
    }
    // SAFETY: socket has no pointer arguments and returns a fresh descriptor on success.
    let descriptor = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0) };
    if descriptor < 0 {
        return Err(io::Error::last_os_error());
    }
    let mut address = SockAddrUn {
        family: AF_UNIX as u16,
        path: [0; 108],
    };
    for (destination, source) in address.path.iter_mut().zip(path_bytes) {
        *destination = *source as c_char;
    }
    let address_length = u32::try_from(size_of::<u16>() + path_bytes.len() + 1)
        .expect("sockaddr_un length fits in u32");
    // SAFETY: address points to an initialized sockaddr_un and length includes its NUL.
    if unsafe { connect(descriptor, (&raw const address).cast(), address_length) } != 0 {
        let error = io::Error::last_os_error();
        // SAFETY: descriptor is owned here and has not been transferred.
        unsafe { close(descriptor) };
        return Err(error);
    }
    // SAFETY: fcntl is called with a valid descriptor and the documented integer command.
    let flags = unsafe { fcntl(descriptor, F_GETFL) };
    // SAFETY: as above; F_SETFL consumes the additional integer flags argument.
    let nonblocking = if flags < 0 {
        -1
    } else {
        // SAFETY: F_SETFL consumes the additional integer flags argument.
        unsafe { fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) }
    };
    if flags < 0 || nonblocking != 0 {
        let error = io::Error::last_os_error();
        // SAFETY: descriptor is owned here and has not been transferred.
        unsafe { close(descriptor) };
        return Err(error);
    }
    // SAFETY: descriptor is valid, uniquely owned, and close-on-exec.
    Ok(unsafe { OwnedFd::from_raw_fd(descriptor) })
}
