use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;

use gw_wire::vrr::{OutputVrrCapabilityUpsert, OutputVrrStateUpsert, PresentationTiming};

const O_NOFOLLOW: i32 = 0o400_000;
const MAXIMUM_INTERVALS_PER_OUTPUT: usize = 4096;

#[derive(Default)]
struct Summary {
    samples: u64,
    enabled: u64,
    disabled: u64,
    intervals: Vec<u64>,
}

pub struct VrrReport {
    file: File,
    summaries: BTreeMap<u64, Summary>,
    exhausted_outputs: BTreeSet<u64>,
    finished: bool,
}

impl VrrReport {
    pub fn create(path: &Path) -> io::Result<Self> {
        let parent = path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or(Path::new("."));
        let metadata = fs::symlink_metadata(parent)?;
        if metadata.file_type().is_symlink() || !metadata.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "VRR report parent must be a real directory",
            ));
        }
        let file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .custom_flags(O_NOFOLLOW)
            .open(path)?;
        Ok(Self {
            file,
            summaries: BTreeMap::new(),
            exhausted_outputs: BTreeSet::new(),
            finished: false,
        })
    }

    pub fn capability(&mut self, capability: &OutputVrrCapabilityUpsert) -> io::Result<()> {
        self.append(format!(
            "{{\"record\":\"capability\",\"backend\":\"headless\",\"device\":\"simulated\",\"driver\":\"headless\",\"connector\":\"simulated\",\"crtc\":0,\"mode\":\"configured\",\"output_id\":{},\"connector_property_present\":{},\"connector_property_value\":1,\"hardware_capable\":{},\"crtc_property_present\":true,\"crtc_property_id\":0,\"original_value\":0,\"atomic_test_off\":true,\"atomic_test_on\":true,\"range_source\":\"configured\",\"minimum_refresh_millihertz\":{},\"maximum_refresh_millihertz\":{},\"controllable\":{},\"simulated\":true}}\n",
            capability.output_id,
            capability.connector_property_present,
            capability.hardware_capable,
            capability.minimum_refresh_millihertz,
            capability.maximum_refresh_millihertz,
            capability.kms_controllable,
        ))
    }

    pub fn presentation(
        &mut self,
        state: &OutputVrrStateUpsert,
        timing: &PresentationTiming,
        nominal_interval_nanoseconds: u64,
    ) -> io::Result<()> {
        if self
            .summaries
            .get(&state.output_id)
            .is_some_and(|summary| summary.intervals.len() >= MAXIMUM_INTERVALS_PER_OUTPUT)
        {
            if self.exhausted_outputs.insert(state.output_id) {
                eprintln!(
                    "gwcomp: VRR timing report disabled for output {} after 4096 samples",
                    state.output_id
                );
            }
            return Ok(());
        }
        self.append(format!(
            "{{\"record\":\"decision\",\"commit_id\":{},\"generation\":{},\"output_id\":{},\"policy_mode\":{},\"candidate_window_id\":{},\"candidate_surface_id\":{},\"desired_enabled\":{},\"effective_enabled\":{},\"reason_mask\":{},\"reason_names\":{},\"session_active\":{},\"transition_serial\":{},\"simulated\":true}}\n",
            state.last_commit_id,
            state.last_presented_generation,
            state.output_id,
            state.requested_mode as u16,
            state.candidate_window_id,
            state.candidate_surface_id,
            state.desired_enabled,
            state.effective_enabled,
            state.reason_flags,
            reason_names(state.reason_flags),
            state.session_active,
            state.transition_serial,
        ))?;
        self.append(format!(
            "{{\"record\":\"timing\",\"commit_id\":{},\"generation\":{},\"output_id\":{},\"sequence\":{},\"kernel_timestamp_nanoseconds\":{},\"interval_nanoseconds\":{},\"nominal_mode_interval_nanoseconds\":{},\"effective_enabled\":{},\"simulated\":true}}\n",
            timing.commit_id,
            timing.presented_generation,
            timing.output_id,
            timing.flip_sequence,
            timing.kernel_timestamp_nanoseconds,
            timing.interval_nanoseconds,
            nominal_interval_nanoseconds,
            timing.effective_vrr_enabled,
        ))?;
        let summary = self.summaries.entry(state.output_id).or_default();
        summary.samples += 1;
        summary.enabled += u64::from(state.effective_enabled);
        summary.disabled += u64::from(!state.effective_enabled);
        summary.intervals.push(timing.interval_nanoseconds);
        Ok(())
    }

    pub fn finish(&mut self) -> io::Result<()> {
        if self.finished {
            return Ok(());
        }
        let summaries = std::mem::take(&mut self.summaries);
        for (output_id, mut summary) in summaries {
            summary.intervals.sort_unstable();
            let minimum = summary.intervals.first().copied().unwrap_or(0);
            let maximum = summary.intervals.last().copied().unwrap_or(0);
            let sum = summary
                .intervals
                .iter()
                .fold(0_u64, |sum, value| sum.saturating_add(*value));
            let mean = sum.checked_div(summary.samples).unwrap_or(0);
            let median = summary
                .intervals
                .get(summary.intervals.len() / 2)
                .copied()
                .unwrap_or(0);
            self.append(format!(
                "{{\"record\":\"summary\",\"output_id\":{output_id},\"sample_count\":{},\"enabled_periods\":{},\"disabled_periods\":{},\"minimum_nanoseconds\":{minimum},\"maximum_nanoseconds\":{maximum},\"mean_nanoseconds\":{mean},\"median_nanoseconds\":{median},\"simulated\":true}}\n",
                summary.samples, summary.enabled, summary.disabled,
            ))?;
        }
        self.append("{\"record\":\"restore\",\"original_value\":0,\"restored_value\":0,\"readback_success\":true,\"kms_status\":\"not_applicable\",\"vt_status\":\"not_applicable\",\"getty_status\":\"not_applicable\",\"simulated\":true}\n".to_owned())?;
        self.file.sync_all()?;
        self.finished = true;
        Ok(())
    }

    fn append(&mut self, line: String) -> io::Result<()> {
        self.file.write_all(line.as_bytes())
    }
}

