use std::fmt::Write;

use gw_wire::OutputConfigurationAcknowledged;
use gw_wire::vrr::{VrrDecision, VrrPolicyMode, VrrWindowPreference};

use crate::OutputSnapshot;

const REASONS: [&str; 33] = [
    "output-disabled",
    "output-not-connected",
    "output-not-drm",
    "output-not-vrr-capable",
    "atomic-kms-unavailable",
    "vrr-property-missing",
    "vrr-atomic-test-failed",
    "session-inactive",
    "vt-suspended",
    "output-configuration-busy",
    "policy-off",
    "no-candidate",
    "window-missing",
    "window-hidden",
    "window-unmanaged",
    "window-unfocused",
    "window-not-fullscreen",
    "window-not-borderless-fullscreen",
    "window-spans-outputs",
    "window-preference-disabled",
    "window-did-not-request",
    "surface-missing",
    "surface-metadata-only",
    "surface-not-visible",
    "surface-not-opaque",
    "surface-on-wrong-output",
    "surface-membership-invalid",
    "presenter-rejected",
    "property-readback-mismatch",
    "timing-unavailable",
    "hardware-behavior-unconfirmed",
    "simulated-headless",
    "manual-always-eligible",
];

#[must_use]
pub fn format_acknowledgement(
    acknowledgement: &OutputConfigurationAcknowledged,
    json: bool,
) -> String {
    if json {
        format!(
            "{{\"request_id\":{},\"result\":{},\"applied_generation\":{},\"primary_output_id\":\"{:016x}\",\"root_width\":{},\"root_height\":{},\"enabled_output_count\":{}}}\n",
            acknowledgement.request_id,
            acknowledgement.result as u16,
            acknowledgement.applied_generation,
            acknowledgement.primary_output_id,
            acknowledgement.root_logical_width,
            acknowledgement.root_logical_height,
            acknowledgement.enabled_output_count,
        )
    } else {
        format!(
            "configuration result={} generation={} primary={:016x} root={}x{} enabled={}\n",
            acknowledgement.result as u16,
            acknowledgement.applied_generation,
            acknowledgement.primary_output_id,
            acknowledgement.root_logical_width,
            acknowledgement.root_logical_height,
            acknowledgement.enabled_output_count,
        )
    }
}

#[must_use]
pub fn format_vrr(snapshot: &OutputSnapshot, selector: &str, json: bool) -> String {
    if json {
        format_vrr_json(snapshot, selector)
    } else {
        format_vrr_text(snapshot, selector)
    }
}

