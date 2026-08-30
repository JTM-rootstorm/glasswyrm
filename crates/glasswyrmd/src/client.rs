use crate::request_loop::{RequestWorkBudget, ServerState};
use crate::session::{ClientSession, SetupProgress};
use crate::{ResourceBaseLease, StopCheck};
use std::io::Read;
use std::net::Shutdown;
use std::os::unix::net::UnixStream;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

const CANCELLATION_POLL_INTERVAL: Duration = Duration::from_millis(10);

pub(crate) fn serve(
    mut stream: UnixStream,
    identifier: u64,
    resource_base: ResourceBaseLease,
    setup_timeout: Duration,
    server_state: Arc<Mutex<ServerState>>,
    stop: StopCheck,
) {
    let mut session = ClientSession::new(identifier, resource_base.base(), server_state);
    let mut input = [0_u8; 4096];
    let setup_deadline = SetupDeadline::new(Instant::now(), setup_timeout);
    while session.is_setup() {
        if stop() {
            session.close_now();
            return;
        }
        let Some(remaining) = setup_deadline.remaining(Instant::now()) else {
            eprintln!("glasswyrmd: client {identifier}: X11 setup timed out");
            return;
        };
        if let Err(error) = stream.set_read_timeout(Some(remaining.min(CANCELLATION_POLL_INTERVAL)))
        {
            eprintln!(
                "glasswyrmd: client {identifier}: could not configure setup deadline: {error}"
            );
            return;
        }
        let count = match stream.read(&mut input) {
            Ok(0) => return,
            Ok(count) => count,
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error)
                if error.kind() == std::io::ErrorKind::TimedOut
                    || error.kind() == std::io::ErrorKind::WouldBlock =>
            {
                continue;
            }
            Err(error) => {
                eprintln!("glasswyrmd: client {identifier}: setup read failed: {error}");
                return;
            }
        };
        let progress = session.feed_setup(&input[..count], &mut RequestWorkBudget::default());
        match progress {
            SetupProgress::NeedMore => {}
            SetupProgress::Accepted { order } => {
                eprintln!(
                    "glasswyrmd: client {identifier}: X11 setup accepted byte_order={}",
                    char::from(order.marker())
                );
            }
            SetupProgress::Rejected => {
                if let Err(error) = session.write_output(&mut stream) {
                    eprintln!("glasswyrmd: client {identifier}: setup reply failed: {error}");
                }
                let _ = stream.shutdown(Shutdown::Both);
                return;
            }
            SetupProgress::InvalidByteOrder => {
                eprintln!("glasswyrmd: client {identifier}: invalid X11 byte-order marker");
                return;
            }
            SetupProgress::MessageTooLarge => {
                eprintln!(
                    "glasswyrmd: client {identifier}: X11 setup message exceeds configured limit"
                );
                return;
            }
            SetupProgress::LengthOverflow => {
                eprintln!("glasswyrmd: client {identifier}: X11 setup message length overflow");
                return;
            }
            SetupProgress::TruncatedInput => return,
        }
    }

    if !session.is_established() {
        return;
    }
    if let Err(error) = stream.set_read_timeout(None) {
        eprintln!("glasswyrmd: client {identifier}: could not clear setup deadline: {error}");
        return;
    }
    serve_established(&mut stream, identifier, &mut session, &stop);
}

#[derive(Clone, Copy, Debug)]
struct SetupDeadline {
    deadline: Instant,
}

impl SetupDeadline {
    fn new(started_at: Instant, timeout: Duration) -> Self {
        Self {
            deadline: started_at.checked_add(timeout).unwrap_or(started_at),
        }
    }

    fn remaining(self, now: Instant) -> Option<Duration> {
        let remaining = self.deadline.checked_duration_since(now)?;
        (!remaining.is_zero()).then_some(remaining)
    }
}

fn serve_established(
    stream: &mut UnixStream,
    identifier: u64,
    session: &mut ClientSession,
    stop: &StopCheck,
) {
    if let Err(error) = stream.set_nonblocking(true) {
        eprintln!("glasswyrmd: client {identifier}: nonblocking setup failed: {error}");
        return;
    }

    while !session.is_closed() {
        if stop() {
            session.close_now();
            break;
        }
        let mut progressed = false;
        let mut budget = RequestWorkBudget::default();
        progressed |= session.process_pending(&mut budget) != 0;

        if session.accepts_input() && budget.available() {
            while session.accepts_input() && budget.available() && !session.has_pending_input() {
                let mut input = [0_u8; 4096];
                match stream.read(&mut input) {
                    Ok(0) => {
                        session.end_of_input();
                        progressed = true;
                        break;
                    }
                    Ok(count) => {
                        progressed = true;
                        session.feed_established(&input[..count], &mut budget);
                    }
                    Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => break,
                    Err(error) => {
                        eprintln!("glasswyrmd: client {identifier}: request read failed: {error}");
                        session.close_now();
                        break;
                    }
                }
            }
        }

        match session.write_output(stream) {
            Ok(wrote) => progressed |= wrote,
            Err(error) => {
                eprintln!("glasswyrmd: client {identifier}: response write failed: {error}");
                session.close_now();
            }
        }

        if !progressed && !session.is_closed() {
            thread::sleep(Duration::from_millis(1));
        }
    }
    let _ = stream.shutdown(Shutdown::Both);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn setup_deadline_is_absolute_across_incremental_input() {
        let started_at = Instant::now();
        let deadline = SetupDeadline::new(started_at, Duration::from_millis(20));

        assert_eq!(
            deadline.remaining(started_at + Duration::from_millis(5)),
            Some(Duration::from_millis(15))
        );
        assert_eq!(
            deadline.remaining(started_at + Duration::from_millis(19)),
            Some(Duration::from_millis(1))
        );
        assert_eq!(
            deadline.remaining(started_at + Duration::from_millis(20)),
            None
        );
    }
}
