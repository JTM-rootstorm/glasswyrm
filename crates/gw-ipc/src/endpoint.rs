//! Owned local GWIPC listener endpoints.
//!
//! Listener paths are private, same-user `SOCK_SEQPACKET` sockets. Stale
//! endpoints are removed only after a liveness probe and identity recheck, and
//! filesystem operations remain anchored to a validated parent directory.
//! Different-EUID processes cannot redirect cleanup through writable ancestors;
//! same-EUID processes remain inside the local trust boundary.

use core::ffi::{c_char, c_int, c_void};
use core::mem::size_of;
use std::ffi::{CString, OsStr};
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path, PathBuf};

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
const ESTALE: i32 = 116;
const AT_FDCWD: c_int = -100;
const AT_SYMLINK_NOFOLLOW: c_int = 0x100;
const O_CLOEXEC: c_int = 0o2_000_000;
const O_DIRECTORY: c_int = 0o200_000;
const O_NOFOLLOW: c_int = 0o400_000;
const O_PATH: c_int = 0o10_000_000;
const S_IFDIR: u32 = 0o040_000;
const S_IFMT: u32 = 0o170_000;
const S_IFSOCK: u32 = 0o140_000;
const WRITE_BY_GROUP_OR_OTHER: u32 = 0o022;
const STICKY: u32 = 0o1000;

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
    fn openat(directory: c_int, path: *const c_char, flags: c_int, ...) -> c_int;
    fn fstatat(
        directory: c_int,
        path: *const c_char,
        status: *mut gw_sys::stat,
        flags: c_int,
    ) -> c_int;
    fn fchmodat(directory: c_int, path: *const c_char, mode: u32, flags: c_int) -> c_int;
    fn unlinkat(directory: c_int, path: *const c_char, flags: c_int) -> c_int;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct EndpointIdentity {
    device: u64,
    inode: u64,
}

impl EndpointIdentity {
    fn from_status(status: &gw_sys::stat) -> Self {
        Self {
            device: status.st_dev,
            inode: status.st_ino,
        }
    }

    fn matches(self, status: &gw_sys::stat) -> bool {
        status.st_mode & S_IFMT == S_IFSOCK
            && status.st_dev == self.device
            && status.st_ino == self.inode
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct DirectoryIdentity {
    device: u64,
    inode: u64,
}

impl DirectoryIdentity {
    fn from_status(status: &gw_sys::stat) -> Self {
        Self {
            device: status.st_dev,
            inode: status.st_ino,
        }
    }

    fn matches(self, status: &gw_sys::stat) -> bool {
        status.st_mode & S_IFMT == S_IFDIR
            && status.st_dev == self.device
            && status.st_ino == self.inode
    }
}

#[derive(Debug)]
struct EndpointPath {
    parent: OwnedFd,
    parent_path: PathBuf,
    parent_identity: DirectoryIdentity,
    leaf: CString,
    descriptor_path: PathBuf,
}

/// An owned, nonblocking Unix `SOCK_SEQPACKET` listener.
#[derive(Debug)]
pub struct EndpointListener {
    fd: OwnedFd,
    parent: OwnedFd,
    leaf: CString,
    identity: EndpointIdentity,
}

impl EndpointListener {
    /// Binds a private same-user endpoint, safely replacing only a stale socket.
    pub fn bind(path: &Path) -> io::Result<Self> {
        make_address(path)?;
        let endpoint = EndpointPath::resolve(path)?;
        let (address, length) = make_address(&endpoint.descriptor_path)?;
        prepare_path(&endpoint, &address, length)?;

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

        let status = status_at(endpoint.parent.as_fd(), &endpoint.leaf)?.ok_or_else(|| {
            io::Error::new(io::ErrorKind::NotFound, "bound GWIPC endpoint disappeared")
        })?;
        // SAFETY: `geteuid` has no preconditions.
        let effective_uid = unsafe { geteuid() };
        if status.st_mode & S_IFMT != S_IFSOCK || status.st_uid != effective_uid {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "bound GWIPC endpoint is not a same-euid socket",
            ));
        }
        let identity = EndpointIdentity::from_status(&status);
        let cleanup = || unlink_if_owned_at(endpoint.parent.as_fd(), &endpoint.leaf, identity);
        // SAFETY: `parent` is a live directory and `leaf` is NUL-terminated.
        if unsafe {
            fchmodat(
                endpoint.parent.as_raw_fd(),
                endpoint.leaf.as_ptr(),
                0o600,
                0,
            )
        } != 0
        {
            let error = io::Error::last_os_error();
            cleanup();
            return Err(error);
        }
        // SAFETY: `fd` is a valid listening-capable local socket.
        if unsafe { listen(fd.as_raw_fd(), 32) } != 0 {
            let error = io::Error::last_os_error();
            cleanup();
            return Err(error);
        }
        let verified = status_at(endpoint.parent.as_fd(), &endpoint.leaf)?
            .ok_or_else(|| io::Error::from_raw_os_error(ESTALE))?;
        if !identity.matches(&verified) || verified.st_uid != effective_uid {
            cleanup();
            return Err(io::Error::from_raw_os_error(ESTALE));
        }
        let current_parent = open_trusted_directory_chain(&endpoint.parent_path)?;
        let current_status = status_fd(current_parent.as_fd())?;
        if !endpoint.parent_identity.matches(&current_status) {
            cleanup();
            return Err(io::Error::from_raw_os_error(ESTALE));
        }
        Ok(Self {
            fd,
            parent: endpoint.parent,
            leaf: endpoint.leaf,
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
            match require_same_euid_peer(peer.as_fd()) {
                Ok(()) => return Ok(Some(peer)),
                Err(error)
                    if error.kind() == io::ErrorKind::PermissionDenied
                        && error.raw_os_error().is_none() => {}
                Err(error) => return Err(error),
            }
            // Drop and continue so an unauthorized queued peer cannot prevent
            // a same-user peer behind it from being serviced.
        }
    }
}

impl Drop for EndpointListener {
    fn drop(&mut self) {
        unlink_if_owned_at(self.parent.as_fd(), &self.leaf, self.identity);
    }
}

impl EndpointPath {
    fn resolve(path: &Path) -> io::Result<Self> {
        let leaf = path.file_name().ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC endpoint path has no filename",
            )
        })?;
        if matches!(leaf.as_bytes(), b"." | b"..") {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC endpoint filename is not a normal path component",
            ));
        }
        let leaf = CString::new(leaf.as_bytes()).map_err(|_| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC endpoint filename contains NUL",
            )
        })?;
        let parent = path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        let parent_path = absolute_normalized(parent)?;
        let parent = open_trusted_directory_chain(&parent_path)?;
        let parent_status = status_fd(parent.as_fd())?;
        let parent_identity = DirectoryIdentity::from_status(&parent_status);
        let descriptor_path = PathBuf::from(format!("/proc/self/fd/{}", parent.as_raw_fd()))
            .join(OsStr::from_bytes(leaf.as_bytes()));
        Ok(Self {
            parent,
            parent_path,
            parent_identity,
            leaf,
            descriptor_path,
        })
    }
}

