use core::ffi::{c_char, c_int, c_void};
use core::mem::size_of;
use std::fs;
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{FileTypeExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

const AF_UNIX: c_int = 1;
const SOCK_SEQPACKET: c_int = 5;
const SOCK_CLOEXEC: c_int = 0o2_000_000;
const SOCK_NONBLOCK: c_int = 0o4_000;
const EAGAIN: i32 = 11;
const EINTR: i32 = 4;
const SIGINT: c_int = 2;
const SIGTERM: c_int = 15;

static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

#[repr(C)]
struct SockAddrUnix {
    family: u16,
    path: [c_char; 108],
}

unsafe extern "C" {
    fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    fn bind(socket: c_int, address: *const c_void, length: u32) -> c_int;
    fn listen(socket: c_int, backlog: c_int) -> c_int;
    fn accept4(socket: c_int, address: *mut c_void, length: *mut u32, flags: c_int) -> c_int;
    fn signal(number: c_int, handler: usize) -> usize;
}

extern "C" fn stop_handler(_signal: c_int) {
    STOP_REQUESTED.store(true, Ordering::Relaxed);
}

pub fn install_signal_handlers() -> io::Result<()> {
    STOP_REQUESTED.store(false, Ordering::Relaxed);
    // SAFETY: `stop_handler` has the required C signal-handler ABI and only
    // performs a lock-free atomic store. SIGINT and SIGTERM are valid signals.
    let (interrupt, terminate) = unsafe {
        (
            signal(SIGINT, stop_handler as *const () as usize),
            signal(SIGTERM, stop_handler as *const () as usize),
        )
    };
    if interrupt == usize::MAX || terminate == usize::MAX {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

pub fn stop_requested() -> bool {
    STOP_REQUESTED.load(Ordering::Relaxed)
}

pub struct Listener {
    fd: OwnedFd,
    path: PathBuf,
}

impl Listener {
    pub fn bind(path: &Path) -> io::Result<Self> {
        let bytes = path.as_os_str().as_bytes();
        if bytes.is_empty() || bytes.len() >= 108 || bytes.contains(&0) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC socket path is empty, too long, or contains NUL",
            ));
        }
        match fs::symlink_metadata(path) {
            Ok(metadata) if metadata.file_type().is_socket() => fs::remove_file(path)?,
            Ok(_) => {
                return Err(io::Error::new(
                    io::ErrorKind::AlreadyExists,
                    "GWIPC path exists and is not a socket",
                ));
            }
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            Err(error) => return Err(error),
        }
        // SAFETY: the arguments select a local nonblocking close-on-exec
        // SOCK_SEQPACKET endpoint. The returned descriptor is checked below.
        let raw = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: successful `socket` returned a fresh descriptor owned here.
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        let mut address = SockAddrUnix {
            family: AF_UNIX as u16,
            path: [0; 108],
        };
        for (destination, source) in address.path.iter_mut().zip(bytes) {
            *destination = *source as c_char;
        }
        let length = u32::try_from(size_of::<u16>() + bytes.len() + 1)
            .expect("Unix socket address length fits u32");
        // SAFETY: `address` is initialized for `length` bytes and remains live
        // through the call. `fd` is a valid local socket.
        if unsafe {
            bind(
                fd.as_raw_fd(),
                (&raw const address).cast::<c_void>(),
                length,
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }
        fs::set_permissions(path, fs::Permissions::from_mode(0o600))?;
        // SAFETY: `fd` remains valid and the positive backlog is conventional.
        if unsafe { listen(fd.as_raw_fd(), 16) } != 0 {
            let error = io::Error::last_os_error();
            let _ = fs::remove_file(path);
            return Err(error);
        }
        Ok(Self {
            fd,
            path: path.to_owned(),
        })
    }

    pub fn accept(&self) -> io::Result<Option<OwnedFd>> {
        // SAFETY: null address pointers explicitly discard peer addressing.
        // On success accept4 returns a fresh descriptor owned by the caller.
        let raw = unsafe {
            accept4(
                self.fd.as_raw_fd(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                SOCK_NONBLOCK | SOCK_CLOEXEC,
            )
        };
        if raw >= 0 {
            // SAFETY: successful `accept4` returned a fresh descriptor.
            return Ok(Some(unsafe { OwnedFd::from_raw_fd(raw) }));
        }
        let error = io::Error::last_os_error();
        if matches!(error.raw_os_error(), Some(EAGAIN) | Some(EINTR)) {
            Ok(None)
        } else {
            Err(error)
        }
    }
}

impl Drop for Listener {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}
