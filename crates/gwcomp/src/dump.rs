use std::fmt::Write as _;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::os::unix::fs::OpenOptionsExt;
use std::path::{Path, PathBuf};

use gwcomp_core::SoftwareFrameSet;

const O_NOFOLLOW: i32 = 0o400_000;
const MAXIMUM_DUMP_BYTES: u64 = 512 * 1024 * 1024;

pub struct FrameDumper {
    directory: PathBuf,
    written_bytes: u64,
    exhausted: bool,
}

impl FrameDumper {
    pub fn prepare(directory: &Path) -> io::Result<Self> {
        match fs::symlink_metadata(directory) {
            Ok(metadata) if metadata.file_type().is_symlink() => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "dump path must not be a symbolic link",
                ));
            }
            Ok(metadata) if !metadata.is_dir() => {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "dump path exists but is not a directory",
                ));
            }
            Ok(_) => {}
            Err(error) if error.kind() == io::ErrorKind::NotFound => fs::create_dir_all(directory)?,
            Err(error) => return Err(error),
        }
        let written_bytes = existing_regular_bytes(directory)?;
        let exhausted = written_bytes >= MAXIMUM_DUMP_BYTES;
        if exhausted {
            eprintln!(
                "gwcomp: frame dumping disabled because the directory reached the 512 MiB limit"
            );
        }
        Ok(Self {
            directory: directory.to_owned(),
            written_bytes,
            exhausted,
        })
    }

    pub fn dump(
        &mut self,
        frames: &SoftwareFrameSet,
        ordinal: u64,
        commit_id: u64,
        generation: u64,
    ) -> io::Result<()> {
        if self.exhausted {
            return Ok(());
        }
        let frame_set_line = frame_set_line(frames, ordinal, commit_id, generation);
        let mut additional_bytes = frame_set_line.len() as u64;
        for (&output_id, output) in frames.outputs() {
            let name = format!("frame-{ordinal:06}-output-{output_id:016x}.ppm");
            let header = format!(
                "P6\n{} {}\n255\n",
                output.output.width, output.output.height
            );
            let manifest_line = format!(
                "{{\"frame\":{ordinal},\"commit_id\":{commit_id},\"generation\":{generation},\"output_id\":{output_id},\"width\":{},\"height\":{},\"damage_rectangles\":{},\"fnv1a64\":\"{:016x}\",\"file\":\"{name}\"}}\n",
                output.output.width,
                output.output.height,
                output.damage.len(),
                output.visible_hash
            );
            let pixel_bytes = u64::try_from(output.frame.pixels().len())
                .ok()
                .and_then(|pixels| pixels.checked_mul(3))
                .ok_or_else(|| dump_limit_error("frame dump size overflow"))?;
            additional_bytes = additional_bytes
                .checked_add(header.len() as u64)
                .and_then(|total| total.checked_add(pixel_bytes))
                .and_then(|total| total.checked_add(manifest_line.len() as u64))
                .ok_or_else(|| dump_limit_error("frame dump size overflow"))?;
        }
        if !self.reserve(additional_bytes)? {
            return Ok(());
        }

        for (&output_id, output) in frames.outputs() {
            let name = format!("frame-{ordinal:06}-output-{output_id:016x}.ppm");
            let final_path = self.directory.join(&name);
            let temporary = self
                .directory
                .join(format!(".{name}.tmp.{}", std::process::id()));
            let mut bytes = format!(
                "P6\n{} {}\n255\n",
                output.output.width, output.output.height
            )
            .into_bytes();
            bytes.reserve(output.frame.pixels().len() * 3);
            for pixel in output.frame.pixels() {
                let [_, red, green, blue] = pixel.to_be_bytes();
                bytes.extend_from_slice(&[red, green, blue]);
            }
            {
                let mut file = OpenOptions::new()
                    .create_new(true)
                    .write(true)
                    .open(&temporary)?;
                file.write_all(&bytes)?;
                file.sync_all()?;
            }
            fs::rename(&temporary, &final_path)?;
            let mut manifest = open_regular_append(&self.directory.join("frames.jsonl"))?;
            writeln!(
                manifest,
                "{{\"frame\":{ordinal},\"commit_id\":{commit_id},\"generation\":{generation},\"output_id\":{output_id},\"width\":{},\"height\":{},\"damage_rectangles\":{},\"fnv1a64\":\"{:016x}\",\"file\":\"{name}\"}}",
                output.output.width,
                output.output.height,
                output.damage.len(),
                output.visible_hash
            )?;
            manifest.flush()?;
        }
        let mut manifest = open_regular_append(&self.directory.join("frame-sets.jsonl"))?;
        manifest.write_all(frame_set_line.as_bytes())?;
        manifest.flush()
    }

    fn reserve(&mut self, bytes: u64) -> io::Result<bool> {
        if self.exhausted {
            return Ok(false);
        }
        let new_total = self
            .written_bytes
            .checked_add(bytes)
            .ok_or_else(|| dump_limit_error("frame dump size overflow"))?;
        if new_total > MAXIMUM_DUMP_BYTES {
            self.exhausted = true;
            eprintln!(
                "gwcomp: frame dumping disabled because the directory reached the 512 MiB limit"
            );
            return Ok(false);
        }
        self.written_bytes = new_total;
        Ok(true)
    }
}

