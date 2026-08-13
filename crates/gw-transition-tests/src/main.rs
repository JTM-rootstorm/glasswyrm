mod unix;
mod wire;

use gw_ipc::{
    HandshakeConfig, Transport, TransportError, TransportLimits, make_hello,
    validate_server_response,
};
use gw_test_support::{
    Attachment, FailureBundle, ProcessSpec, RuntimeDir, SupervisedChild, TestId,
    deterministic_seed, poll_until,
};
use gw_types::{Capabilities, MessageFlags, MessageType, Role};
use std::error::Error;
use std::ffi::OsString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::os::unix::fs::FileTypeExt;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use wire::{
    Acknowledgement, OUTPUT_CONFIGURATION_ACCEPTED, OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED,
    Snapshot,
};

type AnyError = Box<dyn Error + Send + Sync>;

#[derive(Clone, Debug)]
struct Options {
    build_dir: PathBuf,
    artifact_root: PathBuf,
    timeout: Duration,
    run_id: String,
}

impl Options {
    fn parse() -> Result<Self, String> {
        let mut build_dir = std::env::var_os("GW_LEGACY_BUILD_DIR").map(PathBuf::from);
        let mut artifact_root = std::env::var_os("GW_TRANSITION_ARTIFACT_ROOT")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("artifacts/rust-transition/failures"));
        let mut timeout = Duration::from_secs(8);
        let mut run_id = default_run_id();
        let mut arguments = std::env::args_os().skip(1);
        while let Some(argument) = arguments.next() {
            let value = argument.to_string_lossy();
            match value.as_ref() {
                "--build-dir" => {
                    build_dir = Some(PathBuf::from(take_value(&mut arguments, "--build-dir")?));
                }
                "--artifact-root" => {
                    artifact_root = PathBuf::from(take_value(&mut arguments, "--artifact-root")?);
                }
                "--timeout-ms" => {
                    let raw = take_value(&mut arguments, "--timeout-ms")?;
                    let milliseconds = raw
                        .to_string_lossy()
                        .parse::<u64>()
                        .map_err(|_| "--timeout-ms must be a positive integer".to_owned())?;
                    if milliseconds == 0 {
                        return Err("--timeout-ms must be nonzero".to_owned());
                    }
                    timeout = Duration::from_millis(milliseconds);
                }
                "--run-id" => {
                    run_id = take_value(&mut arguments, "--run-id")?
                        .into_string()
                        .map_err(|_| "--run-id must be valid UTF-8".to_owned())?;
                    validate_component(&run_id)?;
                }
                "--help" => {
                    println!(
                        "Usage: legacy-output-restart --build-dir DIR [--artifact-root DIR] [--timeout-ms N] [--run-id ID]"
                    );
                    std::process::exit(0);
                }
                _ => return Err(format!("unknown argument: {value}")),
            }
        }
        let build_dir =
            build_dir.ok_or_else(|| "--build-dir or GW_LEGACY_BUILD_DIR is required".to_owned())?;
        Ok(Self {
            build_dir,
            artifact_root,
            timeout,
            run_id,
        })
    }
}

fn take_value(
    arguments: &mut impl Iterator<Item = OsString>,
    option: &str,
) -> Result<OsString, String> {
    arguments
        .next()
        .ok_or_else(|| format!("{option} requires a value"))
}

fn validate_component(value: &str) -> Result<(), String> {
    if value.is_empty()
        || value == "."
        || value == ".."
        || !value
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || character == '-')
    {
        return Err("--run-id must contain only ASCII letters, digits, and hyphens".to_owned());
    }
    Ok(())
}

fn default_run_id() -> String {
    let elapsed = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    format!(
        "run-{}-{}-{}",
        std::process::id(),
        elapsed.as_secs(),
        elapsed.subsec_nanos()
    )
}

#[derive(Debug)]
struct Trace {
    path: PathBuf,
    file: File,
}

impl Trace {
    fn create(path: PathBuf) -> io::Result<Self> {
        let file = OpenOptions::new()
            .create_new(true)
            .append(true)
            .open(&path)?;
        Ok(Self { path, file })
    }

