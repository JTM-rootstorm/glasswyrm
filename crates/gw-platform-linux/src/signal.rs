use core::marker::PhantomData;
use std::io;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd};
use std::rc::Rc;

use crate::HardenedFd;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Signal {
    Hangup,
    Interrupt,
    User1,
    User2,
    Child,
    Terminate,
}

impl Signal {
    const fn raw(self) -> i32 {
        match self {
            Self::Hangup => gw_sys::SIGHUP,
            Self::Interrupt => gw_sys::SIGINT,
            Self::User1 => gw_sys::SIGUSR1,
            Self::User2 => gw_sys::SIGUSR2,
            Self::Child => gw_sys::SIGCHLD,
            Self::Terminate => gw_sys::SIGTERM,
        }
    }

    fn from_raw(value: u32) -> Option<Self> {
        Some(match i32::try_from(value).ok()? {
            gw_sys::SIGHUP => Self::Hangup,
            gw_sys::SIGINT => Self::Interrupt,
            gw_sys::SIGUSR1 => Self::User1,
            gw_sys::SIGUSR2 => Self::User2,
            gw_sys::SIGCHLD => Self::Child,
            gw_sys::SIGTERM => Self::Terminate,
            _ => return None,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SignalEvent {
    pub signal: Signal,
    pub process_id: u32,
    pub user_id: u32,
}

/// A nonblocking signalfd bound to the thread that created it.
///
/// Construction blocks the selected signals in the current thread. Drop
/// restores that thread's previous mask, so this type is deliberately neither
/// `Send` nor `Sync`.
#[derive(Debug)]
pub struct SignalFd {
    fd: HardenedFd,
    previous_mask: gw_sys::sigset_t,
    _thread_bound: PhantomData<Rc<()>>,
}

impl SignalFd {
    pub fn new(signals: &[Signal]) -> io::Result<Self> {
        if signals.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "signalfd requires at least one signal",
            ));
        }
        let mut set = gw_sys::sigset_t::default();
        // SAFETY: `set` points to initialized sigset_t storage.
        if unsafe { gw_sys::sigemptyset(&raw mut set) } != 0 {
            return Err(io::Error::last_os_error());
        }
        for signal in signals {
            // SAFETY: `set` remains writable and Signal only exposes valid
            // catchable signal numbers.
            if unsafe { gw_sys::sigaddset(&raw mut set, signal.raw()) } != 0 {
                return Err(io::Error::last_os_error());
            }
        }

        let mut previous_mask = gw_sys::sigset_t::default();
        // SAFETY: both signal sets are valid storage. pthread_sigmask returns
        // an errno value directly and affects only the calling thread.
        let mask_error = unsafe {
            gw_sys::pthread_sigmask(gw_sys::SIG_BLOCK, &raw const set, &raw mut previous_mask)
        };
        if mask_error != 0 {
            return Err(io::Error::from_raw_os_error(mask_error));
        }

        // SAFETY: `set` stays live for the call and -1 asks for a fresh
        // signalfd using the selected nonblocking close-on-exec flags.
        let raw = unsafe {
            gw_sys::signalfd(
                -1,
                &raw const set,
                gw_sys::SFD_CLOEXEC | gw_sys::SFD_NONBLOCK,
            )
        };
        if raw < 0 {
            let error = io::Error::last_os_error();
            restore_mask(&previous_mask);
            return Err(error);
        }
        // SAFETY: a nonnegative signalfd result is freshly owned here.
        let owned = unsafe { OwnedFd::from_raw_fd(raw) };
        let fd = match HardenedFd::new(owned) {
            Ok(fd) => fd,
            Err(error) => {
                restore_mask(&previous_mask);
                return Err(error);
            }
        };
        Ok(Self {
            fd,
            previous_mask,
            _thread_bound: PhantomData,
        })
    }

    pub fn read_signal(&self) -> io::Result<Option<SignalEvent>> {
        let mut information = gw_sys::signalfd_siginfo::default();
        loop {
            // SAFETY: `information` is writable for exactly its structure size,
            // and the borrowed signalfd remains live during the call.
            let read = unsafe {
                gw_sys::read(
                    self.fd.as_raw_fd(),
                    (&raw mut information).cast(),
                    core::mem::size_of::<gw_sys::signalfd_siginfo>(),
                )
            };
            if read == core::mem::size_of::<gw_sys::signalfd_siginfo>() as isize {
                let signal = Signal::from_raw(information.ssi_signo).ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::InvalidData,
                        "signalfd returned an unrequested signal number",
                    )
                })?;
                return Ok(Some(SignalEvent {
                    signal,
                    process_id: information.ssi_pid,
                    user_id: information.ssi_uid,
                }));
            }
            if read >= 0 {
                return Err(io::Error::new(
                    io::ErrorKind::UnexpectedEof,
                    "signalfd returned a truncated record",
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
}

impl AsFd for SignalFd {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.fd.as_fd()
    }
}

impl Drop for SignalFd {
    fn drop(&mut self) {
        restore_mask(&self.previous_mask);
    }
}

fn restore_mask(mask: &gw_sys::sigset_t) {
    // SAFETY: the mask was produced by pthread_sigmask for this same
    // thread-bound object. Drop intentionally ignores restoration failure.
    let _ = unsafe { gw_sys::pthread_sigmask(gw_sys::SIG_SETMASK, mask, core::ptr::null_mut()) };
}
