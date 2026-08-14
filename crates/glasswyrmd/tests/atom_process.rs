use std::fs;
use std::io::{Read, Write};
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
    fn u16(self, bytes: [u8; 2]) -> u16 {
        match self {
            Self::Little => u16::from_le_bytes(bytes),
            Self::Big => u16::from_be_bytes(bytes),
        }
    }

    fn u32(self, bytes: [u8; 4]) -> u32 {
        match self {
            Self::Little => u32::from_le_bytes(bytes),
            Self::Big => u32::from_be_bytes(bytes),
        }
    }

    fn u16_bytes(self, value: u16) -> [u8; 2] {
        match self {
            Self::Little => value.to_le_bytes(),
            Self::Big => value.to_be_bytes(),
        }
    }

    fn u32_bytes(self, value: u32) -> [u8; 4] {
        match self {
            Self::Little => value.to_le_bytes(),
            Self::Big => value.to_be_bytes(),
        }
    }

    fn marker(self) -> u8 {
        match self {
            Self::Little => b'l',
            Self::Big => b'B',
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
        let directory = std::env::temp_dir().join(format!(
            "glasswyrmd-rust-atom-{}-{nonce}",
            std::process::id()
        ));
        let mut directory_builder = fs::DirBuilder::new();
        directory_builder.mode(0o700);
        directory_builder
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

    fn connect(&self, order: Order) -> UnixStream {
        let mut stream = UnixStream::connect(&self.socket).expect("connect to Rust glasswyrmd");
        let mut setup = [0_u8; 12];
        setup[0] = order.marker();
        setup[2..4].copy_from_slice(&order.u16_bytes(11));
        stream.write_all(&setup).expect("write X11 setup");
        let mut header = [0_u8; 8];
        stream
            .read_exact(&mut header)
            .expect("read X11 setup header");
        assert_eq!(header[0], 1, "X11 setup succeeds");
        let extra = usize::from(order.u16([header[6], header[7]])) * 4;
        let mut body = vec![0_u8; extra];
        stream.read_exact(&mut body).expect("read X11 setup body");
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

fn intern_atom(order: Order, only_if_exists: bool, name: &[u8]) -> Vec<u8> {
    let mut request = vec![16, u8::from(only_if_exists), 0, 0];
    request.extend_from_slice(&order.u16_bytes(name.len() as u16));
    request.extend_from_slice(&[0, 0]);
    request.extend_from_slice(name);
    request.resize((request.len() + 3) & !3, 0);
    let units = (request.len() / 4) as u16;
    request[2..4].copy_from_slice(&order.u16_bytes(units));
    request
}

fn get_atom_name(order: Order, atom: u32) -> Vec<u8> {
    let mut request = vec![17, 0];
    request.extend_from_slice(&order.u16_bytes(2));
    request.extend_from_slice(&order.u32_bytes(atom));
    request
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

fn write_request(stream: &mut UnixStream, request: &[u8]) {
    stream.write_all(request).expect("write X11 request");
}

fn atom_from_reply(reply: &[u8], order: Order) -> u32 {
    assert_eq!(reply[0], 1, "InternAtom returns a reply");
    order.u32(reply[8..12].try_into().expect("atom field"))
}

#[test]
fn rust_process_matches_the_frozen_legacy_atom_exchange() {
    let server = Server::start();
    let mut little = server.connect(Order::Little);
    let mut big = server.connect(Order::Big);
    let name = b"GW_\xff_ATOM";

    write_request(&mut little, &intern_atom(Order::Little, false, name));
    assert_eq!(
        atom_from_reply(&read_packet(&mut little, Order::Little), Order::Little),
        69
    );

    write_request(&mut big, &intern_atom(Order::Big, true, name));
    assert_eq!(
        atom_from_reply(&read_packet(&mut big, Order::Big), Order::Big),
        69
    );

    write_request(&mut big, &get_atom_name(Order::Big, 69));
    let name_reply = read_packet(&mut big, Order::Big);
    assert_eq!(&name_reply[32..32 + name.len()], name);

    write_request(&mut little, &get_atom_name(Order::Little, 0x1020_3040));
    let error = read_packet(&mut little, Order::Little);
    assert_eq!(error[1], 5, "unknown atom returns BadAtom");
    assert_eq!(
        Order::Little.u32(error[4..8].try_into().expect("bad atom value")),
        0x1020_3040
    );
    assert_eq!(error[10], 17);
}
