use std::fs;
use std::io;
use std::mem::size_of;
use std::os::fd::AsRawFd;
use std::os::raw::{c_int, c_void};
use std::os::unix::fs::{FileTypeExt, MetadataExt, PermissionsExt};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Component;
use std::path::{Path, PathBuf};

unsafe extern "C" {
    fn geteuid() -> u32;
    fn getsockopt(
        socket: c_int,
        level: c_int,
        option_name: c_int,
        option_value: *mut c_void,
        option_length: *mut u32,
    ) -> c_int;
}

const SOL_SOCKET: c_int = 1;
const SO_PEERCRED: c_int = 17;
const SOCKET_MODE: u32 = 0o600;
const WRITE_BY_GROUP_OR_OTHER: u32 = 0o022;
const STICKY: u32 = 0o1000;
const ESTALE: i32 = 116;
const MAXIMUM_REJECTED_PEERS_PER_ACCEPT: usize = 16;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct Credentials {
    pid: i32,
    uid: u32,
    gid: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct SocketIdentity {
    device: u64,
    inode: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct DirectoryIdentity {
    device: u64,
    inode: u64,
}

impl DirectoryIdentity {
    fn from_metadata(metadata: &fs::Metadata) -> Self {
        Self {
            device: metadata.dev(),
            inode: metadata.ino(),
        }
    }

    fn matches(self, metadata: &fs::Metadata) -> bool {
        metadata.file_type().is_dir()
            && metadata.dev() == self.device
            && metadata.ino() == self.inode
    }
}

impl SocketIdentity {
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

pub(crate) struct OwnedListener {
    listener: UnixListener,
    path: PathBuf,
    identity: SocketIdentity,
}

impl OwnedListener {
    pub(crate) fn bind(path: &Path) -> io::Result<Self> {
        let parent = path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        let parent_identity = ensure_parent_chain(parent)?;
        prepare_path(path)?;

        let listener = UnixListener::bind(path)?;
        let metadata = fs::symlink_metadata(path)?;
        if !metadata.file_type().is_socket() {
            let _ = fs::remove_file(path);
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "bound X11 path is not a socket",
            ));
        }
        let identity = SocketIdentity::from_metadata(&metadata);
        if let Err(error) = fs::set_permissions(path, fs::Permissions::from_mode(SOCKET_MODE)) {
            unlink_if_owned(path, identity);
            return Err(error);
        }
        let current_parent = fs::symlink_metadata(parent)?;
        if !parent_identity.matches(&current_parent) {
            unlink_if_owned(path, identity);
            return Err(io::Error::from_raw_os_error(ESTALE));
        }
        if let Err(error) = listener.set_nonblocking(true) {
            unlink_if_owned(path, identity);
            return Err(error);
        }
        Ok(Self {
            listener,
            path: path.to_owned(),
            identity,
        })
    }

    pub(crate) fn accept(&self) -> io::Result<Option<UnixStream>> {
        for _ in 0..MAXIMUM_REJECTED_PEERS_PER_ACCEPT {
            match self.listener.accept() {
                Ok((stream, _)) => {
                    if let Err(error) = require_same_euid_peer(&stream) {
                        eprintln!("glasswyrmd: rejected X11 peer: {error}");
                        continue;
                    }
                    stream.set_nonblocking(false)?;
                    return Ok(Some(stream));
                }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(None),
                Err(error) => return Err(error),
            }
        }
        Ok(None)
    }
}

impl Drop for OwnedListener {
    fn drop(&mut self) {
        unlink_if_owned(&self.path, self.identity);
    }
}

fn ensure_parent_chain(parent: &Path) -> io::Result<DirectoryIdentity> {
    let absolute = if parent.is_absolute() {
        parent.to_owned()
    } else {
        std::env::current_dir()?.join(parent)
    };
    let effective_uid = effective_uid();
    // User-namespace test environments may expose the host root owner through
    // an overflow UID. Trust the owner of the process's root directory as the
    // filesystem administrator rather than assuming it is numerically zero.
    let root_uid = fs::symlink_metadata("/")?.uid();
    let mut current = PathBuf::from("/");
    for component in absolute.components() {
        match component {
            Component::RootDir | Component::CurDir => continue,
            Component::ParentDir => {
                current.pop();
                continue;
            }
            Component::Normal(name) => current.push(name),
            Component::Prefix(_) => unreachable!("Unix paths do not have prefixes"),
        }
        let metadata = match fs::symlink_metadata(&current) {
            Ok(metadata) => metadata,
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                match fs::create_dir(&current) {
                    Ok(()) => {}
                    Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {}
                    Err(error) => return Err(error),
                }
                fs::symlink_metadata(&current)?
            }
            Err(error) => return Err(error),
        };
        if !metadata.file_type().is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!(
                    "X11 socket path ancestor is not a directory: {}",
                    current.display()
                ),
            ));
        }
        if metadata.uid() != root_uid && metadata.uid() != effective_uid {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                format!(
                    "X11 socket path ancestor has an untrusted owner: {}",
                    current.display()
                ),
            ));
        }
        let mode = metadata.mode();
        if mode & WRITE_BY_GROUP_OR_OTHER != 0 && mode & STICKY == 0 {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                format!(
                    "X11 socket path ancestor is writable without sticky protection: {}",
                    current.display()
                ),
            ));
        }
    }

    let metadata = fs::symlink_metadata(&absolute)?;
    Ok(DirectoryIdentity::from_metadata(&metadata))
}