    fn event(&mut self, event: &str, detail: &str) -> io::Result<()> {
        writeln!(
            self.file,
            "{{\"event\":\"{}\",\"detail\":\"{}\"}}",
            json_escape(event),
            json_escape(detail)
        )?;
        self.file.flush()
    }
}

#[derive(Debug)]
struct Processes {
    wm: Option<SupervisedChild>,
    compositor: Option<SupervisedChild>,
    replacement_compositor: Option<SupervisedChild>,
    server: Option<SupervisedChild>,
}

impl Processes {
    fn new() -> Self {
        Self {
            wm: None,
            compositor: None,
            replacement_compositor: None,
            server: None,
        }
    }

    fn shutdown(&mut self, timeout: Duration) {
        for process in [
            &mut self.server,
            &mut self.replacement_compositor,
            &mut self.compositor,
            &mut self.wm,
        ]
        .into_iter()
        .flatten()
        {
            let _ = process.resume();
            let _ = process.terminate(timeout.min(Duration::from_secs(2)));
        }
    }

    fn capture(&mut self, bundle: &mut FailureBundle) -> io::Result<()> {
        for process in [
            &mut self.wm,
            &mut self.compositor,
            &mut self.replacement_compositor,
            &mut self.server,
        ]
        .into_iter()
        .flatten()
        {
            bundle.capture_process(process)?;
        }
        Ok(())
    }
}

#[derive(Debug)]
struct LegacyClient {
    transport: Transport,
    next_sequence: u64,
    timeout: Duration,
}

impl LegacyClient {
    fn connect(path: &Path, timeout: Duration) -> Result<Self, AnyError> {
        let descriptor = poll_until(
            format!("connect to legacy control socket {}", path.display()),
            timeout,
            Duration::from_millis(5),
            || match unix::connect_seqpacket(path) {
                Ok(descriptor) => Ok(Some(descriptor)),
                Err(error)
                    if matches!(
                        error.kind(),
                        io::ErrorKind::NotFound
                            | io::ErrorKind::ConnectionRefused
                            | io::ErrorKind::WouldBlock
                    ) =>
                {
                    Ok(None)
                }
                Err(error) => Err(error),
            },
        )?;
        let limits = TransportLimits::new(4096, 0)?;
        let mut transport = Transport::from_owned_fd(descriptor, limits)?;
        let capabilities = Capabilities::SNAPSHOTS
            .with(Capabilities::OUTPUT_STATE)
            .with(Capabilities::OUTPUT_CONTROL)
            .with(Capabilities::VRR_METADATA)
            .with(Capabilities::VRR_POLICY)
            .with(Capabilities::PRESENTATION_TIMING);
        let mut config = HandshakeConfig::new(
            Role::DiagnosticTool,
            [0x52; 16],
            "rust-transition-output-restart",
        )
        .allow_peer_role(Role::ProtocolServer)
        .offer(capabilities)
        .require_peer(Capabilities::OUTPUT_CONTROL);
        config.limits = limits;
        let hello = make_hello(&config)?;
        send_with_poll(&transport, &hello.envelope, &hello.payload, timeout)?;
        let welcome = receive_with_poll(&transport, timeout)?;
        let peer = validate_server_response(&welcome, &config)?;
        if !peer.capabilities.contains(Capabilities::VRR_POLICY)
            || !peer.capabilities.contains(Capabilities::VRR_METADATA)
        {
            return Err("legacy control socket did not negotiate the M14 VRR profile".into());
        }
        transport.set_limits(peer.limits)?;
        Ok(Self {
            transport,
            next_sequence: 2,
            timeout,
        })
    }

    fn send(
        &mut self,
        message_type: MessageType,
        flags: MessageFlags,
        payload: &[u8],
    ) -> Result<(), AnyError> {
        let envelope =
            wire::request_envelope(message_type, flags, self.next_sequence, payload.len())?;
        self.next_sequence = self
            .next_sequence
            .checked_add(1)
            .ok_or("GWIPC sequence exhausted")?;
        send_with_poll(&self.transport, &envelope, payload, self.timeout)
    }

