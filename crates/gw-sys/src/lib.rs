//! Raw Linux ABI declarations used by Glasswyrm.
//!
//! This crate intentionally provides no safe API. Callers must validate raw
//! descriptors, pointers, lengths, and return values before exposing them to
//! the rest of the runtime.

#![cfg(target_os = "linux")]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_int, c_short, c_uint, c_void};

pub type nfds_t = usize;
pub type off_t = i64;
pub type pthread_t = usize;
pub type socklen_t = u32;
pub type time_t = i64;

pub const AF_UNIX: c_int = 1;

pub const SOCK_STREAM: c_int = 1;
pub const SOCK_SEQPACKET: c_int = 5;
pub const SOCK_CLOEXEC: c_int = 0o2_000_000;
pub const SOCK_NONBLOCK: c_int = 0o4_000;

pub const SOL_SOCKET: c_int = 1;
pub const SO_TYPE: c_int = 3;
pub const SO_PEERCRED: c_int = 17;

pub const F_GETFD: c_int = 1;
pub const F_SETFD: c_int = 2;
pub const F_GETFL: c_int = 3;
pub const F_SETFL: c_int = 4;
pub const F_ADD_SEALS: c_int = 1033;
pub const F_GET_SEALS: c_int = 1034;
pub const FD_CLOEXEC: c_int = 1;
pub const O_NONBLOCK: c_int = 0o4_000;

pub const S_IFMT: c_uint = 0o170_000;
pub const S_IFREG: c_uint = 0o100_000;

pub const F_SEAL_SEAL: c_int = 0x0001;
pub const F_SEAL_SHRINK: c_int = 0x0002;
pub const F_SEAL_GROW: c_int = 0x0004;
pub const F_SEAL_WRITE: c_int = 0x0008;

pub const MFD_CLOEXEC: c_uint = 0x0001;
pub const MFD_ALLOW_SEALING: c_uint = 0x0002;

pub const PROT_READ: c_int = 0x1;
pub const PROT_WRITE: c_int = 0x2;
pub const MAP_SHARED: c_int = 0x01;
pub const MAP_FAILED: *mut c_void = usize::MAX as *mut c_void;

pub const POLLIN: c_short = 0x001;
pub const POLLOUT: c_short = 0x004;
pub const POLLERR: c_short = 0x008;
pub const POLLHUP: c_short = 0x010;
pub const POLLNVAL: c_short = 0x020;

pub const EFD_CLOEXEC: c_int = SOCK_CLOEXEC;
pub const EFD_NONBLOCK: c_int = SOCK_NONBLOCK;
pub const TFD_CLOEXEC: c_int = SOCK_CLOEXEC;
pub const TFD_NONBLOCK: c_int = SOCK_NONBLOCK;
pub const SFD_CLOEXEC: c_int = SOCK_CLOEXEC;
pub const SFD_NONBLOCK: c_int = SOCK_NONBLOCK;
pub const CLOCK_MONOTONIC: c_int = 1;

