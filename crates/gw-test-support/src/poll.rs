use std::cell::Cell;
use std::error::Error;
use std::fmt;
use std::io;
use std::path::Path;
use std::thread;
use std::time::{Duration, Instant};

pub trait Clock {
    fn now(&self) -> Duration;
    fn sleep(&self, duration: Duration);
}

#[derive(Clone, Copy, Debug)]
pub struct SystemClock {
    origin: Instant,
}

impl SystemClock {
    pub fn new() -> Self {
        Self {
            origin: Instant::now(),
        }
    }
}

impl Default for SystemClock {
    fn default() -> Self {
        Self::new()
    }
}

impl Clock for SystemClock {
    fn now(&self) -> Duration {
        self.origin.elapsed()
    }

    fn sleep(&self, duration: Duration) {
        thread::sleep(duration);
    }
}

/// A deterministic single-threaded clock for readiness-policy unit tests.
#[derive(Debug, Default)]
pub struct FakeClock {
    elapsed: Cell<Duration>,
}

impl FakeClock {
    pub fn elapsed(&self) -> Duration {
        self.elapsed.get()
    }
}

impl Clock for FakeClock {
    fn now(&self) -> Duration {
        self.elapsed.get()
    }

    fn sleep(&self, duration: Duration) {
        self.elapsed
            .set(self.elapsed.get().saturating_add(duration));
    }
}

#[derive(Debug)]
pub struct PollError {
    description: String,
    timeout: Duration,
    attempts: u64,
    last_error: Option<io::Error>,
}

impl PollError {
    pub fn description(&self) -> &str {
        &self.description
    }

    pub fn timeout(&self) -> Duration {
        self.timeout
    }

    pub fn attempts(&self) -> u64 {
        self.attempts
    }

    pub fn last_error(&self) -> Option<&io::Error> {
        self.last_error.as_ref()
    }
}

impl fmt::Display for PollError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "timed out after {:?} waiting for {} ({} attempts)",
            self.timeout, self.description, self.attempts
        )?;
        if let Some(error) = &self.last_error {
            write!(formatter, "; last observation error: {error}")?;
        }
        Ok(())
    }
}

impl Error for PollError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        self.last_error
            .as_ref()
            .map(|error| error as &(dyn Error + 'static))
    }
}

pub fn poll_until<T, F>(
    description: impl Into<String>,
    timeout: Duration,
    interval: Duration,
    operation: F,
) -> Result<T, PollError>
where
    F: FnMut() -> io::Result<Option<T>>,
{
    poll_until_with_clock(
        &SystemClock::new(),
        description,
        timeout,
        interval,
        operation,
    )
}

pub fn poll_until_with_clock<T, F, C>(
    clock: &C,
    description: impl Into<String>,
    timeout: Duration,
    interval: Duration,
    mut operation: F,
) -> Result<T, PollError>
where
    F: FnMut() -> io::Result<Option<T>>,
    C: Clock,
{
    let description = description.into();
    let start = clock.now();
    let mut attempts = 0_u64;
    loop {
        attempts += 1;
        let last_error = match operation() {
            Ok(Some(value)) => return Ok(value),
            Ok(None) => None,
            Err(error) => Some(error),
        };

        let elapsed = clock.now().saturating_sub(start);
        if elapsed >= timeout {
            return Err(PollError {
                description,
                timeout,
                attempts,
                last_error,
            });
        }
        clock.sleep(interval.min(timeout - elapsed));
    }
}

pub fn wait_for_path(
    path: impl AsRef<Path>,
    timeout: Duration,
    interval: Duration,
) -> Result<(), PollError> {
    let path = path.as_ref().to_path_buf();
    poll_until(
        format!("path {}", path.display()),
        timeout,
        interval,
        || Ok(path.exists().then_some(())),
    )
}

#[cfg(unix)]
/// Waits for a `SOCK_STREAM` Unix listener to accept connections.
///
/// GWIPC uses `SOCK_SEQPACKET`; its readiness must be established with a
/// protocol-aware seqpacket connector and handshake instead of this helper.
pub fn wait_for_unix_stream_socket(
    path: impl AsRef<Path>,
    timeout: Duration,
    interval: Duration,
) -> Result<(), PollError> {
    use std::os::unix::net::UnixStream;

    let path = path.as_ref().to_path_buf();
    poll_until(
        format!("connectable Unix socket {}", path.display()),
        timeout,
        interval,
        || UnixStream::connect(&path).map(|_| Some(())),
    )
}

#[cfg(not(unix))]
pub fn wait_for_unix_stream_socket(
    path: impl AsRef<Path>,
    timeout: Duration,
    _interval: Duration,
) -> Result<(), PollError> {
    Err(PollError {
        description: format!("Unix socket {} on a non-Unix host", path.as_ref().display()),
        timeout,
        attempts: 0,
        last_error: Some(io::Error::new(
            io::ErrorKind::Unsupported,
            "Unix sockets are unsupported on this host",
        )),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fake_clock_makes_polling_deterministic() {
        let clock = FakeClock::default();
        let mut attempts = 0;
        let result = poll_until_with_clock(
            &clock,
            "third observation",
            Duration::from_secs(10),
            Duration::from_secs(2),
            || {
                attempts += 1;
                Ok((attempts == 3).then_some(42))
            },
        );
        assert_eq!(result.unwrap(), 42);
        assert_eq!(clock.elapsed(), Duration::from_secs(4));
    }

    #[test]
    fn timeout_includes_the_last_diagnostic() {
        let clock = FakeClock::default();
        let error = poll_until_with_clock::<(), _, _>(
            &clock,
            "peer hello",
            Duration::from_millis(5),
            Duration::from_millis(2),
            || {
                Err(io::Error::new(
                    io::ErrorKind::ConnectionRefused,
                    "not ready",
                ))
            },
        )
        .unwrap_err();
        assert_eq!(error.attempts(), 4);
        assert!(error.to_string().contains("not ready"));
        assert_eq!(clock.elapsed(), Duration::from_millis(5));
    }

    #[cfg(unix)]
    #[test]
    fn unix_stream_socket_readiness_requires_a_connection() {
        use std::os::unix::net::UnixListener;

        let path = std::env::temp_dir().join(format!(
            "gw-test-support-socket-{}-{}",
            std::process::id(),
            deterministic_suffix()
        ));
        let listener = match UnixListener::bind(&path) {
            Ok(listener) => listener,
            Err(error)
                if matches!(
                    error.kind(),
                    io::ErrorKind::PermissionDenied | io::ErrorKind::Unsupported
                ) =>
            {
                // Some managed test sandboxes prohibit AF_UNIX even in /tmp.
                return;
            }
            Err(error) => panic!("could not bind test socket: {error}"),
        };
        wait_for_unix_stream_socket(&path, Duration::from_millis(100), Duration::from_millis(1))
            .unwrap();
        drop(listener);
        std::fs::remove_file(path).unwrap();
    }

    #[cfg(unix)]
    fn deterministic_suffix() -> &'static str {
        "ready"
    }
}