    fn query(&mut self, query_id: u64) -> Result<Snapshot, AnyError> {
        self.send(
            MessageType::OUTPUT_STATE_QUERY,
            MessageFlags::ACK_REQUIRED,
            &wire::encode_query(query_id),
        )?;
        let mut outputs = Vec::new();
        let mut vrr_policies = Vec::new();
        let mut vrr_state_count = 0;
        loop {
            let record = receive_with_poll(&self.transport, self.timeout)?;
            if !record.fds.is_empty() {
                return Err("output control reply unexpectedly carried descriptors".into());
            }
            match record.envelope.message_type {
                MessageType::OUTPUT_UPSERT => outputs.push(record.payload),
                MessageType::OUTPUT_VRR_POLICY_UPSERT => vrr_policies.push(record.payload),
                MessageType::OUTPUT_VRR_STATE_UPSERT => vrr_state_count += 1,
                MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED => {
                    let acknowledgement = wire::decode_acknowledgement(&record.payload)?;
                    if acknowledgement.request_id != query_id {
                        return Err(format!(
                            "expected query acknowledgement {query_id}, observed {}",
                            acknowledgement.request_id
                        )
                        .into());
                    }
                    return Ok(Snapshot {
                        generation: acknowledgement.generation,
                        primary_output: acknowledgement.primary_output,
                        root_width: acknowledgement.root_width,
                        root_height: acknowledgement.root_height,
                        result: acknowledgement.result,
                        outputs,
                        vrr_policies,
                        vrr_state_count,
                    });
                }
                MessageType::PROTOCOL_ERROR => {
                    return Err("legacy peer reported a GWIPC protocol error".into());
                }
                _ => {}
            }
        }
    }

    fn send_configuration(&mut self, id: u64, initial: &Snapshot) -> Result<(), AnyError> {
        let outputs = wire::vertical_output_payloads(&initial.outputs)?;
        let item_count = u32::try_from(outputs.len() + initial.vrr_policies.len())
            .map_err(|_| "output snapshot item count overflow")?;
        self.send(
            MessageType::SNAPSHOT_BEGIN,
            MessageFlags::default(),
            &wire::encode_snapshot_begin(id, initial.generation, item_count),
        )?;
        for output in &outputs {
            self.send(
                MessageType::OUTPUT_UPSERT,
                MessageFlags::SNAPSHOT_ITEM,
                output,
            )?;
        }
        for policy in &initial.vrr_policies {
            self.send(
                MessageType::OUTPUT_VRR_POLICY_UPSERT,
                MessageFlags::SNAPSHOT_ITEM,
                policy,
            )?;
        }
        self.send(
            MessageType::SNAPSHOT_END,
            MessageFlags::default(),
            &wire::encode_snapshot_end(id, initial.generation, item_count),
        )?;
        let primary = wire::output_id(
            outputs
                .get(1)
                .ok_or("output configuration requires its second output")?,
        )?;
        self.send(
            MessageType::OUTPUT_CONFIGURATION_COMMIT,
            MessageFlags::ACK_REQUIRED,
            &wire::encode_commit(id, initial.generation, primary),
        )
    }

    fn receive_acknowledgement(&self, request_id: u64) -> Result<Acknowledgement, AnyError> {
        loop {
            let record = receive_with_poll(&self.transport, self.timeout)?;
            if record.envelope.message_type == MessageType::PROTOCOL_ERROR {
                return Err("legacy peer reported a GWIPC protocol error".into());
            }
            if record.envelope.message_type != MessageType::OUTPUT_CONFIGURATION_ACKNOWLEDGED {
                continue;
            }
            let acknowledgement = wire::decode_acknowledgement(&record.payload)?;
            if acknowledgement.request_id != request_id {
                return Err(format!(
                    "expected configuration acknowledgement {request_id}, observed {}",
                    acknowledgement.request_id
                )
                .into());
            }
            return Ok(acknowledgement);
        }
    }
}

fn send_with_poll(
    transport: &Transport,
    envelope: &gw_wire::Envelope,
    payload: &[u8],
    timeout: Duration,
) -> Result<(), AnyError> {
    poll_until(
        format!("send GWIPC message {:#06x}", envelope.message_type.get()),
        timeout,
        Duration::from_millis(5),
        || match transport.send(envelope, payload, &[]) {
            Ok(()) => Ok(Some(())),
            Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => Ok(None),
            Err(error) => Err(io::Error::other(error)),
        },
    )?;
    Ok(())
}

