use core::ffi::c_void;
use core::ops::{BitOr, BitOrAssign};
use core::ptr::NonNull;
use std::ffi::CString;
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};

use crate::HardenedFd;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MapAccess {
    ReadOnly,
    ReadWrite,
}

/// A shared file mapping that is unmapped on drop.
#[derive(Debug)]
pub struct Mapping {
    address: NonNull<u8>,
    length: usize,
    access: MapAccess,
}

impl Mapping {
    pub fn map(
        fd: BorrowedFd<'_>,
        length: usize,
        offset: u64,
        access: MapAccess,
    ) -> io::Result<Self> {
        if length == 0 || offset > i64::MAX as u64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping length and offset are invalid",
            ));
        }
        let protection = match access {
            MapAccess::ReadOnly => gw_sys::PROT_READ,
            MapAccess::ReadWrite => gw_sys::PROT_READ | gw_sys::PROT_WRITE,
        };
        // SAFETY: the kernel validates `fd`, page alignment, offset, and
        // protection. Null requests an address chosen by the kernel. A
        // successful mapping is uniquely owned by the returned RAII object.
        let address = unsafe {
            gw_sys::mmap(
                core::ptr::null_mut(),
                length,
                protection,
                gw_sys::MAP_SHARED,
                fd.as_raw_fd(),
                offset as i64,
            )
        };
        if address == gw_sys::MAP_FAILED {
            return Err(io::Error::last_os_error());
        }
        let address = NonNull::new(address.cast::<u8>()).ok_or_else(|| {
            io::Error::other("mmap unexpectedly returned a null successful address")
        })?;
        Ok(Self {
            address,
            length,
            access,
        })
    }

    #[must_use]
    pub const fn len(&self) -> usize {
        self.length
    }

    #[must_use]
    pub const fn is_empty(&self) -> bool {
        false
    }

    #[must_use]
    pub fn as_slice(&self) -> &[u8] {
        // SAFETY: construction validated a non-null mapping of `length` bytes;
        // the mapping remains live and cannot be unmapped while borrowed.
        unsafe { core::slice::from_raw_parts(self.address.as_ptr(), self.length) }
    }

    pub fn as_mut_slice(&mut self) -> io::Result<&mut [u8]> {
        if self.access != MapAccess::ReadWrite {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "mapping is read-only",
            ));
        }
        // SAFETY: `&mut self` provides exclusive access to this mapping object,
        // which owns a valid writable mapping of exactly `length` bytes.
        Ok(unsafe { core::slice::from_raw_parts_mut(self.address.as_ptr(), self.length) })
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        // SAFETY: this pair is exactly the successful mmap result and length,
        // and Drop runs once after all safe borrows have ended.
        let _ = unsafe { gw_sys::munmap(self.address.as_ptr().cast::<c_void>(), self.length) };
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SealSet(i32);

impl SealSet {
    pub const NONE: Self = Self(0);
    pub const SEAL: Self = Self(gw_sys::F_SEAL_SEAL);
    pub const SHRINK: Self = Self(gw_sys::F_SEAL_SHRINK);
    pub const GROW: Self = Self(gw_sys::F_SEAL_GROW);
    pub const WRITE: Self = Self(gw_sys::F_SEAL_WRITE);

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        self.0 & other.0 == other.0
    }
}

impl BitOr for SealSet {
    type Output = Self;

    fn bitor(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

impl BitOrAssign for SealSet {
    fn bitor_assign(&mut self, other: Self) {
        self.0 |= other.0;
    }
}

/// An anonymous sealable file backed by memory.
#[derive(Debug)]
pub struct Memfd {
    fd: HardenedFd,
}

impl Memfd {
    pub fn create(name: &str, length: u64) -> io::Result<Self> {
        let name = CString::new(name)
            .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "memfd name contains NUL"))?;
        let length = i64::try_from(length).map_err(|_| {
            io::Error::new(io::ErrorKind::InvalidInput, "memfd length exceeds off_t")
        })?;
        // SAFETY: `name` is a live NUL-terminated string and the flags request
        // a fresh close-on-exec descriptor with sealing enabled.
        let raw = unsafe {
            gw_sys::memfd_create(
                name.as_ptr(),
                gw_sys::MFD_CLOEXEC | gw_sys::MFD_ALLOW_SEALING,
            )
        };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: a nonnegative memfd_create result is a fresh descriptor owned
        // by this function and immediately placed under RAII.
        let owned = unsafe { OwnedFd::from_raw_fd(raw) };
        // SAFETY: the descriptor is valid and the nonnegative length fits
        // `off_t`; no borrowed memory is involved.
        if unsafe { gw_sys::ftruncate(owned.as_raw_fd(), length) } != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(Self {
            fd: HardenedFd::new(owned)?,
        })
    }

    pub fn map(&self, length: usize, access: MapAccess) -> io::Result<Mapping> {
        Mapping::map(self.fd.as_fd(), length, 0, access)
    }

    pub fn add_seals(&self, seals: SealSet) -> io::Result<()> {
        // SAFETY: F_ADD_SEALS receives the required integer bitmask; the kernel
        // validates the descriptor and supported seal combination.
        if unsafe { gw_sys::fcntl(self.fd.as_raw_fd(), gw_sys::F_ADD_SEALS, seals.0) } < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    pub fn seals(&self) -> io::Result<SealSet> {
        // SAFETY: F_GET_SEALS takes no variadic argument and does not mutate
        // userspace memory.
        let seals = unsafe { gw_sys::fcntl(self.fd.as_raw_fd(), gw_sys::F_GET_SEALS) };
        if seals < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(SealSet(seals))
    }
}

impl AsFd for Memfd {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}
