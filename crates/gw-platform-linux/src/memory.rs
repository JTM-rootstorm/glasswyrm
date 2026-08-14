use core::ffi::c_void;
use core::ops::{BitOr, BitOrAssign};
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
    address: *mut u8,
    length: usize,
    access: MapAccess,
}

impl Mapping {
    /// Maps a range from a sealed regular object.
    ///
    /// The descriptor remains owned by the caller and may be closed after this
    /// function returns. Shrink and grow seals are required so the validated
    /// range cannot later be resized out from under safe mapping operations.
    pub fn map(
        fd: BorrowedFd<'_>,
        length: usize,
        offset: u64,
        access: MapAccess,
    ) -> io::Result<Self> {
        if length == 0 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping length must be nonzero",
            ));
        }
        if offset > i64::MAX as u64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping offset exceeds off_t",
            ));
        }
        let length_u64 = u64::try_from(length).map_err(|_| {
            io::Error::new(io::ErrorKind::InvalidInput, "mapping length exceeds u64")
        })?;
        let end = offset.checked_add(length_u64).ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidInput, "mapping range overflowed")
        })?;
        if length > isize::MAX as usize {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping length exceeds the addressable pointer range",
            ));
        }

        let mut status = gw_sys::stat::default();
        // SAFETY: `fd` is valid for the duration of the call and `status`
        // points to writable storage with the platform `struct stat` layout.
        if unsafe { gw_sys::fstat(fd.as_raw_fd(), &mut status) } != 0 {
            return Err(io::Error::last_os_error());
        }
        if status.st_mode & gw_sys::S_IFMT != gw_sys::S_IFREG {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping descriptor is not a regular object",
            ));
        }

        // Safe access cannot tolerate a concurrent truncate, which would turn
        // an otherwise in-bounds memory access into SIGBUS. Requiring both
        // size seals also makes the following size snapshot stable.
        // SAFETY: F_GET_SEALS takes no variadic argument and does not mutate
        // userspace memory.
        let raw_seals = unsafe { gw_sys::fcntl(fd.as_raw_fd(), gw_sys::F_GET_SEALS) };
        if raw_seals < 0 || !SealSet(raw_seals).contains(SealSet::SHRINK | SealSet::GROW) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping descriptor lacks required shrink and grow seals",
            ));
        }

        // Refresh size after observing the seals. A competing descriptor may
        // have resized and then sealed the object after the first fstat; once
        // both seals are visible, this second size snapshot is stable.
        // SAFETY: the same descriptor and writable ABI-compatible storage used
        // by the first fstat remain valid here.
        if unsafe { gw_sys::fstat(fd.as_raw_fd(), &mut status) } != 0 {
            return Err(io::Error::last_os_error());
        }
        if status.st_size < 0 || end > status.st_size as u64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "mapping range exceeds backing object",
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
        if address.is_null() {
            // A null address can be a successful mmap result on systems that
            // permit page-zero mappings, but Rust pointer operations require a
            // non-null pointer even for zero-byte copies. Release it before
            // rejecting the mapping so this safe wrapper never stores null.
            // SAFETY: this is the exact successful mmap result and length.
            let _ = unsafe { gw_sys::munmap(address, length) };
            return Err(io::Error::other(
                "mmap returned an unsupported null address",
            ));
        }
        Ok(Self {
            address: address.cast::<u8>(),
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

    pub fn read(&self, offset: usize, destination: &mut [u8]) -> io::Result<()> {
        let range = checked_range(offset, destination.len(), self.length)?;
        // SAFETY: bounds checking proved that the source range lies wholly in
        // the live mapping. `destination` is independently borrowed writable
        // storage, so the two regions cannot overlap through this safe API.
        unsafe {
            core::ptr::copy_nonoverlapping(
                self.address.add(range.start),
                destination.as_mut_ptr(),
                destination.len(),
            );
        }
        Ok(())
    }

    pub fn write(&mut self, offset: usize, source: &[u8]) -> io::Result<()> {
        if self.access != MapAccess::ReadWrite {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "mapping is read-only",
            ));
        }
        let range = checked_range(offset, source.len(), self.length)?;
        // SAFETY: bounds checking proved that the destination range lies wholly
        // in the live writable mapping. `&mut self` prevents another safe
        // operation through this mapping object during the copy.
        unsafe {
            core::ptr::copy_nonoverlapping(
                source.as_ptr(),
                self.address.add(range.start),
                source.len(),
            );
        }
        Ok(())
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        // SAFETY: this pair is exactly the successful mmap result and length,
        // and Drop runs once after all safe borrows have ended.
        let _ = unsafe { gw_sys::munmap(self.address.cast::<c_void>(), self.length) };
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
        // A mapping must remain sized for its full lifetime. Apply these seals
        // before exposing it so another descriptor cannot truncate or extend
        // the backing object and invalidate the mapped range.
        self.add_seals(SealSet::SHRINK | SealSet::GROW)?;
        Mapping::map(self.fd.as_fd(), length, 0, access)
    }

    pub fn add_seals(&self, seals: SealSet) -> io::Result<()> {
        let existing = self.seals()?;
        let missing = SealSet(seals.0 & !existing.0);
        if missing == SealSet::NONE {
            return Ok(());
        }
        // SAFETY: F_ADD_SEALS receives the required integer bitmask; the kernel
        // validates the descriptor and supported seal combination.
        if unsafe { gw_sys::fcntl(self.fd.as_raw_fd(), gw_sys::F_ADD_SEALS, missing.0) } < 0 {
            let error = io::Error::last_os_error();
            // Another holder may have installed the requested seals, including
            // F_SEAL_SEAL, between our query and update. Treat that race as
            // success only after observing the complete requested set.
            if !self.seals()?.contains(seals) {
                return Err(error);
            }
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

fn checked_range(
    offset: usize,
    length: usize,
    mapping_length: usize,
) -> io::Result<core::ops::Range<usize>> {
    let end = offset
        .checked_add(length)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "mapping range overflowed"))?;
    if end > mapping_length {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "mapping range is out of bounds",
        ));
    }
    Ok(offset..end)
}