fn receive_with_poll(
    transport: &Transport,
    timeout: Duration,
) -> Result<gw_ipc::ReceivedRecord, AnyError> {
    Ok(poll_until(
        "receive GWIPC record",
        timeout,
        Duration::from_millis(5),
        || match transport.receive() {
            Ok(record) => Ok(Some(record)),
            Err(TransportError::Io(error)) if error.kind() == io::ErrorKind::WouldBlock => Ok(None),
            Err(error) => Err(io::Error::other(error)),
        },
    )?)
}

fn process_spec(name: &str, path: PathBuf, arguments: &[&str]) -> ProcessSpec {
    ProcessSpec::new(name, path).args(arguments.iter().copied())
}

fn compositor_spec(name: &str, path: PathBuf, socket: &Path, dumps: &Path) -> ProcessSpec {
    ProcessSpec::new(name, path)
        .args(["--backend", "headless", "--ipc-socket"])
        .arg(socket.as_os_str())
        .arg("--dump-dir")
        .arg(dumps.as_os_str())
        .args([
            "--headless-output",
            "LEFT:640x480@60000",
            "--headless-output",
            "RIGHT:640x480@60000",
            "--headless-vrr",
            "LEFT=40000-60000",
            "--headless-vrr",
            "RIGHT=40000-60000",
        ])
}

fn wait_for_socket_path(path: &Path, timeout: Duration) -> Result<(), AnyError> {
    poll_until(
        format!("Unix socket path {}", path.display()),
        timeout,
        Duration::from_millis(5),
        || match fs::symlink_metadata(path) {
            Ok(metadata) => Ok(metadata.file_type().is_socket().then_some(())),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(error) => Err(error),
        },
    )?;
    Ok(())
}

fn wait_for_socket_removal(path: &Path, timeout: Duration) -> Result<(), AnyError> {
    poll_until(
        format!("Unix socket removal {}", path.display()),
        timeout,
        Duration::from_millis(5),
        || Ok((!path.exists()).then_some(())),
    )?;
    Ok(())
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct ProcStatus {
    state: char,
    context_switches: u64,
}

fn read_proc_status(pid: u32) -> io::Result<ProcStatus> {
    parse_proc_status(&fs::read_to_string(format!("/proc/{pid}/status"))?)
}

fn parse_proc_status(text: &str) -> io::Result<ProcStatus> {
    let state = text
        .lines()
        .find_map(|line| line.strip_prefix("State:").map(str::trim))
        .and_then(|value| value.chars().next())
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing process state"))?;
    let context_switches = text
        .lines()
        .filter_map(|line| {
            line.strip_prefix("voluntary_ctxt_switches:")
                .or_else(|| line.strip_prefix("nonvoluntary_ctxt_switches:"))
        })
        .try_fold(0_u64, |total, value| {
            value
                .trim()
                .parse::<u64>()
                .map(|count| total.saturating_add(count))
                .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))
        })?;
    Ok(ProcStatus {
        state,
        context_switches,
    })
}

fn wait_for_stopped(process: &mut SupervisedChild, timeout: Duration) -> Result<(), AnyError> {
    let pid = process.id();
    poll_until(
        format!("process {} ({pid}) to enter stopped state", process.name()),
        timeout,
        Duration::from_millis(5),
        || {
            if process.refresh_status()?.is_some() {
                return Err(io::Error::other("process exited while waiting for SIGSTOP"));
            }
            Ok(matches!(read_proc_status(pid)?.state, 'T' | 't').then_some(()))
        },
    )?;
    Ok(())
}

fn wait_for_quiescence(
    process: &mut SupervisedChild,
    prior_switches: u64,
    timeout: Duration,
) -> Result<(), AnyError> {
    let pid = process.id();
    poll_until(
        format!("process {} ({pid}) to run and quiesce", process.name()),
        timeout,
        Duration::from_millis(5),
        || {
            if process.refresh_status()?.is_some() {
                return Err(io::Error::other(
                    "process exited before reaching quiescence",
                ));
            }
            let status = read_proc_status(pid)?;
            Ok(
                (matches!(status.state, 'S' | 'I') && status.context_switches > prior_switches)
                    .then_some(()),
            )
        },
    )?;
    Ok(())
}

