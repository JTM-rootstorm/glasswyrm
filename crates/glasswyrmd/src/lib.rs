//! Rust X11 setup and initial core-request process used while `glasswyrmd` migrates.

mod client;
mod listener;
pub mod options;
mod request_loop;
mod signals;

use glasswyrm_core::resource_id::{ResourceBase, first_available_resource_base};
use listener::OwnedListener;
use std::collections::HashSet;
use std::io;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

pub use options::Options;

const MAXIMUM_ACTIVE_CLIENTS: usize = 128;
const MAXIMUM_ACCEPTS_PER_TURN: usize = 64;
const SETUP_TIMEOUT: Duration = Duration::from_secs(5);

pub fn run(options: Options) -> io::Result<()> {
    signals::install()?;
    let socket_path = options.socket_path();
    let listener = OwnedListener::bind(&socket_path)?;
    eprintln!("glasswyrmd: listening on {}", socket_path.display());

    let resource_bases = Arc::new(Mutex::new(HashSet::new()));
    let mut next_client_identifier = 1_u64;
    while !signals::stop_requested() {
        for _ in 0..MAXIMUM_ACCEPTS_PER_TURN {
            match listener.accept() {
                Ok(Some(stream)) => {
                    let identifier = next_client_identifier;
                    next_client_identifier = next_client_identifier.saturating_add(1);
                    match start_client(
                        stream,
                        identifier,
                        Arc::clone(&resource_bases),
                        MAXIMUM_ACTIVE_CLIENTS,
                        SETUP_TIMEOUT,
                    ) {
                        Ok(()) => {
                            eprintln!("glasswyrmd: accepted client {identifier}");
                        }
                        Err(ClientStartError::ClientLimit) => {
                            eprintln!(
                                "glasswyrmd: active client limit of {MAXIMUM_ACTIVE_CLIENTS} reached"
                            );
                        }
                        Err(ClientStartError::ResourceIdsExhausted) => {
                            eprintln!("glasswyrmd: client resource-ID space exhausted");
                        }
                        Err(ClientStartError::Worker(error)) => {
                            eprintln!(
                                "glasswyrmd: client {identifier}: could not start worker: {error}"
                            );
                        }
                    }
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

#[derive(Debug)]
enum ClientStartError {
    ClientLimit,
    ResourceIdsExhausted,
    Worker(io::Error),
}

fn start_client(
    stream: std::os::unix::net::UnixStream,
    identifier: u64,
    resource_bases: Arc<Mutex<HashSet<ResourceBase>>>,
    maximum_active_clients: usize,
    setup_timeout: Duration,
) -> Result<(), ClientStartError> {
    let lease =
        ResourceBaseLease::allocate(resource_bases, maximum_active_clients).map_err(|error| {
            match error {
                LeaseAllocationError::ClientLimit => ClientStartError::ClientLimit,
                LeaseAllocationError::ResourceIdsExhausted => {
                    ClientStartError::ResourceIdsExhausted
                }
            }
        })?;
    thread::Builder::new()
        .name(format!("glasswyrmd-client-{identifier}"))
        .spawn(move || client::serve(stream, identifier, lease, setup_timeout))
        .map(|_| ())
        .map_err(ClientStartError::Worker)
}

struct ResourceBaseLease {
    base: ResourceBase,
    in_use: Arc<Mutex<HashSet<ResourceBase>>>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LeaseAllocationError {
    ClientLimit,
    ResourceIdsExhausted,
}

impl ResourceBaseLease {
    fn allocate(
        in_use: Arc<Mutex<HashSet<ResourceBase>>>,
        maximum_active_clients: usize,
    ) -> Result<Self, LeaseAllocationError> {
        let mut guard = in_use
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if guard.len() >= maximum_active_clients {
            return Err(LeaseAllocationError::ClientLimit);
        }
        let base = first_available_resource_base(|candidate| guard.contains(&candidate))
            .ok_or(LeaseAllocationError::ResourceIdsExhausted)?;
        guard.insert(base);
        drop(guard);
        Ok(Self { base, in_use })
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;
    use std::os::unix::net::UnixStream;
    use std::time::{Duration, Instant};

    fn wait_for_active_clients(in_use: &Mutex<HashSet<ResourceBase>>, expected: usize) {
        let deadline = Instant::now() + Duration::from_secs(1);
        loop {
            let count = in_use
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .len();
            if count == expected {
                return;
            }
            assert!(
                Instant::now() < deadline,
                "active-client count did not become {expected}"
            );
            thread::sleep(Duration::from_millis(1));
        }
    }

    #[test]
    fn client_admission_is_bounded_and_reuses_released_slots() {
        let in_use = Arc::new(Mutex::new(HashSet::new()));
        let first = ResourceBaseLease::allocate(Arc::clone(&in_use), 2).unwrap();
        let first_base = first.base();
        let second = ResourceBaseLease::allocate(Arc::clone(&in_use), 2).unwrap();
        assert_ne!(first_base, second.base());
        assert!(matches!(
            ResourceBaseLease::allocate(Arc::clone(&in_use), 2),
            Err(LeaseAllocationError::ClientLimit)
        ));

        drop(first);
        let replacement = ResourceBaseLease::allocate(Arc::clone(&in_use), 2).unwrap();
        assert_eq!(replacement.base(), first_base);
    }

    #[test]
    fn zero_client_limit_rejects_before_allocating_a_resource_base() {
        let in_use = Arc::new(Mutex::new(HashSet::new()));
        assert!(matches!(
            ResourceBaseLease::allocate(Arc::clone(&in_use), 0),
            Err(LeaseAllocationError::ClientLimit)
        ));
        assert!(in_use.lock().unwrap().is_empty());
    }

    #[test]
    fn stalled_setup_client_cannot_exceed_limit_or_stop_later_admission() {
        let in_use = Arc::new(Mutex::new(HashSet::new()));
        let (first_server, first_client) = UnixStream::pair().unwrap();
        start_client(first_server, 1, Arc::clone(&in_use), 1, SETUP_TIMEOUT).unwrap();
        wait_for_active_clients(&in_use, 1);

        let (second_server, mut second_client) = UnixStream::pair().unwrap();
        assert!(matches!(
            start_client(second_server, 2, Arc::clone(&in_use), 1, SETUP_TIMEOUT),
            Err(ClientStartError::ClientLimit)
        ));
        second_client.set_nonblocking(true).unwrap();
        let close_deadline = Instant::now() + Duration::from_secs(1);
        loop {
            match second_client.read(&mut [0_u8; 1]) {
                Ok(0) => break,
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                    assert!(
                        Instant::now() < close_deadline,
                        "rejected client socket did not close"
                    );
                    thread::sleep(Duration::from_millis(1));
                }
                result => panic!("unexpected rejected-client read result: {result:?}"),
            }
        }

        drop(first_client);
        wait_for_active_clients(&in_use, 0);

        let (third_server, third_client) = UnixStream::pair().unwrap();
        start_client(third_server, 3, Arc::clone(&in_use), 1, SETUP_TIMEOUT).unwrap();
        wait_for_active_clients(&in_use, 1);
        drop(third_client);
        wait_for_active_clients(&in_use, 0);
    }

    #[test]
    fn stalled_setup_releases_its_slot_at_the_injected_deadline() {
        let in_use = Arc::new(Mutex::new(HashSet::new()));
        let (server, client) = UnixStream::pair().unwrap();
        start_client(server, 1, Arc::clone(&in_use), 1, Duration::ZERO).unwrap();

        wait_for_active_clients(&in_use, 0);
        drop(client);

        let (replacement_server, replacement_client) = UnixStream::pair().unwrap();
        start_client(
            replacement_server,
            2,
            Arc::clone(&in_use),
            1,
            Duration::ZERO,
        )
        .unwrap();
        wait_for_active_clients(&in_use, 0);
        drop(replacement_client);
    }
}