fn require_same_euid_peer(stream: &UnixStream) -> io::Result<()> {
    let mut credentials = Credentials::default();
    let mut length = u32::try_from(size_of::<Credentials>()).expect("credential size fits u32");
    // SAFETY: `credentials` is writable for `length` bytes, and the accepted
    // Unix stream descriptor remains live for the duration of the call.
    if unsafe {
        getsockopt(
            stream.as_raw_fd(),
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
    require_matching_euid(credentials.uid, effective_uid())
}

fn require_matching_euid(peer_uid: u32, effective_uid: u32) -> io::Result<()> {
    if peer_uid != effective_uid {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            "X11 peer effective UID does not match glasswyrmd",
        ));
    }
    Ok(())
}

fn effective_uid() -> u32 {
    // SAFETY: `geteuid` has no preconditions and does not retain pointers.
    unsafe { geteuid() }
}

fn prepare_path(path: &Path) -> io::Result<()> {
    let before = match fs::symlink_metadata(path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error),
    };
    let effective_uid = effective_uid();
    if !before.file_type().is_socket() {
        return Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            format!("refusing to replace non-socket path {}", path.display()),
        ));
    }
    if before.uid() != effective_uid {
        return Err(io::Error::new(
            io::ErrorKind::PermissionDenied,
            format!(
                "refusing to remove socket owned by another user: {}",
                path.display()
            ),
        ));
    }

    match UnixStream::connect(path) {
        Ok(_) => {
            return Err(io::Error::new(
                io::ErrorKind::AddrInUse,
                format!("display socket is already active: {}", path.display()),
            ));
        }
        Err(error)
            if matches!(
                error.kind(),
                io::ErrorKind::ConnectionRefused | io::ErrorKind::NotFound
            ) => {}
        Err(error) => return Err(error),
    }

    let identity = SocketIdentity::from_metadata(&before);
    let after = fs::symlink_metadata(path)?;
    if !identity.matches(&after) || after.uid() != effective_uid {
        return Err(io::Error::from_raw_os_error(ESTALE));
    }
    fs::remove_file(path)
}

fn unlink_if_owned(path: &Path, identity: SocketIdentity) {
    if fs::symlink_metadata(path).is_ok_and(|metadata| identity.matches(&metadata)) {
        let _ = fs::remove_file(path);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::symlink;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_TEST_DIRECTORY: AtomicU64 = AtomicU64::new(1);

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            let sequence = NEXT_TEST_DIRECTORY.fetch_add(1, Ordering::Relaxed);
            let path = std::env::temp_dir().join(format!(
                "glasswyrmd-listener-test-{}-{sequence}",
                std::process::id()
            ));
            fs::create_dir(&path).unwrap();
            Self(path)
        }

        fn socket_path(&self) -> PathBuf {
            self.0.join("X-test")
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn bind_error(path: &Path) -> io::Error {
        match OwnedListener::bind(path) {
            Ok(_) => panic!("listener bind unexpectedly succeeded"),
            Err(error) => error,
        }
    }

    #[test]
    fn listener_is_private_and_accepts_same_uid_peer() {
        let directory = TestDirectory::new();
        let path = directory.socket_path();
        let listener = OwnedListener::bind(&path).unwrap();

        assert_eq!(fs::symlink_metadata(&path).unwrap().mode() & 0o777, 0o600);
        let client = UnixStream::connect(&path).unwrap();
        assert!(listener.accept().unwrap().is_some());
        drop(client);
    }

    #[test]
    fn listener_rejects_unprotected_writable_parent() {
        let directory = TestDirectory::new();
        fs::set_permissions(&directory.0, fs::Permissions::from_mode(0o777)).unwrap();

        let error = bind_error(&directory.socket_path());
        assert_eq!(error.kind(), io::ErrorKind::PermissionDenied);
    }

    #[test]
    fn listener_accepts_sticky_shared_parent_like_x11_directory() {
        let directory = TestDirectory::new();
        fs::set_permissions(&directory.0, fs::Permissions::from_mode(0o1777)).unwrap();

        let listener = OwnedListener::bind(&directory.socket_path()).unwrap();
        assert_eq!(
            fs::symlink_metadata(&listener.path).unwrap().mode() & 0o777,
            0o600
        );
    }

    #[test]
    fn listener_rejects_symlinked_parent() {
        let directory = TestDirectory::new();
        let real = directory.0.join("real");
        let linked = directory.0.join("linked");
        fs::create_dir(&real).unwrap();
        symlink(&real, &linked).unwrap();

        let error = bind_error(&linked.join("nested/X-test"));
        assert_eq!(error.kind(), io::ErrorKind::InvalidInput);
        assert!(!real.join("nested").exists());
    }

    #[test]
    fn drop_does_not_unlink_a_replacement_socket() {
        let directory = TestDirectory::new();
        let path = directory.socket_path();
        let displaced = directory.0.join("displaced");
        let listener = OwnedListener::bind(&path).unwrap();
        fs::rename(&path, &displaced).unwrap();
        let replacement = UnixListener::bind(&path).unwrap();

        drop(listener);
        assert!(fs::symlink_metadata(&path).unwrap().file_type().is_socket());
        drop(replacement);
    }

    #[test]
    fn stale_socket_cleanup_does_not_replace_non_socket_paths() {
        let directory = TestDirectory::new();
        let path = directory.socket_path();
        fs::write(&path, b"preserve").unwrap();

        let error = bind_error(&path);
        assert_eq!(error.kind(), io::ErrorKind::AlreadyExists);
        assert_eq!(fs::read(&path).unwrap(), b"preserve");
    }

    #[test]
    fn peer_uid_must_match_effective_uid() {
        assert!(require_matching_euid(1000, 1000).is_ok());
        assert_eq!(
            require_matching_euid(1001, 1000).unwrap_err().kind(),
            io::ErrorKind::PermissionDenied
        );
    }
}