fn require(condition: bool, detail: impl Into<String>) -> Result<(), AnyError> {
    if condition {
        Ok(())
    } else {
        Err(detail.into().into())
    }
}

fn run_scenario(
    options: &Options,
    runtime: &RuntimeDir,
    processes: &mut Processes,
    trace: &mut Trace,
) -> Result<(), AnyError> {
    let glasswyrmd = options.build_dir.join("src/glasswyrmd");
    let gwm = options.build_dir.join("src/gwm");
    let gwcomp = options.build_dir.join("src/gwcomp");
    for executable in [&glasswyrmd, &gwm, &gwcomp] {
        require(
            executable.is_file(),
            format!("legacy executable is missing: {}", executable.display()),
        )?;
    }

    let wm_socket = runtime.path().join("gwm.sock");
    let compositor_socket = runtime.path().join("gwcomp.sock");
    let control_socket = runtime.path().join("control.sock");
    let x11_dir = runtime.path().join("x11");
    let dumps = runtime.path().join("dumps");
    fs::create_dir(&x11_dir)?;
    fs::create_dir(&dumps)?;
    let process_dir = runtime.processes_dir();

    processes.wm = Some(
        ProcessSpec::new("gwm", gwm)
            .args(["--ipc-socket"])
            .arg(wm_socket.as_os_str())
            .spawn(&process_dir)?,
    );
    processes.compositor = Some(
        compositor_spec("gwcomp", gwcomp.clone(), &compositor_socket, &dumps)
            .spawn(&process_dir)?,
    );
    wait_for_socket_path(&wm_socket, options.timeout)?;
    wait_for_socket_path(&compositor_socket, options.timeout)?;
    trace.event(
        "legacy-peers-ready",
        "gwm and headless gwcomp sockets exist",
    )?;

    processes.server = Some(
        process_spec("glasswyrmd", glasswyrmd, &["--display", "93"])
            .arg("--socket-dir")
            .arg(x11_dir.as_os_str())
            .arg("--wm-socket")
            .arg(wm_socket.as_os_str())
            .arg("--compositor-socket")
            .arg(compositor_socket.as_os_str())
            .args(["--output-model", "--control-socket"])
            .arg(control_socket.as_os_str())
            .args(["--software-content", "--vrr-protocol"])
            .spawn(&process_dir)?,
    );

    let mut client = LegacyClient::connect(&control_socket, options.timeout)?;
    let initial = client.query(600)?;
    require(
        initial.result == OUTPUT_CONFIGURATION_ACCEPTED
            && initial.generation == 1
            && initial.root_width == 1280
            && initial.root_height == 480
            && initial.outputs.len() == 2
            && initial.vrr_policies.len() == 2
            && initial.vrr_state_count == 2,
        format!("unexpected initial M14 snapshot: {initial:?}"),
    )?;
    trace.event(
        "query-600",
        "generation 1 horizontal layout and VRR state accepted",
    )?;

    let server = processes.server.as_mut().ok_or("server process missing")?;
    server.stop()?;
    wait_for_stopped(server, options.timeout)?;
    trace.event(
        "server-stopped",
        "glasswyrmd reached a kernel-observed stopped state",
    )?;

    let original_compositor = processes
        .compositor
        .as_mut()
        .ok_or("original compositor process missing")?;
    let exit = original_compositor.terminate(options.timeout)?;
    require(
        exit.success || exit.signal.is_some(),
        "original compositor did not terminate",
    )?;
    wait_for_socket_removal(&compositor_socket, options.timeout)?;

    processes.replacement_compositor = Some(
        compositor_spec("gwcomp-replacement", gwcomp, &compositor_socket, &dumps)
            .spawn(&process_dir)?,
    );
    wait_for_socket_path(&compositor_socket, options.timeout)?;
    let replacement = processes
        .replacement_compositor
        .as_mut()
        .ok_or("replacement compositor process missing")?;
    replacement.stop()?;
    wait_for_stopped(replacement, options.timeout)?;
    trace.event(
        "replacement-stopped",
        "replacement gwcomp socket is ready and its process is stopped",
    )?;

    client.send_configuration(601, &initial)?;
    trace.event(
        "configuration-601-sent",
        "vertical layout queued while server is stopped",
    )?;
    let before_resume = read_proc_status(server.id())?.context_switches;
    server.resume()?;
    wait_for_quiescence(server, before_resume, options.timeout)?;
    trace.event(
        "server-quiescent",
        "glasswyrmd ran after resume and blocked awaiting the stopped replacement peer",
    )?;
    replacement.resume()?;
    let rejected = client.receive_acknowledgement(601)?;
    require(
        rejected.result == OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED
            && rejected.generation == 1
            && rejected.root_width == 1280
            && rejected.root_height == 480
            && rejected.primary_output == initial.primary_output,
        format!("configuration 601 did not retain generation 1: {rejected:?}"),
    )?;
    trace.event(
        "configuration-601-rejected",
        "compositor rejection retained generation 1 and 1280x480 root layout",
    )?;

    client.send_configuration(602, &initial)?;
    let accepted = client.receive_acknowledgement(602)?;
    require(
        accepted.result == OUTPUT_CONFIGURATION_ACCEPTED
            && accepted.generation == 2
            && accepted.root_width == 640
            && accepted.root_height == 960,
        format!("configuration 602 was not accepted as generation 2: {accepted:?}"),
    )?;
    trace.event(
        "configuration-602-accepted",
        "replacement compositor accepted generation 2 vertical 640x960 layout",
    )?;
    Ok(())
}

