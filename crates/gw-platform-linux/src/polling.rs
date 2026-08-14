use core::ops::{BitOr, BitOrAssign};
use std::io;
use std::os::fd::{AsRawFd, BorrowedFd};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PollInterest(i16);

impl PollInterest {
    pub const READABLE: Self = Self(gw_sys::POLLIN);
    pub const WRITABLE: Self = Self(gw_sys::POLLOUT);
}

impl BitOr for PollInterest {
    type Output = Self;

    fn bitor(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

impl BitOrAssign for PollInterest {
    fn bitor_assign(&mut self, other: Self) {
        self.0 |= other.0;
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PollEvents(i16);

impl PollEvents {
    #[must_use]
    pub const fn is_readable(self) -> bool {
        self.0 & gw_sys::POLLIN != 0
    }

    #[must_use]
    pub const fn is_writable(self) -> bool {
        self.0 & gw_sys::POLLOUT != 0
    }

    #[must_use]
    pub const fn has_error(self) -> bool {
        self.0 & gw_sys::POLLERR != 0
    }

    #[must_use]
    pub const fn is_hung_up(self) -> bool {
        self.0 & gw_sys::POLLHUP != 0
    }

    #[must_use]
    pub const fn is_invalid(self) -> bool {
        self.0 & gw_sys::POLLNVAL != 0
    }
}

#[derive(Debug)]
pub struct PollTarget<'fd> {
    fd: BorrowedFd<'fd>,
    interests: PollInterest,
    events: PollEvents,
}

impl<'fd> PollTarget<'fd> {
    #[must_use]
    pub const fn new(fd: BorrowedFd<'fd>, interests: PollInterest) -> Self {
        Self {
            fd,
            interests,
            events: PollEvents(0),
        }
    }

    #[must_use]
    pub const fn events(&self) -> PollEvents {
        self.events
    }
}

/// Polls the supplied descriptors, retrying interrupted waits without extending
/// a finite timeout.
pub fn poll(targets: &mut [PollTarget<'_>], timeout: Option<Duration>) -> io::Result<usize> {
    let deadline = timeout.and_then(|duration| Instant::now().checked_add(duration));
    let mut descriptors = targets
        .iter()
        .map(|target| gw_sys::pollfd {
            fd: target.fd.as_raw_fd(),
            events: target.interests.0,
            revents: 0,
        })
        .collect::<Vec<_>>();

    loop {
        let milliseconds = match (timeout, deadline) {
            (None, _) => -1,
            (Some(duration), None) => duration_to_milliseconds(duration),
            (Some(_), Some(deadline)) => {
                duration_to_milliseconds(deadline.saturating_duration_since(Instant::now()))
            }
        };
        // SAFETY: `descriptors` is writable contiguous storage for exactly
        // `descriptors.len()` pollfd values and remains alive for the call.
        let ready =
            unsafe { gw_sys::poll(descriptors.as_mut_ptr(), descriptors.len(), milliseconds) };
        if ready >= 0 {
            for (target, descriptor) in targets.iter_mut().zip(&descriptors) {
                target.events = PollEvents(descriptor.revents);
            }
            return usize::try_from(ready).map_err(io::Error::other);
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
        if deadline.is_some_and(|deadline| Instant::now() >= deadline) {
            for target in targets {
                target.events = PollEvents(0);
            }
            return Ok(0);
        }
    }
}

fn duration_to_milliseconds(duration: Duration) -> i32 {
    if duration.is_zero() {
        return 0;
    }
    duration
        .as_nanos()
        .div_ceil(1_000_000)
        .min(i32::MAX as u128) as i32
}
