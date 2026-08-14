use std::fs;
use std::io;
use std::os::unix::fs::{FileTypeExt, MetadataExt};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};

unsafe extern "C" {
    fn geteuid() -> u32;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct SocketIdentity {
    device: u64,
    inode: u64,
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
        fs::create_dir_all(parent)?;
        if !fs::symlink_metadata(parent)?.file_type().is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("socket directory is not a directory: {}", parent.display()),
            ));
        }
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
        match self.listener.accept() {
            Ok((stream, _)) => {
                stream.set_nonblocking(false)?;
                Ok(Some(stream))
            }
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => Ok(None),
            Err(error) => Err(error),
        }
    }
}

impl Drop for OwnedListener {
    fn drop(&mut self) {
        unlink_if_owned(&self.path, self.identity);
    }
}

fn prepare_path(path: &Path) -> io::Result<()> {
    let before = match fs::symlink_metadata(path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error),
    };
    // SAFETY: `geteuid` has no preconditions and does not retain pointers.
    let effective_uid = unsafe { geteuid() };
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
        return Err(io::Error::from_raw_os_error(116)); // ESTALE
    }
    fs::remove_file(path)
}

fn unlink_if_owned(path: &Path, identity: SocketIdentity) {
    if fs::symlink_metadata(path).is_ok_and(|metadata| identity.matches(&metadata)) {
        let _ = fs::remove_file(path);
    }
}