fn reason_names(mask: u64) -> String {
    const NAMES: [&str; 33] = [
        "OutputDisabled",
        "OutputNotConnected",
        "OutputNotDrm",
        "OutputNotVrrCapable",
        "AtomicKmsUnavailable",
        "VrrPropertyMissing",
        "VrrAtomicTestFailed",
        "SessionInactive",
        "VtSuspended",
        "OutputConfigurationBusy",
        "PolicyOff",
        "NoCandidate",
        "WindowMissing",
        "WindowHidden",
        "WindowUnmanaged",
        "WindowUnfocused",
        "WindowNotFullscreen",
        "WindowNotBorderlessFullscreen",
        "WindowSpansOutputs",
        "WindowPreferenceDisabled",
        "WindowDidNotRequest",
        "SurfaceMissing",
        "SurfaceMetadataOnly",
        "SurfaceNotVisible",
        "SurfaceNotOpaque",
        "SurfaceOnWrongOutput",
        "SurfaceMembershipInvalid",
        "PresenterRejected",
        "PropertyReadbackMismatch",
        "TimingUnavailable",
        "HardwareBehaviorUnconfirmed",
        "SimulatedHeadless",
        "ManualAlwaysEligible",
    ];
    let names = NAMES
        .iter()
        .enumerate()
        .filter(|(bit, _)| mask & (1_u64 << bit) != 0)
        .map(|(_, name)| format!("\"{name}\""))
        .collect::<Vec<_>>()
        .join(",");
    format!("[{names}]")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT: AtomicU64 = AtomicU64::new(1);

    fn path() -> std::path::PathBuf {
        std::env::temp_dir().join(format!(
            "gwcomp-vrr-report-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }

    #[test]
    fn report_is_non_replacing() {
        let path = path();
        let mut first = VrrReport::create(&path).unwrap();
        assert!(VrrReport::create(&path).is_err());
        first.finish().unwrap();
        let contents = fs::read_to_string(&path).unwrap();
        assert!(contents.contains("\"record\":\"restore\""));
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn interval_retention_is_bounded_per_output() {
        let path = path();
        let mut report = VrrReport::create(&path).unwrap();
        report.summaries.insert(
            7,
            Summary {
                samples: MAXIMUM_INTERVALS_PER_OUTPUT as u64,
                enabled: MAXIMUM_INTERVALS_PER_OUTPUT as u64,
                disabled: 0,
                intervals: vec![16_666_666; MAXIMUM_INTERVALS_PER_OUTPUT],
            },
        );
        let state = OutputVrrStateUpsert {
            output_id: 7,
            requested_mode: gw_wire::vrr::VrrPolicyMode::Focused,
            decision: gw_wire::vrr::VrrDecision::Enabled,
            desired_enabled: true,
            effective_enabled: true,
            property_readback_valid: true,
            session_active: true,
            candidate_window_id: 1,
            candidate_surface_id: 2,
            reason_flags: 0,
            state_generation: 1,
            transition_serial: 1,
            last_commit_id: 1,
            last_presented_generation: 1,
            last_flip_sequence: 1,
            flags: 0,
            last_flip_timestamp_nanoseconds: 16_666_666,
            last_interval_nanoseconds: 16_666_666,
        };
        let timing = PresentationTiming {
            output_id: 7,
            commit_id: 1,
            presented_generation: 1,
            flip_sequence: 1,
            flags: 0,
            kernel_timestamp_nanoseconds: 16_666_666,
            interval_nanoseconds: 16_666_666,
            effective_vrr_enabled: true,
            timestamp_available: true,
        };

        let bytes_before = report.file.metadata().unwrap().len();
        report.presentation(&state, &timing, 16_666_666).unwrap();
        report.presentation(&state, &timing, 16_666_666).unwrap();
        assert_eq!(report.summaries[&7].intervals.len(), 4096);
        assert!(report.exhausted_outputs.contains(&7));
        assert_eq!(report.file.metadata().unwrap().len(), bytes_before);

        drop(report);
        fs::remove_file(path).unwrap();
    }
}
