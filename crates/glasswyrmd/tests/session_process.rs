use std::fs;
use std::io::{self, Read, Write};
use std::net::Shutdown;
use std::os::unix::fs::DirBuilderExt;
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::thread;
use std::time::{Duration, Instant, SystemTime};

#[derive(Clone, Copy)]
enum Order {
    Little,
    Big,
}

impl Order {
    fn marker(self) -> u8 {
        match self {
            Self::Little => b'l',
            Self::Big => b'B',
        }
    }

    fn u16(self, bytes: [u8; 2]) -> u16 {
        match self {
            Self::Little => u16::from_le_bytes(bytes),
            Self::Big => u16::from_be_bytes(bytes),
        }
    }

    fn u16_bytes(self, value: u16) -> [u8; 2] {
        match self {
            Self::Little => value.to_le_bytes(),
            Self::Big => value.to_be_bytes(),
        }
    }

    fn u32(self, bytes: [u8; 4]) -> u32 {
        match self {
            Self::Little => u32::from_le_bytes(bytes),
            Self::Big => u32::from_be_bytes(bytes),
        }
    }
}

struct Server {
    child: Child,
    directory: PathBuf,
    socket: PathBuf,
}

impl Server {
    fn start() -> Self {
        let nonce = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .expect("system clock follows the Unix epoch")
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("gw-s-{}-{nonce}", std::process::id()));
        let mut builder = fs::DirBuilder::new();
        builder.mode(0o700);
        builder
            .create(&directory)
            .expect("create private process-test directory");
        let socket = directory.join("X0");
        let child = Command::new(env!("CARGO_BIN_EXE_glasswyrmd"))
            .args(["--display", "0", "--socket-dir"])
            .arg(&directory)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .expect("start Rust glasswyrmd");
        let server = Self {
            child,
            directory,
            socket,
        };
        server.wait_until_ready();
        server
    }

    fn wait_until_ready(&self) {
        let deadline = Instant::now() + Duration::from_secs(2);
        while !self.socket.exists() {
            assert!(
                Instant::now() < deadline,
                "glasswyrmd socket did not appear"
            );
            thread::sleep(Duration::from_millis(1));
        }
    }

    fn raw_connect(&self) -> UnixStream {
        let stream = UnixStream::connect(&self.socket).expect("connect to Rust glasswyrmd");
        stream
            .set_read_timeout(Some(Duration::from_secs(2)))
            .expect("set process-test read timeout");
        stream
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
        let _ = fs::remove_file(&self.socket);
        let _ = fs::remove_dir(&self.directory);
    }
}

fn setup(order: Order, major: u16, minor: u16) -> Vec<u8> {
    let mut bytes = vec![order.marker(), 0];
    bytes.extend_from_slice(&order.u16_bytes(major));
    bytes.extend_from_slice(&order.u16_bytes(minor));
    bytes.extend_from_slice(&order.u16_bytes(0));
    bytes.extend_from_slice(&order.u16_bytes(0));
    bytes.extend_from_slice(&[0, 0]);
    bytes
}

fn request(order: Order, opcode: u8) -> [u8; 4] {
    let mut bytes = [opcode, 0, 0, 0];
    bytes[2..4].copy_from_slice(&order.u16_bytes(1));
    bytes
}

fn write_chunked(stream: &mut UnixStream, bytes: &[u8]) {
    for byte in bytes {
        stream
            .write_all(std::slice::from_ref(byte))
            .expect("write one X11 byte");
    }
}

fn read_setup(stream: &mut UnixStream, order: Order) -> Vec<u8> {
    let mut header = [0_u8; 8];
    stream
        .read_exact(&mut header)
        .expect("read X11 setup header");
    let extra = usize::from(order.u16([header[6], header[7]])) * 4;
    let mut packet = header.to_vec();
    packet.resize(8 + extra, 0);
    stream
        .read_exact(&mut packet[8..])
        .expect("read X11 setup body");
    packet
}

fn read_packet(stream: &mut UnixStream, order: Order) -> Vec<u8> {
    let mut packet = vec![0_u8; 32];
    stream
        .read_exact(&mut packet)
        .expect("read X11 reply or error");
    if packet[0] == 1 {
        let extra = order.u32(packet[4..8].try_into().expect("reply length")) as usize * 4;
        packet.resize(32 + extra, 0);
        stream
            .read_exact(&mut packet[32..])
            .expect("read X11 reply payload");
    }
    packet
}

fn expect_closed(stream: &mut UnixStream) {
    let mut byte = [0_u8; 1];
    match stream.read(&mut byte) {
        Ok(0) => {}
        Err(error)
            if matches!(
                error.kind(),
                io::ErrorKind::ConnectionReset | io::ErrorKind::BrokenPipe
            ) => {}
        other => panic!("expected closed X11 connection, got {other:?}"),
    }
}

#[test]
fn linux_adapter_matches_byte_session_recovery_for_both_orders() {
    let server = Server::start();
    for order in [Order::Little, Order::Big] {
        let mut stream = server.raw_connect();
        write_chunked(&mut stream, &setup(order, 11, 0));
        let setup_reply = read_setup(&mut stream, order);
        assert_eq!(setup_reply[0], 1, "setup succeeds after one-byte reads");

        let mut pipeline = Vec::from(request(order, 250));
        pipeline.extend_from_slice(&request(order, 43));
        write_chunked(&mut stream, &pipeline);

        let error = read_packet(&mut stream, order);
        assert_eq!(error[0], 0);
        assert_eq!(error[1], 1, "unknown opcode returns BadRequest");
        assert_eq!(order.u16([error[2], error[3]]), 1);
        assert_eq!(error[10], 250);

        let reply = read_packet(&mut stream, order);
        assert_eq!(reply[0], 1, "session remains synchronized after BadRequest");
        assert_eq!(order.u16([reply[2], reply[3]]), 2);
        assert_eq!(order.u32(reply[8..12].try_into().expect("focus field")), 1);

        stream.shutdown(Shutdown::Write).expect("finish X11 input");
        expect_closed(&mut stream);
    }
}

#[test]
fn linux_adapter_drains_rejected_setup_before_close() {
    let server = Server::start();
    for order in [Order::Little, Order::Big] {
        let mut stream = server.raw_connect();
        write_chunked(&mut stream, &setup(order, 10, 0));
        let rejection = read_setup(&mut stream, order);
        assert_eq!(rejection[0], 0);
        let reason_length = usize::from(rejection[1]);
        assert_eq!(
            &rejection[8..8 + reason_length],
            b"X11 protocol 11.0 required"
        );
        expect_closed(&mut stream);
    }
}

#[test]
fn linux_adapter_closes_hostile_and_truncated_setup_without_reply() {
    let server = Server::start();

    let mut hostile = server.raw_connect();
    hostile
        .write_all(b"x")
        .expect("write hostile byte-order marker");
    expect_closed(&mut hostile);

    let mut truncated = server.raw_connect();
    truncated
        .write_all(&[b'l', 0, 11])
        .expect("write truncated setup");
    truncated
        .shutdown(Shutdown::Write)
        .expect("finish truncated setup input");
    expect_closed(&mut truncated);
}
