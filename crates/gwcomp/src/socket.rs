use std::io;
use std::os::fd::OwnedFd;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

use gw_ipc::EndpointListener;

use core::ffi::c_int;

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
    // SAFETY: the handler has the signal ABI and performs only a lock-free
    // atomic store. SIGINT and SIGTERM are valid POSIX signal numbers.
    let results = unsafe {
        (
            signal(SIGINT, stop_handler as *const () as usize),
            signal(SIGTERM, stop_handler as *const () as usize),
        )
    };
    if results.0 == usize::MAX || results.1 == usize::MAX {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}
pub fn stop_requested() -> bool {
    STOP_REQUESTED.load(Ordering::Relaxed)
}

pub struct Listener {
    endpoint: EndpointListener,
}
impl Listener {
    pub fn bind(path: &Path) -> io::Result<Self> {
        EndpointListener::bind(path).map(|endpoint| Self { endpoint })
    }
    pub fn accept(&self) -> io::Result<Option<OwnedFd>> {
        self.endpoint.accept()
    }
}
