use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, IntoRawFd, OwnedFd, RawFd};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DescriptorFlags {
    pub close_on_exec: bool,
    pub nonblocking: bool,
}

/// An owned descriptor hardened for event-loop use.
///
/// Construction ensures both close-on-exec and nonblocking mode. The wrapper
/// never duplicates the descriptor and closes it on drop.
#[derive(Debug)]
pub struct HardenedFd(OwnedFd);

impl HardenedFd {
    pub fn new(fd: OwnedFd) -> io::Result<Self> {
        harden_descriptor(fd.as_fd())?;
        Ok(Self(fd))
    }

    #[must_use]
    pub fn into_inner(self) -> OwnedFd {
        self.0
    }
}

impl AsFd for HardenedFd {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.0.as_fd()
    }
}

impl AsRawFd for HardenedFd {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

impl IntoRawFd for HardenedFd {
    fn into_raw_fd(self) -> RawFd {
        self.0.into_raw_fd()
    }
}

pub fn descriptor_flags(fd: BorrowedFd<'_>) -> io::Result<DescriptorFlags> {
    // SAFETY: `fd` is borrowed and valid for the duration of both calls. The
    // selected `fcntl` commands take no variadic argument.
    let (descriptor, status) = unsafe {
        (
            gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_GETFD),
            gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_GETFL),
        )
    };
    if descriptor < 0 || status < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(DescriptorFlags {
        close_on_exec: descriptor & gw_sys::FD_CLOEXEC != 0,
        nonblocking: status & gw_sys::O_NONBLOCK != 0,
    })
}

pub fn harden_descriptor(fd: BorrowedFd<'_>) -> io::Result<()> {
    // SAFETY: `fd` is borrowed and valid for all calls. F_GET* take no
    // variadic argument; F_SET* receive the required integer flag argument.
    unsafe {
        let descriptor = gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_GETFD);
        if descriptor < 0 {
            return Err(io::Error::last_os_error());
        }
        if descriptor & gw_sys::FD_CLOEXEC == 0
            && gw_sys::fcntl(
                fd.as_raw_fd(),
                gw_sys::F_SETFD,
                descriptor | gw_sys::FD_CLOEXEC,
            ) < 0
        {
            return Err(io::Error::last_os_error());
        }

        let status = gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_GETFL);
        if status < 0 {
            return Err(io::Error::last_os_error());
        }
        if status & gw_sys::O_NONBLOCK == 0
            && gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_SETFL, status | gw_sys::O_NONBLOCK) < 0
        {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}
