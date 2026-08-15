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

    fn u16_bytes(self, value: u16) -> [u8; 2] {
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
            "glasswyrmd-extension-discovery-oracle-{}-{nonce}",
            std::process::id()
        ));
        let mut directory_builder = fs::DirBuilder::new();
        directory_builder.mode(0o700);
        directory_builder
            .create(&directory)
            .expect("create private process-test directory");
        let socket = directory.join("X0");
        let executable = std::env::var_os("GW_GLASSWYRMD_EXTENSION_DISCOVERY_ORACLE")
            .unwrap_or_else(|| env!("CARGO_BIN_EXE_glasswyrmd").into());
        let child = Command::new(executable)
            .args(["--display", "0", "--socket-dir"])
            .arg(&directory)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .expect("start glasswyrmd extension-discovery candidate");
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
        let mut stream = UnixStream::connect(&self.socket).expect("connect to glasswyrmd");
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

fn query_extension(order: Order, name: &[u8]) -> Vec<u8> {
    let padded_length = (name.len() + 3) & !3;
    let mut request = vec![98, 0];
    request.extend_from_slice(&order.u16_bytes((2 + padded_length / 4) as u16));
    request.extend_from_slice(&order.u16_bytes(name.len() as u16));
    request.extend_from_slice(&[0, 0]);
    request.extend_from_slice(name);
    request.resize(8 + padded_length, 0);
    request
}

fn list_extensions(order: Order) -> Vec<u8> {
    let mut request = vec![99, 0];
    request.extend_from_slice(&order.u16_bytes(1));
    request
}

fn malformed_query_extension(order: Order) -> Vec<u8> {
    let mut request = vec![98, 0];
    request.extend_from_slice(&order.u16_bytes(1));
    request
}

fn get_input_focus(order: Order) -> Vec<u8> {
    let mut request = vec![43, 0];
    request.extend_from_slice(&order.u16_bytes(1));
    request
}

fn read_packet(stream: &mut UnixStream) -> Vec<u8> {
    let mut packet = vec![0_u8; 32];
    stream
        .read_exact(&mut packet)
        .expect("read X11 reply or error");
    packet
}

fn absent_reply(order: Order, sequence: u16) -> Vec<u8> {
    match (order, sequence) {
        (Order::Little, 1) => vec![
            1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        (Order::Big, 1) => vec![
            1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        (Order::Little, 3) => vec![
            1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        (Order::Big, 3) => vec![
            1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        _ => unreachable!("the frozen oracle uses sequences one and three"),
    }
}

fn empty_list_reply(order: Order) -> Vec<u8> {
    match order {
        Order::Little => vec![
            1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        Order::Big => vec![
            1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
    }
}

fn bad_length_reply(order: Order) -> Vec<u8> {
    match order {
        Order::Little => vec![
            0, 16, 4, 0, 0, 0, 0, 0, 0, 0, 98, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0,
        ],
        Order::Big => vec![
            0, 16, 0, 4, 0, 0, 0, 0, 0, 0, 98, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0,
        ],
    }
}

fn focus_reply(order: Order) -> Vec<u8> {
    match order {
        Order::Little => vec![
            1, 0, 5, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
        Order::Big => vec![
            1, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ],
    }
}

fn exercise_frozen_default_packets(server: &Server, order: Order) {
    let mut stream = server.connect(order);
    stream
        .write_all(&query_extension(order, b"RENDER"))
        .expect("write QueryExtension");
    assert_eq!(read_packet(&mut stream), absent_reply(order, 1));

    stream
        .write_all(&list_extensions(order))
        .expect("write ListExtensions");
    assert_eq!(read_packet(&mut stream), empty_list_reply(order));

    stream
        .write_all(&query_extension(order, &[b'R', 0xff]))
        .expect("write non-UTF-8 QueryExtension");
    assert_eq!(read_packet(&mut stream), absent_reply(order, 3));

    stream
        .write_all(&malformed_query_extension(order))
        .expect("write malformed QueryExtension");
    assert_eq!(read_packet(&mut stream), bad_length_reply(order));

    stream
        .write_all(&get_input_focus(order))
        .expect("write recovery GetInputFocus");
    assert_eq!(read_packet(&mut stream), focus_reply(order));
}

#[test]
fn process_matches_frozen_native_default_extension_discovery_packets() {
    let server = Server::start();
    exercise_frozen_default_packets(&server, Order::Little);
    exercise_frozen_default_packets(&server, Order::Big);
}
