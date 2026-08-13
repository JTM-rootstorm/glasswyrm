use crate::{ExitInfo, SupervisedChild, TestId, sha256_file};
use std::fs::{self, File};
use std::io::{self, Write};
use std::path::PathBuf;
use std::time::Instant;

#[derive(Clone, Debug)]
pub struct Attachment {
    pub name: String,
    pub source: PathBuf,
}

impl Attachment {
    pub fn new(name: impl Into<String>, source: impl Into<PathBuf>) -> Self {
        Self {
            name: name.into(),
            source: source.into(),
        }
    }
}

#[derive(Debug)]
struct CapturedProcess {
    name: String,
    command_line: String,
    exit: Option<ExitInfo>,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
}

/// Builds a self-describing failure directory without requiring a JSON crate.
#[derive(Debug)]
pub struct FailureBundle {
    root: PathBuf,
    test_id: TestId,
    run_id: String,
    started: Instant,
    processes: Vec<CapturedProcess>,
    attachments: Vec<Attachment>,
}

impl FailureBundle {
    pub fn new(
        root: impl Into<PathBuf>,
        test_id: TestId,
        run_id: impl Into<String>,
    ) -> io::Result<Self> {
        let run_id = run_id.into();
        validate_component(&run_id)?;
        Ok(Self {
            root: root.into(),
            test_id,
            run_id,
            started: Instant::now(),
            processes: Vec::new(),
            attachments: Vec::new(),
        })
    }

    pub fn capture_process(&mut self, process: &mut SupervisedChild) -> io::Result<&mut Self> {
        let exit = process.refresh_status()?;
        self.processes.push(CapturedProcess {
            name: process.name().to_owned(),
            command_line: process.command_line().to_owned(),
            exit,
            stdout: process.stdout()?,
            stderr: process.stderr()?,
        });
        Ok(self)
    }

    pub fn attach(&mut self, attachment: Attachment) -> io::Result<&mut Self> {
        validate_component(&attachment.name)?;
        if !attachment.source.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!(
                    "attachment {} is not a regular file",
                    attachment.source.display()
                ),
            ));
        }
        self.attachments.push(attachment);
        Ok(self)
    }

    pub fn emit(self) -> io::Result<PathBuf> {
        let bundle = self.root.join(self.test_id.slug()).join(&self.run_id);
        let parent = bundle.parent().expect("a bundle always has a parent");
        fs::create_dir_all(parent)?;
        fs::create_dir(&bundle).map_err(|error| {
            if error.kind() == io::ErrorKind::AlreadyExists {
                io::Error::new(
                    error.kind(),
                    format!(
                        "failure bundle {} already exists; refusing to overwrite evidence",
                        bundle.display()
                    ),
                )
            } else {
                error
            }
        })?;

        let mut commands = File::create(bundle.join("command-lines.txt"))?;
        let mut statuses = File::create(bundle.join("process-status.json"))?;
        writeln!(statuses, "[")?;
        for (index, process) in self.processes.iter().enumerate() {
            let name = safe_component(&process.name);
            fs::write(bundle.join(format!("{name}.stdout")), &process.stdout)?;
            fs::write(bundle.join(format!("{name}.stderr")), &process.stderr)?;
            writeln!(commands, "{}: {}", process.name, process.command_line)?;
            let comma = if index + 1 == self.processes.len() {
                ""
            } else {
                ","
            };
            match process.exit {
                Some(exit) => writeln!(
                    statuses,
                    "  {{\"name\":\"{}\",\"state\":\"exited\",\"success\":{},\"code\":{},\"signal\":{}}}{comma}",
                    json_escape(&process.name),
                    exit.success,
                    json_option_i32(exit.code),
                    json_option_i32(exit.signal),
                )?,
                None => writeln!(
                    statuses,
                    "  {{\"name\":\"{}\",\"state\":\"running\",\"success\":null,\"code\":null,\"signal\":null}}{comma}",
                    json_escape(&process.name),
                )?,
            }
        }
        writeln!(statuses, "]")?;

        let attachments_dir = bundle.join("attachments");
        if !self.attachments.is_empty() {
            fs::create_dir(&attachments_dir)?;
        }
        let mut attachment_manifest = Vec::new();
        for attachment in &self.attachments {
            let destination = attachments_dir.join(&attachment.name);
            fs::copy(&attachment.source, &destination)?;
            attachment_manifest.push((attachment.name.clone(), sha256_file(&destination)?));
        }

        let mut manifest = File::create(bundle.join("manifest.json"))?;
        writeln!(manifest, "{{")?;
        writeln!(manifest, "  \"schema_version\": 1,")?;
        writeln!(
            manifest,
            "  \"test_name\": \"{}\",",
            json_escape(self.test_id.name())
        )?;
        writeln!(manifest, "  \"test_id\": \"{}\",", self.test_id.slug())?;
        writeln!(manifest, "  \"seed\": {},", self.test_id.seed())?;
        writeln!(manifest, "  \"run_id\": \"{}\",", json_escape(&self.run_id))?;
        writeln!(
            manifest,
            "  \"elapsed_monotonic_ns\": {},",
            self.started.elapsed().as_nanos()
        )?;
        writeln!(manifest, "  \"process_count\": {},", self.processes.len())?;
        writeln!(manifest, "  \"attachments\": [")?;
        for (index, (name, digest)) in attachment_manifest.iter().enumerate() {
            let comma = if index + 1 == attachment_manifest.len() {
                ""
            } else {
                ","
            };
            writeln!(
                manifest,
                "    {{\"name\":\"{}\",\"sha256\":\"{}\"}}{comma}",
                json_escape(name),
                digest
            )?;
        }
        writeln!(manifest, "  ]")?;
        writeln!(manifest, "}}")?;
        Ok(bundle)
    }
}

