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
            "glasswyrmd-window-oracle-{}-{nonce}",
            std::process::id()
        ));
        let mut directory_builder = fs::DirBuilder::new();
        directory_builder.mode(0o700);
        directory_builder
            .create(&directory)
            .expect("create private process-test directory");
        let socket = directory.join("X0");
        let executable = std::env::var_os("GW_GLASSWYRMD_WINDOW_ORACLE")
            .unwrap_or_else(|| env!("CARGO_BIN_EXE_glasswyrmd").into());
        let child = Command::new(executable)
            .args(["--display", "0", "--socket-dir"])
            .arg(&directory)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .expect("start glasswyrmd window candidate");
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

    fn connect(&self, order: Order) -> (UnixStream, u32) {
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
        let resource_base = order.u32(body[4..8].try_into().expect("resource base field"));
        (stream, resource_base)
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

fn create_window(order: Order, window: u32, parent: u32) -> Vec<u8> {
    let mut request = vec![1, 24];
    request.extend_from_slice(&order.u16_bytes(8));
    request.extend_from_slice(&order.u32_bytes(window));
    request.extend_from_slice(&order.u32_bytes(parent));
    request.extend_from_slice(&order.u16_bytes((-5_i16) as u16));
    request.extend_from_slice(&order.u16_bytes(7));
    request.extend_from_slice(&order.u16_bytes(320));
    request.extend_from_slice(&order.u16_bytes(200));
    request.extend_from_slice(&order.u16_bytes(2));
    request.extend_from_slice(&order.u16_bytes(1));
    request.extend_from_slice(&order.u32_bytes(3));
    request.extend_from_slice(&order.u32_bytes(0));
    request
}

fn window_request(order: Order, opcode: u8, window: u32) -> Vec<u8> {
    let mut request = vec![opcode, 0];
    request.extend_from_slice(&order.u16_bytes(2));
    request.extend_from_slice(&order.u32_bytes(window));
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

fn query_children(stream: &mut UnixStream, order: Order, window: u32) -> Vec<u32> {
    stream
        .write_all(&window_request(order, 15, window))
        .expect("write QueryTree");
    let reply = read_packet(stream, order);
    assert_eq!(reply[0], 1, "QueryTree returns a reply");
    let count = usize::from(order.u16(reply[16..18].try_into().expect("child count")));
    (0..count)
        .map(|index| {
            let offset = 32 + index * 4;
            order.u32(
                reply[offset..offset + 4]
                    .try_into()
                    .expect("child window field"),
            )
        })
        .collect()
}

#[test]
fn process_runs_the_retained_window_exchange_in_both_byte_orders() {
    let server = Server::start();
    for order in [Order::Little, Order::Big] {
        let (mut stream, resource_base) = server.connect(order);
        let window = resource_base + 1;
        stream
            .write_all(&create_window(order, window, 1))
            .expect("write CreateWindow");
        stream
            .write_all(&window_request(order, 14, window))
            .expect("write GetGeometry");
        let geometry = read_packet(&mut stream, order);
        assert_eq!(geometry[0], 1);
        assert_eq!(geometry[1], 24);
        assert_eq!(order.u32(geometry[8..12].try_into().unwrap()), 1);
        assert_eq!(order.u16(geometry[12..14].try_into().unwrap()) as i16, -5);
        assert_eq!(order.u16(geometry[14..16].try_into().unwrap()), 7);
        assert_eq!(order.u16(geometry[16..18].try_into().unwrap()), 320);
        assert_eq!(order.u16(geometry[18..20].try_into().unwrap()), 200);
        assert_eq!(query_children(&mut stream, order, 1), vec![window]);

        stream
            .write_all(&window_request(order, 4, window))
            .expect("write DestroyWindow");
        assert!(query_children(&mut stream, order, 1).is_empty());
        stream
            .write_all(&window_request(order, 14, window))
            .expect("write missing GetGeometry");
        let missing = read_packet(&mut stream, order);
        assert_eq!(missing[1], 9, "destroyed window returns BadDrawable");
        assert_eq!(order.u32(missing[4..8].try_into().unwrap()), window);
    }
}

#[test]
fn disconnect_recursively_releases_owned_window_state() {
    let server = Server::start();
    let (mut owner, resource_base) = server.connect(Order::Little);
    let (mut observer, _) = server.connect(Order::Big);
    let window = resource_base + 1;
    owner
        .write_all(&create_window(Order::Little, window, 1))
        .expect("write owner CreateWindow");
    owner
        .write_all(&window_request(Order::Little, 14, window))
        .expect("synchronize owner CreateWindow");
    assert_eq!(read_packet(&mut owner, Order::Little)[0], 1);
    assert!(query_children(&mut observer, Order::Big, 1).contains(&window));
    drop(owner);

    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        if !query_children(&mut observer, Order::Big, 1).contains(&window) {
            break;
        }
        assert!(Instant::now() < deadline, "disconnect cleanup did not run");
        thread::sleep(Duration::from_millis(1));
    }
}
