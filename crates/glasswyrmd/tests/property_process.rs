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
            "glasswyrmd-property-oracle-{}-{nonce}",
            std::process::id()
        ));
        let mut directory_builder = fs::DirBuilder::new();
        directory_builder.mode(0o700);
        directory_builder
            .create(&directory)
            .expect("create private process-test directory");
        let socket = directory.join("X0");
        let executable = std::env::var_os("GW_GLASSWYRMD_PROPERTY_ORACLE")
            .unwrap_or_else(|| env!("CARGO_BIN_EXE_glasswyrmd").into());
        let child = Command::new(executable)
            .args(["--display", "0", "--socket-dir"])
            .arg(&directory)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .expect("start glasswyrmd property candidate");
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

fn create_window(order: Order, window: u32) -> Vec<u8> {
    let mut request = vec![1, 24];
    request.extend_from_slice(&order.u16_bytes(8));
    request.extend_from_slice(&order.u32_bytes(window));
    request.extend_from_slice(&order.u32_bytes(1));
    request.extend_from_slice(&order.u16_bytes(0));
    request.extend_from_slice(&order.u16_bytes(0));
    request.extend_from_slice(&order.u16_bytes(64));
    request.extend_from_slice(&order.u16_bytes(64));
    request.extend_from_slice(&order.u16_bytes(0));
    request.extend_from_slice(&order.u16_bytes(1));
    request.extend_from_slice(&order.u32_bytes(3));
    request.extend_from_slice(&order.u32_bytes(0));
    request
}

fn change_property(
    order: Order,
    mode: u8,
    window: u32,
    property: u32,
    property_type: u32,
    format: u8,
    data: &[u8],
) -> Vec<u8> {
    let item_count = match format {
        8 => data.len(),
        16 => data.len() / 2,
        32 => data.len() / 4,
        _ => 0,
    } as u32;
    let padded = (data.len() + 3) & !3;
    let mut request = vec![18, mode];
    request.extend_from_slice(&order.u16_bytes(((24 + padded) / 4) as u16));
    request.extend_from_slice(&order.u32_bytes(window));
    request.extend_from_slice(&order.u32_bytes(property));
    request.extend_from_slice(&order.u32_bytes(property_type));
    request.push(format);
    request.extend_from_slice(&[0; 3]);
    request.extend_from_slice(&order.u32_bytes(item_count));
    request.extend_from_slice(data);
    request.resize(24 + padded, 0);
    request
}

fn get_property(
    order: Order,
    delete: u8,
    window: u32,
    property: u32,
    property_type: u32,
    offset: u32,
    length: u32,
) -> Vec<u8> {
    let mut request = vec![20, delete];
    request.extend_from_slice(&order.u16_bytes(6));
    for value in [window, property, property_type, offset, length] {
        request.extend_from_slice(&order.u32_bytes(value));
    }
    request
}

fn atom_window_request(order: Order, opcode: u8, window: u32, atom: u32) -> Vec<u8> {
    let mut request = vec![opcode, 0];
    request.extend_from_slice(&order.u16_bytes(3));
    request.extend_from_slice(&order.u32_bytes(window));
    request.extend_from_slice(&order.u32_bytes(atom));
    request
}

