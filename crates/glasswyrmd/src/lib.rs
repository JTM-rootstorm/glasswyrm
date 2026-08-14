//! Setup-only Rust process shell used while `glasswyrmd` migrates.

mod client;
mod listener;
pub mod options;
mod signals;

use glasswyrm_core::resource_id::{ResourceBase, first_available_resource_base};
use listener::OwnedListener;
use std::collections::HashSet;
use std::io;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

pub use options::Options;

pub fn run(options: Options) -> io::Result<()> {
    signals::install()?;
    let socket_path = options.socket_path();
    let listener = OwnedListener::bind(&socket_path)?;
    eprintln!("glasswyrmd: listening on {}", socket_path.display());

    let resource_bases = Arc::new(Mutex::new(HashSet::new()));
    let mut next_client_identifier = 1_u64;
    while !signals::stop_requested() {
        loop {
            match listener.accept() {
                Ok(Some(stream)) => {
                    let Some(lease) = ResourceBaseLease::allocate(Arc::clone(&resource_bases))
                    else {
                        eprintln!("glasswyrmd: client resource-ID space exhausted");
                        continue;
                    };
                    let identifier = next_client_identifier;
                    next_client_identifier = next_client_identifier.saturating_add(1);
                    eprintln!("glasswyrmd: accepted client {identifier}");
                    thread::spawn(move || client::serve(stream, identifier, lease));
                }
                Ok(None) => break,
                Err(error) if error.kind() == io::ErrorKind::Interrupted => {}
                Err(error) => return Err(error),
            }
        }
        thread::sleep(Duration::from_millis(1));
    }
    drop(listener);
    Ok(())
}

struct ResourceBaseLease {
    base: ResourceBase,
    in_use: Arc<Mutex<HashSet<ResourceBase>>>,
}

impl ResourceBaseLease {
    fn allocate(in_use: Arc<Mutex<HashSet<ResourceBase>>>) -> Option<Self> {
        let mut guard = in_use
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let base = first_available_resource_base(|candidate| guard.contains(&candidate))?;
        guard.insert(base);
        drop(guard);
        Some(Self { base, in_use })
    }

    const fn base(&self) -> ResourceBase {
        self.base
    }
}

impl Drop for ResourceBaseLease {
    fn drop(&mut self) {
        self.in_use
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .remove(&self.base);
    }
}
