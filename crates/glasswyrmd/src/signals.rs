use core::ffi::c_int;
use std::io;
use std::sync::atomic::{AtomicBool, Ordering};

const SIGINT: c_int = 2;
const SIGPIPE: c_int = 13;
const SIGTERM: c_int = 15;
const SIG_IGN: usize = 1;
const SIG_ERR: usize = usize::MAX;

static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

unsafe extern "C" {
    fn signal(signal: c_int, handler: usize) -> usize;
}

extern "C" fn request_stop(_signal: c_int) {
    STOP_REQUESTED.store(true, Ordering::Relaxed);
}

pub(crate) fn install() -> io::Result<()> {
    STOP_REQUESTED.store(false, Ordering::Relaxed);
    let handler = request_stop as *const () as usize;
    // SAFETY: the handler has the C signal-handler ABI and only records an
    // atomic stop flag. Ignoring SIGPIPE prevents peer disconnects from
    // terminating the daemon while a setup reply is written.
    let installed = unsafe {
        signal(SIGINT, handler) != SIG_ERR
            && signal(SIGTERM, handler) != SIG_ERR
            && signal(SIGPIPE, SIG_IGN) != SIG_ERR
    };
    if installed {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

pub(crate) fn stop_requested() -> bool {
    STOP_REQUESTED.load(Ordering::Relaxed)
}
