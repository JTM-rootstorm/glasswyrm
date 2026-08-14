use std::io;
use std::os::fd::{AsFd, AsRawFd, OwnedFd};
use std::os::unix::net::UnixStream;
use std::time::Duration;

use gw_platform_linux::{
    EventFd, HardenedFd, MapAccess, Memfd, PollInterest, PollTarget, SealSet, Signal, SignalFd,
    SignalTokenError, TimerFd, UnixSocket, UnixSocketType, descriptor_flags, peer_credentials,
    poll,
};

const EPERM: i32 = 1;

fn skip_sandbox_eperm<T>(operation: &str, result: io::Result<T>) -> Option<T> {
    match result {
        Ok(value) => Some(value),
        Err(error) if error.raw_os_error() == Some(EPERM) => {
            eprintln!(
                "skipping {operation}: the managed sandbox denied the Linux primitive with EPERM"
            );
            None
        }
        Err(error) => panic!("{operation} failed: {error}"),
    }
}

#[test]
fn adopting_an_owned_fd_enables_cloexec_and_nonblocking() {
    let (stream, _peer) = UnixStream::pair().unwrap();
    stream.set_nonblocking(false).unwrap();
    let owned: OwnedFd = stream.into();
    let hardened = HardenedFd::new(owned).unwrap();
    let flags = descriptor_flags(hardened.as_fd()).unwrap();
    assert!(flags.close_on_exec);
    assert!(flags.nonblocking);
}

#[test]
fn memfd_mapping_round_trips_and_applies_seals() {
    let Some(memfd) = skip_sandbox_eperm("memfd_create", Memfd::create("gw-platform-test", 4096))
    else {
        return;
    };
    {
        let mut mapping = memfd.map(4096, MapAccess::ReadWrite).unwrap();
        mapping.as_mut_slice().unwrap()[..4].copy_from_slice(b"wyrm");
        assert_eq!(&mapping.as_slice()[..4], b"wyrm");
    }
    let seals = SealSet::SHRINK | SealSet::GROW | SealSet::WRITE | SealSet::SEAL;
    memfd.add_seals(seals).unwrap();
    assert!(memfd.seals().unwrap().contains(seals));
    let mapping = memfd.map(4096, MapAccess::ReadOnly).unwrap();
    assert_eq!(&mapping.as_slice()[..4], b"wyrm");
}

#[test]
fn eventfd_enforces_one_exact_readiness_token() {
    let Some(event) = skip_sandbox_eperm("eventfd", EventFd::new()) else {
        return;
    };
    assert!(!event.consume().unwrap());
    event.signal().unwrap();
    assert!(event.consume().unwrap());
    assert!(!event.consume().unwrap());

    event.signal().unwrap();
    event.signal().unwrap();
    assert!(matches!(
        event.consume(),
        Err(SignalTokenError::UnexpectedToken(2))
    ));
}

#[test]
fn poll_reports_socket_readiness_for_stream_and_seqpacket() {
    for socket_type in [UnixSocketType::Stream, UnixSocketType::SeqPacket] {
        let (sender, receiver) = UnixSocket::pair(socket_type).unwrap();
        let byte = [0x5a_u8];
        // SAFETY: `byte` is readable for one byte and sender owns a connected
        // socket descriptor for the duration of the call.
        let written =
            unsafe { gw_sys::write(sender.as_fd().as_raw_fd(), byte.as_ptr().cast(), byte.len()) };
        assert_eq!(written, 1);
        let mut targets = [PollTarget::new(receiver.as_fd(), PollInterest::READABLE)];
        assert_eq!(
            poll(&mut targets, Some(Duration::from_millis(100))).unwrap(),
            1
        );
        assert!(targets[0].events().is_readable());
        assert!(!targets[0].events().is_invalid());
    }
}

#[test]
fn peer_credentials_match_the_local_process() {
    let (_first, second) = UnixSocket::pair(UnixSocketType::SeqPacket).unwrap();
    let Some(credentials) = skip_sandbox_eperm("SO_PEERCRED", peer_credentials(second.as_fd()))
    else {
        return;
    };
    // SAFETY: these identity queries have no preconditions.
    let (pid, uid, gid) = unsafe { (gw_sys::getpid(), gw_sys::geteuid(), gw_sys::getegid()) };
    assert_eq!(credentials.process_id, pid);
    assert_eq!(credentials.user_id, uid);
    assert_eq!(credentials.group_id, gid);
}

#[test]
fn timerfd_becomes_readable_and_returns_expirations() {
    let Some(timer) = skip_sandbox_eperm("timerfd_create", TimerFd::new()) else {
        return;
    };
    assert_eq!(timer.read_expirations().unwrap(), None);
    timer.arm_once(Duration::from_millis(2)).unwrap();
    let mut targets = [PollTarget::new(timer.as_fd(), PollInterest::READABLE)];
    assert_eq!(poll(&mut targets, Some(Duration::from_secs(1))).unwrap(), 1);
    assert!(targets[0].events().is_readable());
    assert!(
        timer
            .read_expirations()
            .unwrap()
            .is_some_and(|count| count >= 1)
    );
    timer.disarm().unwrap();
}

#[test]
fn signalfd_receives_a_thread_directed_signal() {
    let Some(signals) = skip_sandbox_eperm("signalfd", SignalFd::new(&[Signal::User1])) else {
        return;
    };
    // SAFETY: pthread_self returns the current valid thread identifier and
    // SIGUSR1 is blocked and selected by `signals` in this same thread.
    let result = unsafe { gw_sys::pthread_kill(gw_sys::pthread_self(), gw_sys::SIGUSR1) };
    assert_eq!(result, 0);

    let mut targets = [PollTarget::new(signals.as_fd(), PollInterest::READABLE)];
    assert_eq!(poll(&mut targets, Some(Duration::from_secs(1))).unwrap(), 1);
    let event = signals.read_signal().unwrap().unwrap();
    assert_eq!(event.signal, Signal::User1);
    // pthread-directed signal metadata is supplied by the kernel; pid is the
    // sending process while uid may be zero for SI_TKILL on some kernels.
    assert_eq!(event.process_id, std::process::id());
}