fn validate_component(value: &str) -> io::Result<()> {
    if value.is_empty()
        || value == "."
        || value == ".."
        || value.contains('/')
        || value.contains('\\')
    {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("artifact name must be one safe path component: {value:?}"),
        ));
    }
    Ok(())
}

fn safe_component(value: &str) -> String {
    value
        .chars()
        .map(|character| {
            if character.is_ascii_alphanumeric() || character == '-' || character == '_' {
                character
            } else {
                '-'
            }
        })
        .collect()
}

fn json_option_i32(value: Option<i32>) -> String {
    value.map_or_else(|| "null".to_owned(), |value| value.to_string())
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
            character if character.is_control() => {
                use std::fmt::Write as _;
                write!(output, "\\u{:04x}", character as u32)
                    .expect("writing to String cannot fail");
            }
            character => output.push(character),
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ProcessSpec;
    use std::time::Duration;

    #[cfg(unix)]
    #[test]
    fn emits_machine_readable_process_artifacts() {
        let root =
            std::env::temp_dir().join(format!("gw-test-support-failure-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let process_dir = root.join("processes");
        let mut child = ProcessSpec::new("peer", "sh")
            .args(["-c", "printf hello; printf nasty >&2; exit 3"])
            .spawn(&process_dir)
            .unwrap();
        child.wait_timeout(Duration::from_secs(1)).unwrap().unwrap();

        let attachment = root.join("trace.jsonl");
        fs::write(&attachment, b"{\"event\":\"ready\"}\n").unwrap();
        let mut builder = FailureBundle::new(
            root.join("bundles"),
            TestId::with_seed("bundle", 9),
            "attempt-1",
        )
        .unwrap();
        builder.capture_process(&mut child).unwrap();
        builder
            .attach(Attachment::new("trace.jsonl", &attachment))
            .unwrap();
        let bundle = builder.emit().unwrap();

        assert_eq!(fs::read(bundle.join("peer.stdout")).unwrap(), b"hello");
        let status = fs::read_to_string(bundle.join("process-status.json")).unwrap();
        assert!(status.contains("\"code\":3"));
        let manifest = fs::read_to_string(bundle.join("manifest.json")).unwrap();
        assert!(manifest.contains("\"schema_version\": 1"));
        assert!(manifest.contains("\"sha256\":"));
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn rejects_path_traversal_in_bundle_names() {
        let error = FailureBundle::new("failures", TestId::new("test"), "../escape").unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::InvalidInput);
    }

    #[test]
    fn json_escaping_handles_diagnostics() {
        assert_eq!(json_escape("quote\" newline\n"), "quote\\\" newline\\n");
    }
}