fn absolute_normalized(path: &Path) -> io::Result<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_owned()
    } else {
        std::env::current_dir()?.join(path)
    };
    let mut normalized = PathBuf::from("/");
    for component in absolute.components() {
        match component {
            Component::RootDir | Component::CurDir => {}
            Component::ParentDir => {
                normalized.pop();
            }
            Component::Normal(name) => normalized.push(name),
            Component::Prefix(_) => unreachable!("Unix paths do not have prefixes"),
        }
    }
    Ok(normalized)
}

fn open_trusted_directory_chain(path: &Path) -> io::Result<OwnedFd> {
    debug_assert!(path.is_absolute());
    let root = CString::new("/").expect("root path has no NUL");
    // SAFETY: `root` is NUL-terminated and the flags require a directory
    // descriptor without following a final symlink.
    let raw = unsafe {
        openat(
            AT_FDCWD,
            root.as_ptr(),
            O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
        )
    };
    if raw < 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful `openat` returned a fresh descriptor.
    let mut current = unsafe { OwnedFd::from_raw_fd(raw) };
    let root_status = status_fd(current.as_fd())?;
    let root_uid = root_status.st_uid;
    // SAFETY: `geteuid` has no preconditions.
    let effective_uid = unsafe { geteuid() };
    require_trusted_directory(&root_status, root_uid, effective_uid)?;

    for component in path.components() {
        let Component::Normal(name) = component else {
            continue;
        };
        let name = CString::new(name.as_bytes()).map_err(|_| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "GWIPC endpoint ancestor contains NUL",
            )
        })?;
        // SAFETY: `current` is a live directory descriptor and `name` is a
        // single NUL-terminated component. O_NOFOLLOW rejects symlinked
        // ancestors rather than resolving them between checks.
        let raw = unsafe {
            openat(
                current.as_raw_fd(),
                name.as_ptr(),
                O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
            )
        };
        if raw < 0 {
            let error = io::Error::last_os_error();
            return if matches!(error.raw_os_error(), Some(20) | Some(40)) {
                Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "GWIPC endpoint ancestor is not a real directory",
                ))
            } else {
                Err(error)
            };
        }
        // SAFETY: successful `openat` returned a fresh descriptor.
        let next = unsafe { OwnedFd::from_raw_fd(raw) };
        let status = status_fd(next.as_fd())?;
        require_trusted_directory(&status, root_uid, effective_uid)?;
        current = next;
    }
    Ok(current)
}

