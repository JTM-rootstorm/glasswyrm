use std::collections::BTreeMap;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::os::unix::fs::OpenOptionsExt;
use std::path::{Path, PathBuf};

use gw_wire::compositor::{
    OutputUpsert, SurfaceUpsert, encode_output_upsert, encode_surface_upsert,
};
use gw_wire::{PolicyAppliedState, SurfacePolicyUpsert, encode_surface_policy_upsert};

const O_NOFOLLOW: i32 = 0o400_000;
const FNV_OFFSET: u64 = 14_695_981_039_346_656_037;
const FNV_PRIME: u64 = 1_099_511_628_211;

pub struct SceneManifest {
    path: PathBuf,
}

impl SceneManifest {
    pub fn prepare(path: &Path) -> io::Result<Self> {
        prepare_parent(path)?;
        reject_unsafe_target(path)?;
        Ok(Self {
            path: path.to_owned(),
        })
    }

    pub fn append(
        &self,
        commit_id: u64,
        generation: u64,
        output: &OutputUpsert,
        surfaces: &BTreeMap<u64, SurfaceUpsert>,
        policies: &BTreeMap<u64, SurfacePolicyUpsert>,
    ) -> io::Result<()> {
        let line = describe(commit_id, generation, output, surfaces, policies)?;
        let mut file = open_regular_append(&self.path)?;
        file.write_all(line.as_bytes())?;
        file.sync_data()
    }
}

fn describe(
    commit_id: u64,
    generation: u64,
    output: &OutputUpsert,
    surfaces: &BTreeMap<u64, SurfaceUpsert>,
    policies: &BTreeMap<u64, SurfacePolicyUpsert>,
) -> io::Result<String> {
    let mut ordinary: Vec<_> = surfaces
        .values()
        .filter(|surface| surface.presentation_flags & 2 == 0)
        .collect();
    ordinary.sort_by_key(|surface| {
        if surface.visible {
            (0, surface.stacking, surface.surface_id)
        } else {
            (
                1,
                i32::try_from(surface.x11_window_id).unwrap_or(i32::MAX),
                surface.surface_id,
            )
        }
    });
    let cursors: Vec<_> = surfaces
        .values()
        .filter(|surface| surface.presentation_flags & 2 != 0)
        .collect();
    if cursors.len() > 1 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "scene manifest has more than one cursor surface",
        ));
    }

    let mut hash = fnv_append(FNV_OFFSET, b"glasswyrm-scene-v1");
    hash = fnv_append(hash, &encode_output_upsert(output));
    for surface in &ordinary {
        let policy = policies.get(&surface.surface_id).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "scene manifest surface lacks policy metadata",
            )
        })?;
        hash = fnv_append(hash, &encode_surface_upsert(surface));
        hash = fnv_append(hash, &encode_surface_policy_upsert(policy));
    }
    if let Some(cursor) = cursors.first() {
        hash = fnv_append(hash, &encode_surface_upsert(cursor));
    }

    let mut line = format!(
        "{{\"commit_id\":{commit_id},\"generation\":{generation},\"output_id\":{},\"scene_hash\":\"{hash:016x}\",\"surface_count\":{},\"surfaces\":[",
        output.output_id,
        ordinary.len()
    );
    for (index, surface) in ordinary.iter().enumerate() {
        let policy = &policies[&surface.surface_id];
        if index != 0 {
            line.push(',');
        }
        line.push_str(&format!(
            "{{\"surface_id\":{},\"x11_window_id\":{},\"workspace_id\":{},\"x\":{},\"y\":{},\"width\":{},\"height\":{},\"stacking\":{},\"visible\":{},\"metadata_only\":{},\"focused\":{},\"managed\":{},\"decoration_eligible\":{},\"override_redirect\":{},\"applied_state\":\"{}\",\"fullscreen_eligible\":\"{}\",\"direct_scanout_eligible\":\"{}\"}}",
            surface.surface_id,
            surface.x11_window_id,
            policy.workspace_id,
            surface.logical_x,
            surface.logical_y,
            surface.logical_width,
            surface.logical_height,
            surface.stacking,
            surface.visible,
            surface.presentation_flags == 1,
            policy.focused,
            policy.managed,
            policy.decoration_eligible,
            policy.override_redirect,
            applied(policy.applied_state),
            tri_state(policy.fullscreen_eligible),
            tri_state(policy.direct_scanout_eligible),
        ));
    }
    line.push(']');
    if let Some(cursor) = cursors.first() {
        line.push_str(&format!(
            ",\"cursor_surface\":{{\"surface_id\":{},\"output_id\":{},\"x\":{},\"y\":{},\"width\":{},\"height\":{},\"visible\":{},\"format\":\"ARGB8888Premultiplied\"}}",
            cursor.surface_id,
            cursor.output_id,
            cursor.logical_x,
            cursor.logical_y,
            cursor.logical_width,
            cursor.logical_height,
            cursor.visible,
        ));
    }
    line.push_str("}\n");
    Ok(line)
}

fn applied(value: PolicyAppliedState) -> &'static str {
    match value {
        PolicyAppliedState::Normal => "Normal",
        PolicyAppliedState::Maximized => "Maximized",
        PolicyAppliedState::Fullscreen => "Fullscreen",
        PolicyAppliedState::Minimized => "Minimized",
    }
}

fn tri_state(value: u16) -> &'static str {
    match value {
        1 => "False",
        2 => "True",
        _ => "Unknown",
    }
}

fn fnv_append(mut hash: u64, bytes: &[u8]) -> u64 {
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    hash
}

fn prepare_parent(path: &Path) -> io::Result<()> {
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    match fs::symlink_metadata(parent) {
        Ok(metadata) if metadata.file_type().is_symlink() || !metadata.is_dir() => {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "evidence parent must be a real directory",
            ))
        }
        Ok(_) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => {
            fs::create_dir_all(parent)?;
            let metadata = fs::symlink_metadata(parent)?;
            if metadata.file_type().is_symlink() || !metadata.is_dir() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "evidence parent must be a real directory",
                ));
            }
            Ok(())
        }
        Err(error) => Err(error),
    }
}

fn reject_unsafe_target(path: &Path) -> io::Result<()> {
    match fs::symlink_metadata(path) {
        Ok(metadata) if metadata.file_type().is_symlink() || !metadata.is_file() => {
            Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "evidence target must be a regular file, not a symbolic link",
            ))
        }
        Ok(_) => Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error),
    }
}

fn open_regular_append(path: &Path) -> io::Result<File> {
    reject_unsafe_target(path)?;
    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .mode(0o600)
        .custom_flags(O_NOFOLLOW)
        .open(path)?;
    if !file.metadata()?.file_type().is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "evidence target must be a regular file",
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
            "gwcomp-manifest-{}-{label}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }

    #[test]
    fn rejects_symbolic_link_target() {
        let target = path("target");
        let link = path("link");
        fs::write(&target, b"private").unwrap();
        symlink(&target, &link).unwrap();
        assert!(SceneManifest::prepare(&link).is_err());
        assert_eq!(fs::read(&target).unwrap(), b"private");
        fs::remove_file(link).unwrap();
        fs::remove_file(target).unwrap();
    }
}