pub const SIG_BLOCK: c_int = 0;
pub const SIG_SETMASK: c_int = 2;
pub const SIGHUP: c_int = 1;
pub const SIGINT: c_int = 2;
pub const SIGTERM: c_int = 15;
pub const SIGCHLD: c_int = 17;
pub const SIGUSR1: c_int = 10;
pub const SIGUSR2: c_int = 12;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct pollfd {
    pub fd: c_int,
    pub events: c_short,
    pub revents: c_short,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct timespec {
    pub tv_sec: time_t,
    pub tv_nsec: i64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct itimerspec {
    pub it_interval: timespec,
    pub it_value: timespec,
}

/// Linux x86_64 `struct stat` representation.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct stat {
    pub st_dev: u64,
    pub st_ino: u64,
    pub st_nlink: u64,
    pub st_mode: c_uint,
    pub st_uid: c_uint,
    pub st_gid: c_uint,
    pub __pad0: c_int,
    pub st_rdev: u64,
    pub st_size: off_t,
    pub st_blksize: i64,
    pub st_blocks: i64,
    pub st_atim: timespec,
    pub st_mtim: timespec,
    pub st_ctim: timespec,
    pub __glibc_reserved: [i64; 3],
}

/// glibc's Linux x86_64 `sigset_t` representation.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct sigset_t {
    pub words: [u64; 16],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct signalfd_siginfo {
    pub ssi_signo: u32,
    pub ssi_errno: i32,
    pub ssi_code: i32,
    pub ssi_pid: u32,
    pub ssi_uid: u32,
    pub ssi_fd: i32,
    pub ssi_tid: u32,
    pub ssi_band: u32,
    pub ssi_overrun: u32,
    pub ssi_trapno: u32,
    pub ssi_status: i32,
    pub ssi_int: i32,
    pub ssi_ptr: u64,
    pub ssi_utime: u64,
    pub ssi_stime: u64,
    pub ssi_addr: u64,
    pub ssi_addr_lsb: u16,
    pub pad2: u16,
    pub ssi_syscall: i32,
    pub ssi_call_addr: u64,
    pub ssi_arch: u32,
    pub pad: [u8; 28],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ucred {
    pub pid: i32,
    pub uid: u32,
    pub gid: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct sockaddr_un {
    pub sun_family: u16,
    pub sun_path: [c_char; 108],
}

unsafe extern "C" {
    pub fn accept4(
        socket: c_int,
        address: *mut c_void,
        address_len: *mut socklen_t,
        flags: c_int,
    ) -> c_int;
    #[link_name = "bind"]
    pub fn bind_socket(socket: c_int, address: *const c_void, address_len: socklen_t) -> c_int;
    pub fn connect(socket: c_int, address: *const c_void, address_len: socklen_t) -> c_int;
    pub fn eventfd(initial: c_uint, flags: c_int) -> c_int;
    pub fn fcntl(fd: c_int, command: c_int, ...) -> c_int;
    pub fn fstat(fd: c_int, status: *mut stat) -> c_int;
    pub fn ftruncate(fd: c_int, length: off_t) -> c_int;
    pub fn getegid() -> u32;
    pub fn geteuid() -> u32;
    pub fn getpid() -> i32;
    pub fn getsockopt(
        socket: c_int,
        level: c_int,
        option: c_int,
        value: *mut c_void,
        length: *mut socklen_t,
    ) -> c_int;
    pub fn listen(socket: c_int, backlog: c_int) -> c_int;
    pub fn memfd_create(name: *const c_char, flags: c_uint) -> c_int;
    pub fn mmap(
        address: *mut c_void,
        length: usize,
        protection: c_int,
        flags: c_int,
        fd: c_int,
        offset: off_t,
    ) -> *mut c_void;
    pub fn munmap(address: *mut c_void, length: usize) -> c_int;
    pub fn poll(descriptors: *mut pollfd, count: nfds_t, timeout_ms: c_int) -> c_int;
    pub fn pthread_kill(thread: pthread_t, signal: c_int) -> c_int;
    pub fn pthread_self() -> pthread_t;
    pub fn pthread_sigmask(how: c_int, set: *const sigset_t, old_set: *mut sigset_t) -> c_int;
    pub fn read(fd: c_int, buffer: *mut c_void, count: usize) -> isize;
    pub fn sigaddset(set: *mut sigset_t, signal: c_int) -> c_int;
    pub fn sigemptyset(set: *mut sigset_t) -> c_int;
    pub fn signalfd(fd: c_int, mask: *const sigset_t, flags: c_int) -> c_int;
    pub fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    pub fn socketpair(domain: c_int, kind: c_int, protocol: c_int, sockets: *mut c_int) -> c_int;
    pub fn timerfd_create(clock_id: c_int, flags: c_int) -> c_int;
    pub fn timerfd_settime(
        fd: c_int,
        flags: c_int,
        new_value: *const itimerspec,
        old_value: *mut itimerspec,
    ) -> c_int;
    pub fn write(fd: c_int, buffer: *const c_void, count: usize) -> isize;
}

const _: () = assert!(core::mem::size_of::<sigset_t>() == 128);
const _: () = assert!(core::mem::size_of::<signalfd_siginfo>() == 128);
const _: () = assert!(core::mem::size_of::<stat>() == 144);
const _: () = assert!(core::mem::offset_of!(stat, st_mode) == 24);
const _: () = assert!(core::mem::offset_of!(stat, st_size) == 48);
