use crate::TestId;
use std::env;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// A deterministic temporary runtime directory removed on drop by default.
///
/// Its path is `<base>/<test slug>`. Existing paths cause creation to fail so
/// stale artifacts are never silently overwritten.
#[derive(Debug)]
pub struct RuntimeDir {
    path: PathBuf,
    preserve: bool,
}

impl RuntimeDir {
    pub fn create(test_id: &TestId) -> io::Result<Self> {
        let base = env::var_os("GW_TEST_RUNTIME_ROOT")
            .map(PathBuf::from)
            .unwrap_or_else(|| env::temp_dir().join("glasswyrm-tests"));
        Self::create_in(base, test_id)
    }

    pub fn create_in(base: impl AsRef<Path>, test_id: &TestId) -> io::Result<Self> {
        fs::create_dir_all(base.as_ref())?;
        let path = base.as_ref().join(test_id.slug());
        fs::create_dir(&path).map_err(|error| {
            if error.kind() == io::ErrorKind::AlreadyExists {
                io::Error::new(
                    error.kind(),
                    format!(
                        "runtime directory {} already exists; remove the stale test artifact or use a different seed",
                        path.display()
                    ),
                )
            } else {
                error
            }
        })?;
        fs::create_dir(path.join("processes"))?;
        fs::create_dir(path.join("fixtures"))?;
        Ok(Self {
            path,
            preserve: false,
        })
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn processes_dir(&self) -> PathBuf {
        self.path.join("processes")
    }

    pub fn fixtures_dir(&self) -> PathBuf {
        self.path.join("fixtures")
    }

    /// Keeps the directory after this value is dropped, usually after failure.
    pub fn preserve(&mut self) {
        self.preserve = true;
    }

    /// Transfers ownership of the directory to the caller without deleting it.
    pub fn into_path(mut self) -> PathBuf {
        self.preserve = true;
        self.path.clone()
    }
}

impl Drop for RuntimeDir {
    fn drop(&mut self) {
        if !self.preserve {
            let _ = fs::remove_dir_all(&self.path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_base() -> PathBuf {
        env::temp_dir().join(format!("gw-test-support-runtime-{}", std::process::id()))
    }

    #[test]
    fn creates_a_deterministic_layout_and_cleans_it() {
        let id = TestId::with_seed("runtime layout", 1);
        let expected = test_base().join(id.slug());
        {
            let runtime = RuntimeDir::create_in(test_base(), &id).unwrap();
            assert_eq!(runtime.path(), expected);
            assert!(runtime.processes_dir().is_dir());
            assert!(runtime.fixtures_dir().is_dir());
        }
        assert!(!expected.exists());
    }

    #[test]
    fn refuses_to_overwrite_a_stale_directory() {
        let id = TestId::with_seed("stale", 2);
        let runtime = RuntimeDir::create_in(test_base(), &id).unwrap();
        let error = RuntimeDir::create_in(test_base(), &id).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::AlreadyExists);
        drop(runtime);
    }
}