fn require_trusted_directory(
    status: &gw_sys::stat,
    root_uid: u32,
    effective_uid: u32,
) -> io::Result<()> {
    if status.st_mode & S_IFMT != S_IFDIR {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "GWIPC endpoint ancestor is not a directory",
        ));
    }
    if status.st_uid != root_uid && status.st_uid != effective_uid {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "GWIPC endpoint ancestor has an untrusted owner",
        ));
    }
    if status.st_mode & WRITE_BY_GROUP_OR_OTHER != 0 && status.st_mode & STICKY == 0 {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "GWIPC endpoint ancestor is writable without sticky protection",
        ));
    }
    Ok(())
}

fn status_fd(fd: BorrowedFd<'_>) -> io::Result<gw_sys::stat> {
    let mut status = gw_sys::stat::default();
    // SAFETY: `status` is writable and `fd` remains live for the call.
    if unsafe { gw_sys::fstat(fd.as_raw_fd(), &raw mut status) } != 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(status)
}

fn status_at(parent: BorrowedFd<'_>, leaf: &CString) -> io::Result<Option<gw_sys::stat>> {
    let mut status = gw_sys::stat::default();
    // SAFETY: `parent` is live, `leaf` is NUL-terminated, and `status` is
    // writable. AT_SYMLINK_NOFOLLOW inspects the endpoint itself.
    if unsafe {
        fstatat(
            parent.as_raw_fd(),
            leaf.as_ptr(),
            &raw mut status,
            AT_SYMLINK_NOFOLLOW,
        )
    } != 0
    {
        let error = io::Error::last_os_error();
        return if error.raw_os_error() == Some(ENOENT) {
            Ok(None)
        } else {
            Err(error)
        };
    }
    Ok(Some(status))
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

fn prepare_path(endpoint: &EndpointPath, address: &SockAddrUnix, length: u32) -> io::Result<()> {
    let status = match status_at(endpoint.parent.as_fd(), &endpoint.leaf)? {
        Some(status) => status,
        None => return Ok(()),
    };
    // SAFETY: `geteuid` has no preconditions.
    let effective_uid = unsafe { geteuid() };
    if status.st_mode & S_IFMT != S_IFSOCK || status.st_uid != effective_uid {
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
    let identity = EndpointIdentity::from_status(&status);
    let unchanged = status_at(endpoint.parent.as_fd(), &endpoint.leaf)?
        .ok_or_else(|| io::Error::from_raw_os_error(ESTALE))?;
    if !identity.matches(&unchanged) || unchanged.st_uid != effective_uid {
        return Err(io::Error::from_raw_os_error(ESTALE));
    }
    // SAFETY: `parent` is live and `leaf` is NUL-terminated. The ownership and
    // identity checks above ensure only the stale same-user socket is removed.
    if unsafe { unlinkat(endpoint.parent.as_raw_fd(), endpoint.leaf.as_ptr(), 0) } != 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
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

/// Requires a connected Unix socket peer to have the process's effective UID.
///
/// Call this only after a nonblocking connect has completed successfully. It
/// deliberately checks the kernel's peer credentials rather than trusting the
/// ownership or permissions of the socket pathname.
pub fn require_same_euid_peer(fd: BorrowedFd<'_>) -> io::Result<()> {
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
            fd.as_raw_fd(),
            SOL_SOCKET,
            SO_PEERCRED,
            (&raw mut credentials).cast::<c_void>(),
            &raw mut length,
        )
    } != 0
    {
        return Err(io::Error::last_os_error());
    }
    if length as usize != size_of::<Credentials>() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "SO_PEERCRED returned an invalid credential size",
        ));
    }
    // SAFETY: `geteuid` has no preconditions.
    require_matching_euid(credentials.uid, unsafe { geteuid() })
}

fn require_matching_euid(peer_uid: u32, effective_uid: u32) -> io::Result<()> {
    if peer_uid != effective_uid {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "GWIPC peer effective UID does not match this process",
        ));
    }
    Ok(())
}

