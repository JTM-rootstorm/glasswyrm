//! Owned local GWIPC listener endpoints.
//!
//! Listener paths are private, same-user `SOCK_SEQPACKET` sockets. Stale
//! endpoints are removed only after a liveness probe and identity recheck, and
//! dropping a listener unlinks only the exact socket inode it created.

use core::ffi::{c_char, c_int, c_void};
use core::mem::size_of;
use std::fs;
use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{FileTypeExt, MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};

const AF_UNIX: c_int = 1;
const SOCK_SEQPACKET: c_int = 5;
const SOCK_CLOEXEC: c_int = 0o2_000_000;
const SOCK_NONBLOCK: c_int = 0o4_000;
const SOL_SOCKET: c_int = 1;
const SO_PEERCRED: c_int = 17;
const ENOENT: i32 = 2;
const EAGAIN: i32 = 11;
const ECONNREFUSED: i32 = 111;
const EINTR: i32 = 4;
const EALREADY: i32 = 114;
const EINPROGRESS: i32 = 115;

#[repr(C)]
struct SockAddrUnix {
    family: u16,
    path: [c_char; 108],
}

#[repr(C)]
struct Credentials {
    pid: i32,
    uid: u32,
    gid: u32,
}

unsafe extern "C" {
    fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    #[link_name = "bind"]
    fn bind_socket(socket: c_int, address: *const c_void, length: u32) -> c_int;
    fn connect(socket: c_int, address: *const c_void, length: u32) -> c_int;
    fn listen(socket: c_int, backlog: c_int) -> c_int;
    fn accept4(socket: c_int, address: *mut c_void, length: *mut u32, flags: c_int) -> c_int;
    fn getsockopt(
        socket: c_int,
        level: c_int,
        option: c_int,
        value: *mut c_void,
        length: *mut u32,
    ) -> c_int;
    fn geteuid() -> u32;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct EndpointIdentity {
    device: u64,
    inode: u64,
}

impl EndpointIdentity {
    fn from_metadata(metadata: &fs::Metadata) -> Self {
        Self {
            device: metadata.dev(),
            inode: metadata.ino(),
        }
    }

    fn matches(self, metadata: &fs::Metadata) -> bool {
        metadata.file_type().is_socket()
            && metadata.dev() == self.device
            && metadata.ino() == self.inode
    }
}

/// An owned, nonblocking Unix `SOCK_SEQPACKET` listener.
#[derive(Debug)]
pub struct EndpointListener {
    fd: OwnedFd,
    path: PathBuf,
    identity: EndpointIdentity,
}

impl EndpointListener {
    /// Binds a private same-user endpoint, safely replacing only a stale socket.
    pub fn bind(path: &Path) -> io::Result<Self> {
        let (address, length) = make_address(path)?;
        let parent = path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        if !fs::symlink_metadata(parent)?.file_type().is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC endpoint parent is not a directory",
            ));
        }
        prepare_path(path, &address, length)?;

        // SAFETY: arguments select a local, nonblocking, close-on-exec
        // SOCK_SEQPACKET socket. The fresh descriptor is checked below.
        let raw = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: successful `socket` returned a fresh descriptor owned here.
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        // SAFETY: `address` remains initialized and live for `length` bytes.
        if unsafe {
            bind_socket(
                fd.as_raw_fd(),
                (&raw const address).cast::<c_void>(),
                length,
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }

        let metadata = fs::symlink_metadata(path)?;
        // SAFETY: `geteuid` has no preconditions.
        let effective_uid = unsafe { geteuid() };
        if !metadata.file_type().is_socket() || metadata.uid() != effective_uid {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "bound GWIPC endpoint is not a same-euid socket",
            ));
        }
        let identity = EndpointIdentity::from_metadata(&metadata);
        let cleanup = || unlink_if_owned(path, identity);
        if let Err(error) = fs::set_permissions(path, fs::Permissions::from_mode(0o600)) {
            cleanup();
            return Err(error);
        }
        // SAFETY: `fd` is a valid listening-capable local socket.
        if unsafe { listen(fd.as_raw_fd(), 32) } != 0 {
            let error = io::Error::last_os_error();
            cleanup();
            return Err(error);
        }
        let verified = fs::symlink_metadata(path)?;
        if !identity.matches(&verified) || verified.uid() != effective_uid {
            cleanup();
            return Err(io::Error::from_raw_os_error(116)); // ESTALE
        }
        Ok(Self {
            fd,
            path: path.to_owned(),
            identity,
        })
    }

    /// Accepts the next same-euid peer, or returns `None` when none is ready.
    pub fn accept(&self) -> io::Result<Option<OwnedFd>> {
        loop {
            // SAFETY: null address pointers explicitly discard peer addressing.
            // A successful call returns a fresh descriptor.
            let raw = unsafe {
                accept4(
                    self.fd.as_raw_fd(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    SOCK_NONBLOCK | SOCK_CLOEXEC,
                )
            };
            if raw < 0 {
                let error = io::Error::last_os_error();
                return if matches!(error.raw_os_error(), Some(EAGAIN) | Some(EINTR)) {
                    Ok(None)
                } else {
                    Err(error)
                };
            }
            // SAFETY: successful `accept4` returned a fresh descriptor.
            let peer = unsafe { OwnedFd::from_raw_fd(raw) };
            if peer_is_same_euid(peer.as_raw_fd())? {
                return Ok(Some(peer));
            }
            // Drop and continue so an unauthorized queued peer cannot prevent
            // a same-user peer behind it from being serviced.
        }
    }
}

