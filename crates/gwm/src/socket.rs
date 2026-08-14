use core::ffi::c_int;
use std::io;
use std::sync::atomic::{AtomicBool, Ordering};

const SIGINT: c_int = 2;
const SIGTERM: c_int = 15;

static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

unsafe extern "C" {
    fn signal(number: c_int, handler: usize) -> usize;
}

extern "C" fn stop_handler(_signal: c_int) {
    STOP_REQUESTED.store(true, Ordering::Relaxed);
}

pub fn install_signal_handlers() -> io::Result<()> {
    STOP_REQUESTED.store(false, Ordering::Relaxed);
    // SAFETY: `stop_handler` has the required C signal-handler ABI and only
    // performs a lock-free atomic store. SIGINT and SIGTERM are valid signals.
    let (interrupt, terminate) = unsafe {
        (
            signal(SIGINT, stop_handler as *const () as usize),
            signal(SIGTERM, stop_handler as *const () as usize),
        )
    };
    if interrupt == usize::MAX || terminate == usize::MAX {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

pub fn stop_requested() -> bool {
    STOP_REQUESTED.load(Ordering::Relaxed)
}