fn list_properties(order: Order, window: u32) -> Vec<u8> {
    let mut request = vec![21, 0];
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

fn expected_prefix(order: Order) -> Vec<u8> {
    match order {
        Order::Little => vec![
            1, 8, 3, 0, 1, 0, 0, 0, 31, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, b'a', b'b', b'c', b'd',
        ],
        Order::Big => vec![
            1, 8, 0, 3, 0, 0, 0, 1, 0, 0, 0, 31, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, b'a', b'b', b'c', b'd',
        ],
    }
}

fn expected_tail(order: Order) -> Vec<u8> {
    match order {
        Order::Little => vec![
            1, 8, 4, 0, 1, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, b'e', 0, 0, 0,
        ],
        Order::Big => vec![
            1, 8, 0, 4, 0, 0, 0, 1, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, b'e', 0, 0, 0,
        ],
    }
}

fn expected_absent(order: Order) -> Vec<u8> {
    let mut packet = vec![0_u8; 32];
    packet[0] = 1;
    packet[2..4].copy_from_slice(&order.u16_bytes(5));
    packet
}

fn expected_list(order: Order, sequence: u16, atoms: &[u32]) -> Vec<u8> {
    let mut packet = vec![1, 0];
    packet.extend_from_slice(&order.u16_bytes(sequence));
    packet.extend_from_slice(&order.u32_bytes(atoms.len() as u32));
    packet.extend_from_slice(&order.u16_bytes(atoms.len() as u16));
    packet.resize(32, 0);
    for atom in atoms {
        packet.extend_from_slice(&order.u32_bytes(*atom));
    }
    packet
}

fn expected_error(order: Order, code: u8, sequence: u16, bad_value: u32, major: u8) -> Vec<u8> {
    let mut packet = vec![0, code];
    packet.extend_from_slice(&order.u16_bytes(sequence));
    packet.extend_from_slice(&order.u32_bytes(bad_value));
    packet.extend_from_slice(&[0, 0, major]);
    packet.resize(32, 0);
    packet
}

fn exercise_property_oracle(server: &Server, order: Order) {
    let (mut stream, resource_base) = server.connect(order);
    let window = resource_base + 1;
    stream.write_all(&create_window(order, window)).unwrap();
    stream
        .write_all(&change_property(order, 0, window, 39, 31, 8, b"abcde"))
        .unwrap();

    stream
        .write_all(&get_property(order, 1, window, 39, 31, 0, 1))
        .unwrap();
    assert_eq!(read_packet(&mut stream, order), expected_prefix(order));
    stream
        .write_all(&get_property(order, 1, window, 39, 31, 1, 1))
        .unwrap();
    assert_eq!(read_packet(&mut stream, order), expected_tail(order));
    stream
        .write_all(&get_property(order, 0, window, 39, 0, 0, 1))
        .unwrap();
    assert_eq!(read_packet(&mut stream, order), expected_absent(order));

    stream
        .write_all(&change_property(order, 0, window, 41, 31, 8, b"x"))
        .unwrap();
    stream
        .write_all(&change_property(order, 0, window, 39, 31, 8, b"y"))
        .unwrap();
    stream.write_all(&list_properties(order, window)).unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_list(order, 8, &[39, 41])
    );
    stream
        .write_all(&atom_window_request(order, 19, window, 39))
        .unwrap();
    stream.write_all(&list_properties(order, window)).unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_list(order, 10, &[41])
    );

    stream
        .write_all(&get_property(order, 0, window, 41, 31, 1, 1))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 2, 11, 1, 20)
    );
    stream
        .write_all(&change_property(order, 3, 0x00ab_cdef, 999, 1000, 7, &[]))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 2, 12, 3, 18)
    );
    stream
        .write_all(&change_property(order, 0, 0x00ab_cdef, 999, 1000, 8, &[]))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 3, 13, 0x00ab_cdef, 18)
    );
    stream
        .write_all(&change_property(order, 0, window, 999, 1000, 8, &[]))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 5, 14, 999, 18)
    );
    stream
        .write_all(&atom_window_request(order, 19, 0x00ab_cdef, 999))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 3, 15, 0x00ab_cdef, 19)
    );
    stream
        .write_all(&list_properties(order, 0x00ab_cdef))
        .unwrap();
    assert_eq!(
        read_packet(&mut stream, order),
        expected_error(order, 3, 16, 0x00ab_cdef, 21)
    );
}

#[test]
fn process_matches_frozen_native_property_packets_and_status_order() {
    let server = Server::start();
    exercise_property_oracle(&server, Order::Little);
    exercise_property_oracle(&server, Order::Big);
}

#[test]
fn typed_properties_cross_byte_orders_and_leave_with_their_window() {
    let server = Server::start();
    let (mut owner, resource_base) = server.connect(Order::Little);
    let (mut observer, _) = server.connect(Order::Big);
    let window = resource_base + 1;
    owner
        .write_all(&create_window(Order::Little, window))
        .unwrap();
    owner
        .write_all(&change_property(
            Order::Little,
            0,
            window,
            39,
            19,
            16,
            &[0x34, 0x12, 0xcd, 0xab],
        ))
        .unwrap();
    owner
        .write_all(&get_property(Order::Little, 0, window, 39, 19, 0, 16))
        .unwrap();
    assert_eq!(read_packet(&mut owner, Order::Little)[0], 1);

    observer
        .write_all(&get_property(Order::Big, 0, window, 39, 19, 0, 16))
        .unwrap();
    let words = read_packet(&mut observer, Order::Big);
    assert_eq!(words[1], 16);
    assert_eq!(&words[32..36], &[0x12, 0x34, 0xab, 0xcd]);

    observer
        .write_all(&change_property(
            Order::Big,
            0,
            window,
            39,
            6,
            32,
            &[0x11, 0x22, 0x33, 0x44, 0xaa, 0xbb, 0xcc, 0xdd],
        ))
        .unwrap();
    observer
        .write_all(&get_property(Order::Big, 0, window, 39, 6, 0, 16))
        .unwrap();
    assert_eq!(read_packet(&mut observer, Order::Big)[0], 1);
    owner
        .write_all(&get_property(Order::Little, 0, window, 39, 6, 0, 16))
        .unwrap();
    let dwords = read_packet(&mut owner, Order::Little);
    assert_eq!(dwords[1], 32);
    assert_eq!(
        &dwords[32..40],
        &[0x44, 0x33, 0x22, 0x11, 0xdd, 0xcc, 0xbb, 0xaa]
    );

    drop(owner);
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        observer
            .write_all(&get_property(Order::Big, 0, window, 39, 0, 0, 1))
            .unwrap();
        let packet = read_packet(&mut observer, Order::Big);
        if packet[0] == 0 && packet[1] == 3 {
            assert_eq!(Order::Big.u32(packet[4..8].try_into().unwrap()), window);
            break;
        }
        assert!(Instant::now() < deadline, "disconnect cleanup did not run");
        thread::sleep(Duration::from_millis(1));
    }
}