fn existing_regular_bytes(directory: &Path) -> io::Result<u64> {
    fs::read_dir(directory)?.try_fold(0_u64, |total, entry| {
        let metadata = fs::symlink_metadata(entry?.path())?;
        if metadata.file_type().is_file() {
            total
                .checked_add(metadata.len())
                .ok_or_else(|| dump_limit_error("frame dump directory size overflow"))
        } else {
            Ok(total)
        }
    })
}

fn frame_set_line(
    frames: &SoftwareFrameSet,
    ordinal: u64,
    commit_id: u64,
    generation: u64,
) -> String {
    let mut line = format!(
        "{{\"schema_version\":13,\"transaction_ordinal\":{ordinal},\"commit_id\":{commit_id},\"generation\":{generation},\"layout_generation\":{},\"primary_output_id\":\"{:016x}\",\"output_count\":{},\"aggregate_hash\":\"{:016x}\",\"outputs\":[",
        frames.layout_generation(),
        frames.primary_output_id(),
        frames.outputs().len(),
        frames.aggregate_hash()
    );
    for (index, (&output_id, output)) in frames.outputs().iter().enumerate() {
        if index != 0 {
            line.push(',');
        }
        let transform = match output.transform {
            gwcomp_core::OutputTransform::Normal => "normal",
            gwcomp_core::OutputTransform::Rotate90 => "rotate-90",
            gwcomp_core::OutputTransform::Rotate180 => "rotate-180",
            gwcomp_core::OutputTransform::Rotate270 => "rotate-270",
            gwcomp_core::OutputTransform::Flipped => "flipped",
            gwcomp_core::OutputTransform::Flipped90 => "flipped-90",
            gwcomp_core::OutputTransform::Flipped180 => "flipped-180",
            gwcomp_core::OutputTransform::Flipped270 => "flipped-270",
        };
        let _ = write!(
            line,
            "{{\"output_id\":\"{output_id:016x}\",\"file\":\"frame-{ordinal:06}-output-{output_id:016x}.ppm\",\"fnv1a64\":\"{:016x}\",\"physical\":{{\"width\":{},\"height\":{}}},\"logical\":{{\"x\":{},\"y\":{},\"width\":{},\"height\":{}}},\"scale\":{{\"numerator\":{},\"denominator\":{}}},\"transform\":\"{transform}\",\"damage\":[",
            output.visible_hash,
            output.output.width,
            output.output.height,
            output.logical.x,
            output.logical.y,
            output.logical.width,
            output.logical.height,
            output.scale.numerator,
            output.scale.denominator
        );
        for (damage_index, damage) in output.damage.iter().enumerate() {
            if damage_index != 0 {
                line.push(',');
            }
            let _ = write!(
                line,
                "{{\"x\":{},\"y\":{},\"width\":{},\"height\":{}}}",
                damage.x, damage.y, damage.width, damage.height
            );
        }
        line.push_str("]}");
    }
    line.push_str("]}\n");
    line
}

fn dump_limit_error(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::FileTooLarge, message)
}

fn open_regular_append(path: &Path) -> io::Result<File> {
    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .mode(0o600)
        .custom_flags(O_NOFOLLOW)
        .open(path)?;
    if !file.metadata()?.file_type().is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "frame manifest must be a regular file",
        ));
    }
    Ok(file)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::symlink;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT: AtomicU64 = AtomicU64::new(1);

    fn path(label: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "gwcomp-dump-{}-{label}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }

    #[test]
    fn manifest_append_does_not_follow_symbolic_links() {
        let target = path("target");
        let link = path("link");
        fs::write(&target, b"preserve").unwrap();
        symlink(&target, &link).unwrap();

        assert!(open_regular_append(&link).is_err());
        assert_eq!(fs::read(&target).unwrap(), b"preserve");

        fs::remove_file(link).unwrap();
        fs::remove_file(target).unwrap();
    }

    #[test]
    fn dump_budget_is_finite_and_checked_before_accounting() {
        let directory = path("budget");
        fs::create_dir(&directory).unwrap();
        let mut dumper = FrameDumper::prepare(&directory).unwrap();

        assert!(dumper.reserve(MAXIMUM_DUMP_BYTES).unwrap());
        assert!(!dumper.reserve(1).unwrap());
        assert_eq!(dumper.written_bytes, MAXIMUM_DUMP_BYTES);
        assert!(dumper.exhausted);

        fs::remove_dir(directory).unwrap();
    }

    #[test]
    fn existing_dump_files_count_toward_the_directory_budget() {
        let directory = path("existing-budget");
        fs::create_dir(&directory).unwrap();
        let existing = directory.join("existing.ppm");
        File::create(&existing)
            .unwrap()
            .set_len(MAXIMUM_DUMP_BYTES)
            .unwrap();

        let mut dumper = FrameDumper::prepare(&directory).unwrap();
        assert_eq!(dumper.written_bytes, MAXIMUM_DUMP_BYTES);
        assert!(dumper.exhausted);
        assert!(!dumper.reserve(1).unwrap());

        fs::remove_file(existing).unwrap();
        fs::remove_dir(directory).unwrap();
    }
}