fn format_vrr_json(snapshot: &OutputSnapshot, selector: &str) -> String {
    let mut output = String::from("{\"vrr\":[");
    let mut first = true;
    for (&output_id, capability) in &snapshot.vrr_capabilities {
        if !output_matches(snapshot, output_id, selector) {
            continue;
        }
        if !first {
            output.push(',');
        }
        first = false;
        let name = snapshot
            .descriptors
            .get(&output_id)
            .map_or("", |descriptor| descriptor.name.as_str());
        let policy = snapshot
            .vrr_policies
            .get(&output_id)
            .copied()
            .unwrap_or(VrrPolicyMode::Off);
        write!(
            output,
            "{{\"id\":\"0x{output_id:016x}\",\"name\":{},\"policy\":\"{}\",\"property_present\":{},\"hardware_capable\":{},\"kms_controllable\":{},\"simulated\":{},\"range_millihertz\":",
            json_string(name),
            policy_name(policy),
            capability.connector_property_present,
            capability.hardware_capable,
            capability.kms_controllable,
            capability.simulated,
        )
        .expect("writing into a String cannot fail");
        if capability.range_available {
            write!(
                output,
                "[{},{}]",
                capability.minimum_refresh_millihertz, capability.maximum_refresh_millihertz
            )
            .expect("writing into a String cannot fail");
        } else {
            output.push_str("null");
        }
        let reasons = if let Some(state) = snapshot.vrr_outputs.get(&output_id) {
            write!(
                output,
                ",\"decision\":\"{}\",\"desired_enabled\":{},\"effective_enabled\":{},\"candidate_window\":{},\"transition_serial\":{},\"flip_timestamp_monotonic_ns\":{},\"interval_ns\":{}",
                decision_name(state.decision),
                state.desired_enabled,
                state.effective_enabled,
                state.candidate_window_id,
                state.transition_serial,
                state.last_flip_timestamp_nanoseconds,
                state.last_interval_nanoseconds,
            )
            .expect("writing into a String cannot fail");
            state.reason_flags
        } else {
            capability.reason_flags
        };
        output.push_str(",\"reasons\":");
        push_reasons_json(&mut output, reasons);
        if let Some(timing) = snapshot.vrr_timings.get(&output_id) {
            write!(
                output,
                ",\"latest_timing_interval_ns\":{}",
                timing.interval_nanoseconds
            )
            .expect("writing into a String cannot fail");
        }
        output.push('}');
    }
    output.push_str("],\"windows\":[");
    first = true;
    for (&window_id, window) in &snapshot.vrr_windows {
        if !output_matches(snapshot, window.output_id, selector) {
            continue;
        }
        if !first {
            output.push(',');
        }
        first = false;
        write!(
            output,
            "{{\"window\":{window_id},\"surface\":\"0x{:016x}\",\"output\":\"0x{:016x}\",\"preference\":\"{}\",\"policy_eligible\":{},\"selected\":{},\"focused\":{},\"fullscreen\":{},\"borderless_fullscreen\":{},\"exclusive_output_membership\":{},\"policy_generation\":{},\"reasons\":",
            window.surface_id,
            window.output_id,
            preference_name(window.preference),
            window.policy_eligible,
            window.policy_selected,
            window.focused,
            window.fullscreen,
            window.borderless_fullscreen,
            window.exclusive_output_membership,
            window.policy_generation,
        )
        .expect("writing into a String cannot fail");
        push_reasons_json(&mut output, window.reason_flags);
        output.push('}');
    }
    output.push_str("]}\n");
    output
}

fn format_vrr_text(snapshot: &OutputSnapshot, selector: &str) -> String {
    let mut output = String::new();
    for (&output_id, capability) in &snapshot.vrr_capabilities {
        if !output_matches(snapshot, output_id, selector) {
            continue;
        }
        let name = snapshot
            .descriptors
            .get(&output_id)
            .map_or("", |descriptor| descriptor.name.as_str());
        let policy = snapshot
            .vrr_policies
            .get(&output_id)
            .copied()
            .unwrap_or(VrrPolicyMode::Off);
        write!(
            output,
            "0x{output_id:016x} {name} policy={} property_present={} hardware={} controllable={} simulated={} range=",
            policy_name(policy),
            u8::from(capability.connector_property_present),
            u8::from(capability.hardware_capable),
            u8::from(capability.kms_controllable),
            u8::from(capability.simulated),
        )
        .expect("writing into a String cannot fail");
        if capability.range_available {
            write!(
                output,
                "{}-{}",
                capability.minimum_refresh_millihertz, capability.maximum_refresh_millihertz
            )
            .expect("writing into a String cannot fail");
        } else {
            output.push_str("unavailable");
        }
        let reasons = if let Some(state) = snapshot.vrr_outputs.get(&output_id) {
            write!(
                output,
                " decision={} desired={} effective={} candidate_window={} transition_serial={} flip_timestamp_monotonic_ns={} interval_ns={}",
                decision_name(state.decision),
                u8::from(state.desired_enabled),
                u8::from(state.effective_enabled),
                state.candidate_window_id,
                state.transition_serial,
                state.last_flip_timestamp_nanoseconds,
                state.last_interval_nanoseconds,
            )
            .expect("writing into a String cannot fail");
            state.reason_flags
        } else {
            capability.reason_flags
        };
        if let Some(timing) = snapshot.vrr_timings.get(&output_id) {
            write!(
                output,
                " latest_timing_interval_ns={}",
                timing.interval_nanoseconds
            )
            .expect("writing into a String cannot fail");
        }
        output.push_str(" reasons=");
        push_reasons_text(&mut output, reasons);
        output.push('\n');
    }
    for (&window_id, window) in &snapshot.vrr_windows {
        if !output_matches(snapshot, window.output_id, selector) {
            continue;
        }
        write!(
            output,
            "window={window_id} surface=0x{:016x} output=0x{:016x} preference={} eligible={} selected={} focused={} fullscreen={} borderless={} exclusive={} policy_generation={} reasons=",
            window.surface_id,
            window.output_id,
            preference_name(window.preference),
            u8::from(window.policy_eligible),
            u8::from(window.policy_selected),
            u8::from(window.focused),
            u8::from(window.fullscreen),
            u8::from(window.borderless_fullscreen),
            u8::from(window.exclusive_output_membership),
            window.policy_generation,
        )
        .expect("writing into a String cannot fail");
        push_reasons_text(&mut output, window.reason_flags);
        output.push('\n');
    }
    output
}