impl Drop for EndpointListener {
    fn drop(&mut self) {
        unlink_if_owned(&self.path, self.identity);
    }
}

fn make_address(path: &Path) -> io::Result<(SockAddrUnix, u32)> {
    let bytes = path.as_os_str().as_bytes();
    if bytes.is_empty() || bytes.len() >= 108 || bytes.contains(&0) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "GWIPC socket path is empty, too long, or contains NUL",
        ));
    }
    let mut address = SockAddrUnix {
        family: AF_UNIX as u16,
        path: [0; 108],
    };
    for (destination, source) in address.path.iter_mut().zip(bytes) {
        *destination = *source as c_char;
    }
    let length = u32::try_from(size_of::<u16>() + bytes.len() + 1)
        .expect("Unix socket address length fits u32");
    Ok((address, length))
}

fn prepare_path(path: &Path, address: &SockAddrUnix, length: u32) -> io::Result<()> {
    let metadata = match fs::symlink_metadata(path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error),
    };
    // SAFETY: `geteuid` has no preconditions.
    let effective_uid = unsafe { geteuid() };
    if !metadata.file_type().is_socket() || metadata.uid() != effective_uid {
        return Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            "GWIPC endpoint path is not a same-euid socket",
        ));
    }
    if endpoint_is_live(address, length)? {
        return Err(io::Error::new(
            io::ErrorKind::AddrInUse,
            "GWIPC endpoint is already live",
        ));
    }
    let identity = EndpointIdentity::from_metadata(&metadata);
    let unchanged = fs::symlink_metadata(path)?;
    if !identity.matches(&unchanged) || unchanged.uid() != effective_uid {
        return Err(io::Error::from_raw_os_error(116)); // ESTALE
    }
    fs::remove_file(path)
}

fn endpoint_is_live(address: &SockAddrUnix, length: u32) -> io::Result<bool> {
    // SAFETY: arguments select a temporary local probe socket.
    let raw = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) };
    if raw < 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful `socket` returned a fresh descriptor owned here.
    let fd = unsafe { OwnedFd::from_raw_fd(raw) };
    // SAFETY: `address` remains initialized and live for `length` bytes.
    let result = unsafe {
        connect(
            fd.as_raw_fd(),
            (address as *const SockAddrUnix).cast::<c_void>(),
            length,
        )
    };
    if result == 0 {
        return Ok(true);
    }
    let error = io::Error::last_os_error();
    match error.raw_os_error() {
        Some(EINPROGRESS) | Some(EAGAIN) | Some(EALREADY) => Ok(true),
        Some(ECONNREFUSED) | Some(ENOENT) => Ok(false),
        _ => Err(error),
    }
}

fn peer_is_same_euid(fd: c_int) -> io::Result<bool> {
    let mut credentials = Credentials {
        pid: 0,
        uid: 0,
        gid: 0,
    };
    let mut length = u32::try_from(size_of::<Credentials>()).expect("credential size fits u32");
    // SAFETY: `credentials` is writable for exactly `length` bytes, and `fd`
    // is an accepted Unix socket descriptor.
    if unsafe {
        getsockopt(
            fd,
            SOL_SOCKET,
            SO_PEERCRED,
            (&raw mut credentials).cast::<c_void>(),
            &raw mut length,
        )
    } != 0
        || length as usize != size_of::<Credentials>()
    {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: `geteuid` has no preconditions.
    Ok(credentials.uid == unsafe { geteuid() })
}

fn unlink_if_owned(path: &Path, identity: EndpointIdentity) {
    if fs::symlink_metadata(path).is_ok_and(|metadata| identity.matches(&metadata)) {
        let _ = fs::remove_file(path);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_PATH: AtomicU64 = AtomicU64::new(1);

    fn path(label: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "gw-ipc-endpoint-{}-{label}-{}",
            std::process::id(),
            NEXT_PATH.fetch_add(1, Ordering::Relaxed)
        ))
    }

    #[test]
    fn second_listener_cannot_replace_a_live_endpoint() {
        let path = path("live");
        let first = EndpointListener::bind(&path).unwrap();
        let error = EndpointListener::bind(&path).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::AddrInUse);
        assert!(fs::symlink_metadata(&path).unwrap().file_type().is_socket());
        drop(first);
        assert!(!path.exists());
    }

    #[test]
    fn dropping_listener_does_not_unlink_replacement_pathname() {
        let path = path("replacement");
        let displaced = path.with_extension("displaced");
        let first = EndpointListener::bind(&path).unwrap();
        fs::rename(&path, &displaced).unwrap();
        let replacement = EndpointListener::bind(&path).unwrap();
        let replacement_identity =
            EndpointIdentity::from_metadata(&fs::symlink_metadata(&path).unwrap());

        drop(first);
        assert!(replacement_identity.matches(&fs::symlink_metadata(&path).unwrap()));
        drop(replacement);
        assert!(!path.exists());
        fs::remove_file(displaced).unwrap();
    }
}