fn json_escape(value: &str) -> String {
    let mut output = String::with_capacity(value.len());
    for character in value.chars() {
        match character {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            character if character.is_control() => output.push('?'),
            character => output.push(character),
        }
    }
    output
}

fn real_main() -> Result<(), AnyError> {
    let options = Options::parse().map_err(io::Error::other)?;
    let test_id = TestId::with_seed(
        "legacy M14 output restart",
        deterministic_seed(options.run_id.as_bytes()),
    );
    let runtime = RuntimeDir::create(&test_id)?;
    let trace_path = runtime.fixtures_dir().join("scenario.jsonl");
    let mut trace = Trace::create(trace_path)?;
    let mut processes = Processes::new();
    trace.event(
        "scenario-start",
        &format!("legacy build {}", options.build_dir.display()),
    )?;

    match run_scenario(&options, &runtime, &mut processes, &mut trace) {
        Ok(()) => {
            processes.shutdown(options.timeout);
            println!(
                "{{\"scenario\":\"legacy-m14-output-restart\",\"result\":\"pass\",\"configuration_601\":\"rejected-retained-generation-1\",\"configuration_602\":\"accepted-generation-2\"}}"
            );
            Ok(())
        }
        Err(error) => {
            let _ = trace.event("scenario-failed", &error.to_string());
            processes.shutdown(options.timeout);
            let mut bundle =
                FailureBundle::new(&options.artifact_root, test_id, options.run_id.clone())?;
            processes.capture(&mut bundle)?;
            bundle.attach(Attachment::new("scenario.jsonl", &trace.path))?;
            let bundle_path = bundle.emit()?;
            let runtime_path = runtime.into_path();
            Err(format!(
                "{error}; failure bundle: {}; preserved runtime: {}",
                bundle_path.display(),
                runtime_path.display()
            )
            .into())
        }
    }
}

fn main() {
    if let Err(error) = real_main() {
        eprintln!("legacy-output-restart: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn proc_status_parser_combines_both_switch_counters() {
        let status = parse_proc_status(
            "Name:\tpeer\nState:\tS (sleeping)\nvoluntary_ctxt_switches:\t7\nnonvoluntary_ctxt_switches:\t3\n",
        )
        .unwrap();
        assert_eq!(
            status,
            ProcStatus {
                state: 'S',
                context_switches: 10
            }
        );
    }

    #[test]
    fn run_identifier_rejects_path_components() {
        assert!(validate_component("attempt-1").is_ok());
        assert!(validate_component("../attempt").is_err());
        assert!(validate_component("attempt_1").is_err());
    }
}