fn output_matches(snapshot: &OutputSnapshot, output_id: u64, selector: &str) -> bool {
    snapshot
        .descriptors
        .get(&output_id)
        .is_some_and(|descriptor| descriptor.name == selector)
        || selector
            .strip_prefix("0x")
            .and_then(|value| u64::from_str_radix(value, 16).ok())
            .or_else(|| {
                (selector.len() == 16)
                    .then(|| u64::from_str_radix(selector, 16).ok())
                    .flatten()
            })
            .or_else(|| selector.parse().ok())
            == Some(output_id)
}

fn policy_name(value: VrrPolicyMode) -> &'static str {
    match value {
        VrrPolicyMode::Off => "off",
        VrrPolicyMode::Fullscreen => "fullscreen",
        VrrPolicyMode::Focused => "focused",
        VrrPolicyMode::AppRequested => "app-requested",
        VrrPolicyMode::AlwaysEligible => "always-eligible",
    }
}

fn decision_name(value: VrrDecision) -> &'static str {
    match value {
        VrrDecision::Disabled => "disabled",
        VrrDecision::Enabled => "enabled",
        VrrDecision::Unsupported => "unsupported",
        VrrDecision::Rejected => "rejected",
    }
}

fn preference_name(value: VrrWindowPreference) -> &'static str {
    match value {
        VrrWindowPreference::Default => "default",
        VrrWindowPreference::Disable => "disable",
        VrrWindowPreference::Allow => "allow",
        VrrWindowPreference::Prefer => "prefer",
    }
}

fn push_reasons_json(output: &mut String, reasons: u64) {
    output.push('[');
    let mut first = true;
    for (bit, name) in REASONS.iter().enumerate() {
        if reasons & (1_u64 << bit) == 0 {
            continue;
        }
        if !first {
            output.push(',');
        }
        first = false;
        write!(output, "\"{name}\"").expect("writing into a String cannot fail");
    }
    output.push(']');
}

fn push_reasons_text(output: &mut String, reasons: u64) {
    output.push('[');
    let mut first = true;
    for (bit, name) in REASONS.iter().enumerate() {
        if reasons & (1_u64 << bit) == 0 {
            continue;
        }
        if !first {
            output.push(',');
        }
        first = false;
        output.push_str(name);
    }
    output.push(']');
}

fn json_string(value: &str) -> String {
    let mut output = String::with_capacity(value.len() + 2);
    output.push('"');
    for character in value.chars() {
        match character {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            value if value < '\u{20}' => {
                write!(output, "\\u{:04x}", value as u32)
                    .expect("writing into a String cannot fail");
            }
            value => output.push(value),
        }
    }
    output.push('"');
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use gw_wire::OutputConfigurationResult;

    #[test]
    fn acknowledgement_json_matches_the_legacy_schema() {
        let value = OutputConfigurationAcknowledged {
            request_id: 2,
            applied_generation: 3,
            result: OutputConfigurationResult::Accepted,
            flags: 0,
            primary_output_id: 11,
            root_logical_width: 1280,
            root_logical_height: 480,
            enabled_output_count: 2,
        };
        assert_eq!(
            format_acknowledgement(&value, true),
            "{\"request_id\":2,\"result\":1,\"applied_generation\":3,\"primary_output_id\":\"000000000000000b\",\"root_width\":1280,\"root_height\":480,\"enabled_output_count\":2}\n"
        );
    }
}
