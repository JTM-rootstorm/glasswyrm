use core::fmt;
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};
use std::time::Duration;

use crate::HardenedFd;

const EVENT_TOKEN: u64 = 1;

#[derive(Debug)]
pub enum SignalTokenError {
    Io(io::Error),
    UnexpectedToken(u64),
}

impl fmt::Display for SignalTokenError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "eventfd I/O failed: {error}"),
            Self::UnexpectedToken(token) => {
                write!(formatter, "eventfd returned unexpected token {token}")
            }
        }
    }
}

impl std::error::Error for SignalTokenError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::UnexpectedToken(_) => None,
        }
    }
}

impl From<io::Error> for SignalTokenError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

/// A nonblocking eventfd using Glasswyrm's exact one-token readiness contract.
#[derive(Debug)]
pub struct EventFd {
    fd: HardenedFd,
}

impl EventFd {
    pub fn new() -> io::Result<Self> {
        // SAFETY: eventfd takes scalar values only and returns a fresh
        // descriptor on success.
        let raw = unsafe { gw_sys::eventfd(0, gw_sys::EFD_CLOEXEC | gw_sys::EFD_NONBLOCK) };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: a nonnegative eventfd result is freshly owned here.
        let owned = unsafe { OwnedFd::from_raw_fd(raw) };
        Ok(Self {
            fd: HardenedFd::new(owned)?,
        })
    }

    pub fn signal(&self) -> io::Result<()> {
        write_u64(self.fd.as_fd(), EVENT_TOKEN)
    }

    /// Consumes one readiness token, returning `false` when no token is ready.
    /// Aggregated or otherwise noncanonical values are rejected.
    pub fn consume(&self) -> Result<bool, SignalTokenError> {
        match read_u64(self.fd.as_fd())? {
            Some(EVENT_TOKEN) => Ok(true),
            Some(other) => Err(SignalTokenError::UnexpectedToken(other)),
            None => Ok(false),
        }
    }
}

impl AsFd for EventFd {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

/// A nonblocking monotonic timerfd.
#[derive(Debug)]
pub struct TimerFd {
    fd: HardenedFd,
}

impl TimerFd {
    pub fn new() -> io::Result<Self> {
        // SAFETY: timerfd_create takes scalar constants and returns a fresh
        // descriptor on success.
        let raw = unsafe {
            gw_sys::timerfd_create(
                gw_sys::CLOCK_MONOTONIC,
                gw_sys::TFD_CLOEXEC | gw_sys::TFD_NONBLOCK,
            )
        };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: a nonnegative timerfd_create result is freshly owned here.
        let owned = unsafe { OwnedFd::from_raw_fd(raw) };
        Ok(Self {
            fd: HardenedFd::new(owned)?,
        })
    }

    pub fn arm_once(&self, delay: Duration) -> io::Result<()> {
        self.arm(delay, Duration::ZERO)
    }

    pub fn arm_periodic(&self, delay: Duration, interval: Duration) -> io::Result<()> {
        if interval.is_zero() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "timer interval must be nonzero",
            ));
        }
        self.arm(delay, interval)
    }

    pub fn disarm(&self) -> io::Result<()> {
        self.set_time(gw_sys::itimerspec::default())
    }

    pub fn read_expirations(&self) -> io::Result<Option<u64>> {
        read_u64(self.fd.as_fd())
    }

    fn arm(&self, delay: Duration, interval: Duration) -> io::Result<()> {
        if delay.is_zero() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "initial timer delay must be nonzero",
            ));
        }
        self.set_time(gw_sys::itimerspec {
            it_interval: duration_to_timespec(interval)?,
            it_value: duration_to_timespec(delay)?,
        })
    }

    fn set_time(&self, specification: gw_sys::itimerspec) -> io::Result<()> {
        // SAFETY: `specification` is initialized and live for the call; the
        // descriptor is an owned timerfd. No old value is requested.
        if unsafe {
            gw_sys::timerfd_settime(
                self.fd.as_raw_fd(),
                0,
                &raw const specification,
                core::ptr::null_mut(),
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}

impl AsFd for TimerFd {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

fn duration_to_timespec(duration: Duration) -> io::Result<gw_sys::timespec> {
    let seconds = i64::try_from(duration.as_secs())
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "duration exceeds time_t"))?;
    Ok(gw_sys::timespec {
        tv_sec: seconds,
        tv_nsec: i64::from(duration.subsec_nanos()),
    })
}

fn write_u64(fd: BorrowedFd<'_>, value: u64) -> io::Result<()> {
    let bytes = value.to_ne_bytes();
    loop {
        // SAFETY: `bytes` is readable for exactly eight bytes and the borrowed
        // descriptor remains valid for the call.
        let written = unsafe { gw_sys::write(fd.as_raw_fd(), bytes.as_ptr().cast(), bytes.len()) };
        if written == bytes.len() as isize {
            return Ok(());
        }
        if written >= 0 {
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                "event counter returned a short write",
            ));
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

pub(crate) fn read_u64(fd: BorrowedFd<'_>) -> io::Result<Option<u64>> {
    let mut bytes = [0_u8; size_of::<u64>()];
    loop {
        // SAFETY: `bytes` is writable for exactly eight bytes and the borrowed
        // descriptor remains valid for the call.
        let read = unsafe { gw_sys::read(fd.as_raw_fd(), bytes.as_mut_ptr().cast(), bytes.len()) };
        if read == bytes.len() as isize {
            return Ok(Some(u64::from_ne_bytes(bytes)));
        }
        if read >= 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "event counter returned a short read",
            ));
        }
        let error = io::Error::last_os_error();
        if error.kind() == io::ErrorKind::WouldBlock {
            return Ok(None);
        }
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
}

const fn size_of<T>() -> usize {
    core::mem::size_of::<T>()
}
