use core::ffi::c_void;
use core::mem::size_of;
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};

use crate::HardenedFd;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UnixSocketType {
    Stream,
    SeqPacket,
}

impl UnixSocketType {
    const fn raw(self) -> i32 {
        match self {
            Self::Stream => gw_sys::SOCK_STREAM,
            Self::SeqPacket => gw_sys::SOCK_SEQPACKET,
        }
    }
}

/// An owned, nonblocking, close-on-exec Unix socket.
#[derive(Debug)]
pub struct UnixSocket {
    fd: HardenedFd,
    socket_type: UnixSocketType,
}

impl UnixSocket {
    pub fn new(socket_type: UnixSocketType) -> io::Result<Self> {
        // SAFETY: scalar arguments request a fresh local socket descriptor.
        let raw = unsafe {
            gw_sys::socket(
                gw_sys::AF_UNIX,
                socket_type.raw() | gw_sys::SOCK_CLOEXEC | gw_sys::SOCK_NONBLOCK,
                0,
            )
        };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: a nonnegative socket result is freshly owned here.
        let owned = unsafe { OwnedFd::from_raw_fd(raw) };
        Ok(Self {
            fd: HardenedFd::new(owned)?,
            socket_type,
        })
    }

    pub fn pair(socket_type: UnixSocketType) -> io::Result<(Self, Self)> {
        let mut sockets = [-1; 2];
        // SAFETY: `sockets` has space for exactly two descriptors. On success,
        // both returned descriptors are fresh and uniquely owned here.
        let result = unsafe {
            gw_sys::socketpair(
                gw_sys::AF_UNIX,
                socket_type.raw() | gw_sys::SOCK_CLOEXEC | gw_sys::SOCK_NONBLOCK,
                0,
                sockets.as_mut_ptr(),
            )
        };
        if result != 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: successful socketpair returned two fresh descriptors, neither
        // of which has yet been transferred.
        let first = unsafe { OwnedFd::from_raw_fd(sockets[0]) };
        // SAFETY: same ownership argument as `first`, for the other endpoint.
        let second = unsafe { OwnedFd::from_raw_fd(sockets[1]) };
        Ok((
            Self {
                fd: HardenedFd::new(first)?,
                socket_type,
            },
            Self {
                fd: HardenedFd::new(second)?,
                socket_type,
            },
        ))
    }

    /// Adopts a Unix stream or seqpacket socket and hardens its descriptor.
    pub fn from_owned_fd(fd: OwnedFd) -> io::Result<Self> {
        let socket_type = socket_type(fd.as_fd())?;
        Ok(Self {
            fd: HardenedFd::new(fd)?,
            socket_type,
        })
    }

    #[must_use]
    pub const fn socket_type(&self) -> UnixSocketType {
        self.socket_type
    }

    #[must_use]
    pub fn into_owned_fd(self) -> OwnedFd {
        self.fd.into_inner()
    }
}

impl AsFd for UnixSocket {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PeerCredentials {
    pub process_id: i32,
    pub user_id: u32,
    pub group_id: u32,
}

pub fn peer_credentials(fd: BorrowedFd<'_>) -> io::Result<PeerCredentials> {
    let mut credentials = gw_sys::ucred::default();
    let mut length = u32::try_from(size_of::<gw_sys::ucred>()).expect("ucred size fits socklen_t");
    // SAFETY: `credentials` is writable for the advertised size, `length` is a
    // valid in/out pointer, and the borrowed descriptor remains live.
    if unsafe {
        gw_sys::getsockopt(
            fd.as_raw_fd(),
            gw_sys::SOL_SOCKET,
            gw_sys::SO_PEERCRED,
            (&raw mut credentials).cast::<c_void>(),
            &raw mut length,
        )
    } != 0
    {
        return Err(io::Error::last_os_error());
    }
    if length as usize != size_of::<gw_sys::ucred>() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "SO_PEERCRED returned an unexpected structure size",
        ));
    }
    Ok(PeerCredentials {
        process_id: credentials.pid,
        user_id: credentials.uid,
        group_id: credentials.gid,
    })
}

fn socket_type(fd: BorrowedFd<'_>) -> io::Result<UnixSocketType> {
    let mut value = 0_i32;
    let mut length = u32::try_from(size_of::<i32>()).expect("socket type size fits socklen_t");
    // SAFETY: `value` is writable for the advertised size and `length` is a
    // valid in/out pointer. SO_TYPE does not consume or mutate the descriptor.
    if unsafe {
        gw_sys::getsockopt(
            fd.as_raw_fd(),
            gw_sys::SOL_SOCKET,
            gw_sys::SO_TYPE,
            (&raw mut value).cast::<c_void>(),
            &raw mut length,
        )
    } != 0
    {
        return Err(io::Error::last_os_error());
    }
    if length as usize != size_of::<i32>() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "SO_TYPE returned an unexpected structure size",
        ));
    }
    match value {
        gw_sys::SOCK_STREAM => Ok(UnixSocketType::Stream),
        gw_sys::SOCK_SEQPACKET => Ok(UnixSocketType::SeqPacket),
        _ => Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "descriptor is not a supported Unix socket",
        )),
    }
}
