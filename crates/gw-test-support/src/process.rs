use crate::poll_until;
use std::collections::BTreeMap;
use std::ffi::{OsStr, OsString};
use std::fs::{self, File};
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};
use std::time::Duration;

#[cfg(unix)]
use std::os::unix::process::{CommandExt, ExitStatusExt};

#[derive(Clone, Debug)]
pub struct ProcessSpec {
    name: String,
    program: PathBuf,
    args: Vec<OsString>,
    env: BTreeMap<OsString, OsString>,
    current_dir: Option<PathBuf>,
}

impl ProcessSpec {
    pub fn new(name: impl Into<String>, program: impl Into<PathBuf>) -> Self {
        Self {
            name: name.into(),
            program: program.into(),
            args: Vec::new(),
            env: BTreeMap::new(),
            current_dir: None,
        }
    }

    pub fn arg(mut self, arg: impl Into<OsString>) -> Self {
        self.args.push(arg.into());
        self
    }

    pub fn args<I, S>(mut self, args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<OsString>,
    {
        self.args.extend(args.into_iter().map(Into::into));
        self
    }

    pub fn env(mut self, key: impl Into<OsString>, value: impl Into<OsString>) -> Self {
        self.env.insert(key.into(), value.into());
        self
    }

    pub fn current_dir(mut self, path: impl Into<PathBuf>) -> Self {
        self.current_dir = Some(path.into());
        self
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn command_line(&self) -> String {
        std::iter::once(self.program.as_os_str())
            .chain(self.args.iter().map(OsString::as_os_str))
            .map(display_argument)
            .collect::<Vec<_>>()
            .join(" ")
    }

    pub fn spawn(&self, process_dir: impl AsRef<Path>) -> io::Result<SupervisedChild> {
        self.spawn_as(process_dir, &self.name)
    }

    fn spawn_as(
        &self,
        process_dir: impl AsRef<Path>,
        instance_name: &str,
    ) -> io::Result<SupervisedChild> {
        fs::create_dir_all(process_dir.as_ref())?;
        let safe_name = safe_component(instance_name);
        let stdout_path = process_dir.as_ref().join(format!("{safe_name}.stdout"));
        let stderr_path = process_dir.as_ref().join(format!("{safe_name}.stderr"));
        let stdout = File::create(&stdout_path)?;
        let stderr = File::create(&stderr_path)?;

        let mut command = Command::new(&self.program);
        command
            .args(&self.args)
            .envs(&self.env)
            .stdin(Stdio::null())
            .stdout(Stdio::from(stdout))
            .stderr(Stdio::from(stderr));
        if let Some(current_dir) = &self.current_dir {
            command.current_dir(current_dir);
        }
        #[cfg(unix)]
        command.process_group(0);

        let child = command.spawn()?;
        Ok(SupervisedChild {
            name: instance_name.to_owned(),
            command_line: self.command_line(),
            child,
            stdout_path,
            stderr_path,
            exit: None,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Signal {
    Terminate,
    Kill,
    Stop,
    Continue,
}

impl Signal {
    #[cfg(unix)]
    fn kill_flag(self) -> &'static str {
        match self {
            Self::Terminate => "-TERM",
            Self::Kill => "-KILL",
            Self::Stop => "-STOP",
            Self::Continue => "-CONT",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExitInfo {
    pub success: bool,
    pub code: Option<i32>,
    pub signal: Option<i32>,
}

impl ExitInfo {
    fn from_status(status: ExitStatus) -> Self {
        Self {
            success: status.success(),
            code: status.code(),
            #[cfg(unix)]
            signal: status.signal(),
            #[cfg(not(unix))]
            signal: None,
        }
    }
}

#[derive(Debug)]
pub struct SupervisedChild {
    name: String,
    command_line: String,
    child: Child,
    stdout_path: PathBuf,
    stderr_path: PathBuf,
    exit: Option<ExitInfo>,
}

impl SupervisedChild {
    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn id(&self) -> u32 {
        self.child.id()
    }

    pub fn command_line(&self) -> &str {
        &self.command_line
    }

    pub fn stdout_path(&self) -> &Path {
        &self.stdout_path
    }

    pub fn stderr_path(&self) -> &Path {
        &self.stderr_path
    }

    pub fn stdout(&self) -> io::Result<Vec<u8>> {
        fs::read(&self.stdout_path)
    }

    pub fn stderr(&self) -> io::Result<Vec<u8>> {
        fs::read(&self.stderr_path)
    }

    pub fn exit_info(&self) -> Option<ExitInfo> {
        self.exit
    }

    pub fn refresh_status(&mut self) -> io::Result<Option<ExitInfo>> {
        if self.exit.is_none() {
            self.exit = self.child.try_wait()?.map(ExitInfo::from_status);
        }
        Ok(self.exit)
    }

    pub fn wait(&mut self) -> io::Result<ExitInfo> {
        if let Some(exit) = self.exit {
            return Ok(exit);
        }
        let exit = ExitInfo::from_status(self.child.wait()?);
        self.exit = Some(exit);
        Ok(exit)
    }

    pub fn wait_timeout(&mut self, timeout: Duration) -> io::Result<Option<ExitInfo>> {
        if let Some(exit) = self.refresh_status()? {
            return Ok(Some(exit));
        }
        let result = poll_until(
            format!("process {} ({}) to exit", self.name, self.id()),
            timeout,
            Duration::from_millis(10).min(timeout),
            || self.refresh_status(),
        );
        match result {
            Ok(exit) => Ok(Some(exit)),
            Err(_) => Ok(None),
        }
    }

    #[cfg(unix)]
    pub fn signal(&mut self, signal: Signal) -> io::Result<()> {
        if self.refresh_status()?.is_some() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("process {} has already exited", self.name),
            ));
        }
        signal_process_group(self.id(), signal)
    }

    #[cfg(not(unix))]
    pub fn signal(&mut self, signal: Signal) -> io::Result<()> {
        match signal {
            Signal::Kill => self.child.kill(),
            _ => Err(io::Error::new(
                io::ErrorKind::Unsupported,
                "this signal is only supported on Unix",
            )),
        }
    }

    pub fn stop(&mut self) -> io::Result<()> {
        self.signal(Signal::Stop)
    }

    pub fn resume(&mut self) -> io::Result<()> {
        self.signal(Signal::Continue)
    }

    /// Sends SIGTERM to the complete process group, then escalates to SIGKILL.
    pub fn terminate(&mut self, timeout: Duration) -> io::Result<ExitInfo> {
        if let Some(exit) = self.refresh_status()? {
            return Ok(exit);
        }

        if let Err(group_error) = self.signal(Signal::Terminate) {
            self.child.kill().map_err(|child_error| {
                io::Error::new(
                    child_error.kind(),
                    format!(
                        "could not terminate process group ({group_error}) or child ({child_error})"
                    ),
                )
            })?;
        }
        if let Some(exit) = self.wait_timeout(timeout)? {
            return Ok(exit);
        }

        if self.signal(Signal::Kill).is_err() {
            self.child.kill()?;
        }
        self.wait()
    }
}

impl Drop for SupervisedChild {
    fn drop(&mut self) {
        let _ = self.terminate(Duration::from_millis(500));
    }
}

#[derive(Debug)]
pub struct RestartableProcess {
    spec: ProcessSpec,
    process_dir: PathBuf,
    generation: u32,
    child: SupervisedChild,
}

impl RestartableProcess {
    pub fn spawn(spec: ProcessSpec, process_dir: impl Into<PathBuf>) -> io::Result<Self> {
        let process_dir = process_dir.into();
        let generation = 1;
        let child = spec.spawn_as(
            &process_dir,
            &format!("{}-generation-{generation}", spec.name()),
        )?;
        Ok(Self {
            spec,
            process_dir,
            generation,
            child,
        })
    }

    pub fn generation(&self) -> u32 {
        self.generation
    }

    pub fn child(&self) -> &SupervisedChild {
        &self.child
    }

    pub fn child_mut(&mut self) -> &mut SupervisedChild {
        &mut self.child
    }

    pub fn restart(&mut self, timeout: Duration) -> io::Result<ExitInfo> {
        let previous = self.child.terminate(timeout)?;
        self.generation = self.generation.checked_add(1).ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidData, "process generation overflow")
        })?;
        self.child = self.spec.spawn_as(
            &self.process_dir,
            &format!("{}-generation-{}", self.spec.name(), self.generation),
        )?;
        Ok(previous)
    }
}

#[cfg(unix)]
fn signal_process_group(id: u32, signal: Signal) -> io::Result<()> {
    let group = format!("-{id}");
    let status = Command::new("kill")
        .args([OsStr::new(signal.kill_flag()), OsStr::new("--")])
        .arg(&group)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()?;
    if status.success() {
        Ok(())
    } else {
        Err(io::Error::other(format!(
            "kill {} -- {} exited with {status}",
            signal.kill_flag(),
            group
        )))
    }
}

fn safe_component(value: &str) -> String {
    let result: String = value
        .chars()
        .map(|character| {
            if character.is_ascii_alphanumeric() || character == '-' || character == '_' {
                character
            } else {
                '-'
            }
        })
        .collect();
    if result.is_empty() {
        "process".to_owned()
    } else {
        result
    }
}

fn display_argument(argument: &OsStr) -> String {
    let value = argument.to_string_lossy();
    if !value.is_empty()
        && value
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "_./:@%+=,-".contains(c))
    {
        value.into_owned()
    } else {
        format!("'{}'", value.replace('\'', "'\\''"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn process_dir(name: &str) -> PathBuf {
        let path = std::env::temp_dir().join(format!(
            "gw-test-support-process-{}-{name}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&path);
        path
    }

    #[cfg(unix)]
    #[test]
    fn captures_output_and_exit_code() {
        let dir = process_dir("capture");
        let mut child = ProcessSpec::new("capture", "sh")
            .args(["-c", "printf stdout; printf stderr >&2; exit 7"])
            .spawn(&dir)
            .unwrap();
        let exit = child.wait().unwrap();
        assert_eq!(exit.code, Some(7));
        assert_eq!(child.stdout().unwrap(), b"stdout");
        assert_eq!(child.stderr().unwrap(), b"stderr");
        fs::remove_dir_all(dir).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn stop_continue_and_terminate_are_bounded() {
        let dir = process_dir("signals");
        let mut child = ProcessSpec::new("signals", "sh")
            .args(["-c", "while :; do :; done"])
            .spawn(&dir)
            .unwrap();
        child.stop().unwrap();
        child.resume().unwrap();
        let exit = child.terminate(Duration::from_millis(500)).unwrap();
        assert_eq!(exit.signal, Some(15));
        fs::remove_dir_all(dir).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn restart_uses_separate_capture_files() {
        let dir = process_dir("restart");
        let spec = ProcessSpec::new("peer", "sh").args(["-c", "printf generation; sleep 5"]);
        let mut process = RestartableProcess::spawn(spec, &dir).unwrap();
        process.restart(Duration::from_millis(500)).unwrap();
        assert_eq!(process.generation(), 2);
        assert!(dir.join("peer-generation-1.stdout").exists());
        assert!(dir.join("peer-generation-2.stdout").exists());
        process
            .child_mut()
            .terminate(Duration::from_millis(500))
            .unwrap();
        fs::remove_dir_all(dir).unwrap();
    }
}