fn unlink_if_owned_at(parent: BorrowedFd<'_>, leaf: &CString, identity: EndpointIdentity) {
    if status_at(parent, leaf).is_ok_and(|status| status.is_some_and(|s| identity.matches(&s))) {
        // SAFETY: `parent` is live and `leaf` is NUL-terminated. The validated
        // directory policy prevents a different-EUID process from replacing
        // the leaf between the identity check and unlinkat; same-EUID
        // processes are the documented local trust boundary. Failure is
        // intentionally ignored during best-effort listener cleanup.
        let _ = unsafe { unlinkat(parent.as_raw_fd(), leaf.as_ptr(), 0) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::fd::AsFd;
    use std::os::unix::fs::{FileTypeExt, MetadataExt, PermissionsExt, symlink};
    use std::os::unix::net::UnixListener;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_PATH: AtomicU64 = AtomicU64::new(1);

    #[test]
    fn connected_same_euid_peer_is_accepted() {
        let (first, _second) = crate::Transport::pair(crate::TransportLimits::DEFAULT).unwrap();
        require_same_euid_peer(first.as_fd()).unwrap();
    }

    #[test]
    fn different_euid_peer_is_rejected() {
        let error = require_matching_euid(1000, 1001).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);
        assert_eq!(error.raw_os_error(), None);
    }

    fn path(label: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "gw-ipc-endpoint-{}-{label}-{}",
            std::process::id(),
            NEXT_PATH.fetch_add(1, Ordering::Relaxed)
        ))
    }

    fn connect_seqpacket(path: &Path) -> io::Result<OwnedFd> {
        let (address, length) = make_address(path)?;
        // SAFETY: arguments select a local, nonblocking, close-on-exec socket.
        let raw = unsafe { socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: successful `socket` returned a fresh descriptor.
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        // SAFETY: `address` remains initialized and live for `length` bytes.
        if unsafe {
            connect(
                fd.as_raw_fd(),
                (&raw const address).cast::<c_void>(),
                length,
            )
        } != 0
        {
            let error = io::Error::last_os_error();
            if !matches!(error.raw_os_error(), Some(EINPROGRESS) | Some(EAGAIN)) {
                return Err(error);
            }
        }
        Ok(fd)
    }

    #[test]
    fn descriptor_relative_listener_accepts_public_path_connection() {
        let path = path("public-connect");
        let listener = EndpointListener::bind(&path).unwrap();
        let _client = connect_seqpacket(&path).unwrap();

        assert!(listener.accept().unwrap().is_some());
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
        let replacement_metadata = fs::symlink_metadata(&path).unwrap();
        let replacement_identity = (replacement_metadata.dev(), replacement_metadata.ino());

        drop(first);
        let after_drop = fs::symlink_metadata(&path).unwrap();
        assert_eq!(replacement_identity, (after_drop.dev(), after_drop.ino()));
        drop(replacement);
        assert!(!path.exists());
        fs::remove_file(displaced).unwrap();
    }

    #[test]
    fn listener_rejects_unprotected_writable_parent() {
        let directory = path("writable-parent");
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, fs::Permissions::from_mode(0o777)).unwrap();

        let error = EndpointListener::bind(&directory.join("socket")).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);

        fs::remove_dir(directory).unwrap();
    }

    #[test]
    fn listener_accepts_sticky_shared_parent() {
        let directory = path("sticky-parent");
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, fs::Permissions::from_mode(0o1777)).unwrap();

        let listener = EndpointListener::bind(&directory.join("socket")).unwrap();
        drop(listener);

        fs::remove_dir(directory).unwrap();
    }

    #[test]
    fn listener_rejects_symlinked_parent() {
        let directory = path("symlink-parent");
        let real = directory.join("real");
        let linked = directory.join("linked");
        fs::create_dir(&directory).unwrap();
        fs::create_dir(&real).unwrap();
        symlink(&real, &linked).unwrap();

        let error = EndpointListener::bind(&linked.join("socket")).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::InvalidInput);

        fs::remove_file(linked).unwrap();
        fs::remove_dir(real).unwrap();
        fs::remove_dir(directory).unwrap();
    }

    #[test]
    fn drop_cleans_original_parent_after_parent_path_replacement() {
        let directory = path("parent-replacement");
        let parent = directory.join("parent");
        let displaced = directory.join("displaced");
        let socket = parent.join("socket");
        fs::create_dir(&directory).unwrap();
        fs::create_dir(&parent).unwrap();
        let listener = EndpointListener::bind(&socket).unwrap();

        fs::rename(&parent, &displaced).unwrap();
        fs::create_dir(&parent).unwrap();
        let replacement = UnixListener::bind(&socket).unwrap();
        let replacement_metadata = fs::symlink_metadata(&socket).unwrap();

        drop(listener);
        let after_drop = fs::symlink_metadata(&socket).unwrap();
        assert_eq!(
            (replacement_metadata.dev(), replacement_metadata.ino()),
            (after_drop.dev(), after_drop.ino())
        );
        assert!(!displaced.join("socket").exists());

        drop(replacement);
        fs::remove_file(socket).unwrap();
        fs::remove_dir(parent).unwrap();
        fs::remove_dir(displaced).unwrap();
        fs::remove_dir(directory).unwrap();
    }

    #[test]
    fn untrusted_directory_owner_is_rejected() {
        let mut status = gw_sys::stat {
            st_mode: S_IFDIR | 0o700,
            st_uid: 1002,
            ..gw_sys::stat::default()
        };
        let error = require_trusted_directory(&status, 0, 1001).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);

        status.st_uid = 1001;
        require_trusted_directory(&status, 0, 1001).unwrap();
    }
}
