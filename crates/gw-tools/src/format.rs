use std::fmt::Write;

use gw_wire::OutputKind;
use gw_wire::compositor::Transform;

use crate::OutputSnapshot;
use crate::control_format::{
    push_output_vrr_json, push_output_vrr_text, push_window_vrr_json, push_window_vrr_text,
};

#[must_use]
pub fn format_outputs(snapshot: &OutputSnapshot, json: bool) -> String {
    if json {
        format_outputs_json(snapshot)
    } else {
        format_outputs_text(snapshot)
    }
}

#[must_use]
pub fn format_windows(snapshot: &OutputSnapshot, json: bool) -> String {
    if json {
        let mut output = format!(
            "{{\"layout_generation\":{},\"windows\":",
            snapshot.generation
        );
        push_windows_json(&mut output, snapshot);
        output.push_str("}\n");
        output
    } else {
        let mut output = format_header_text(snapshot);
        push_windows_text(&mut output, snapshot);
        output
    }
}

#[must_use]
pub fn format_all(snapshot: &OutputSnapshot, json: bool) -> String {
    if json {
        let mut output = format!(
            "{{\"layout_generation\":{},\"root_width\":{},\"root_height\":{},\"primary_output_id\":\"{}\",\"outputs\":",
            snapshot.generation,
            snapshot.root_width,
            snapshot.root_height,
            output_id(snapshot.primary_output_id)
        );
        push_outputs_json(&mut output, snapshot);
        output.push_str(",\"windows\":");
        push_windows_json(&mut output, snapshot);
        output.push_str("}\n");
        output
    } else {
        let mut output = format_header_text(snapshot);
        push_outputs_text(&mut output, snapshot);
        push_windows_text(&mut output, snapshot);
        output
    }
}

fn format_outputs_json(snapshot: &OutputSnapshot) -> String {
    let mut output = String::new();
    write!(
        output,
        "{{\"layout_generation\":{},\"root_width\":{},\"root_height\":{},\"primary_output_id\":\"{}\",\"outputs\":",
        snapshot.generation,
        snapshot.root_width,
        snapshot.root_height,
        output_id(snapshot.primary_output_id)
    )
    .expect("writing into a String cannot fail");
    push_outputs_json(&mut output, snapshot);
    output.push_str("}\n");
    output
}

fn push_outputs_json(output: &mut String, snapshot: &OutputSnapshot) {
    output.push('[');
    for (index, (id, state)) in snapshot.outputs.iter().enumerate() {
        if index != 0 {
            output.push(',');
        }
        let metadata = snapshot.descriptors.get(id);
        let name = metadata.map_or("", |value| value.name.as_str());
        let kind = metadata.map_or("unknown", |value| kind_name(value.kind));
        let capabilities = metadata.map_or(0, |value| value.capability_flags);
        let physical_width_mm = metadata.map_or(0, |value| value.physical_width_millimeters);
        let physical_height_mm = metadata.map_or(0, |value| value.physical_height_millimeters);
        write!(
            output,
            "{{\"id\":\"{}\",\"name\":{},\"kind\":{},\"enabled\":{},\"connected\":{},\"primary\":{},\"physical_width\":{},\"physical_height\":{},\"physical_width_mm\":{},\"physical_height_mm\":{},\"refresh_millihertz\":{},\"logical_x\":{},\"logical_y\":{},\"logical_width\":{},\"logical_height\":{},\"scale_numerator\":{},\"scale_denominator\":{},\"transform\":{},\"capabilities\":{},\"modes\":[",
            output_id(*id),
            json_string(name),
            json_string(kind),
            boolean(state.enabled),
            boolean(capabilities & 1 != 0),
            boolean(*id == snapshot.primary_output_id),
            state.physical_pixel_width,
            state.physical_pixel_height,
            physical_width_mm,
            physical_height_mm,
            state.refresh_millihertz,
            state.logical_x,
            state.logical_y,
            state.logical_width,
            state.logical_height,
            state.scale_numerator,
            state.scale_denominator,
            json_string(transform_name(state.transform)),
            capabilities,
        )
        .expect("writing into a String cannot fail");
        let mut first_mode = true;
        for mode in snapshot.modes.iter().filter(|mode| mode.output_id == *id) {
            if !first_mode {
                output.push(',');
            }
            first_mode = false;
            write!(
                output,
                "{{\"id\":\"{}\",\"width\":{},\"height\":{},\"refresh_millihertz\":{},\"preferred\":{},\"current\":{}}}",
                output_id(mode.mode_id),
                mode.physical_width,
                mode.physical_height,
                mode.refresh_millihertz,
                boolean(mode.preferred),
                boolean(mode.current),
            )
            .expect("writing into a String cannot fail");
        }
        output.push(']');
        if snapshot.vrr_queried {
            push_output_vrr_json(output, snapshot, *id);
        }
        output.push('}');
    }
    output.push(']');
}

fn format_outputs_text(snapshot: &OutputSnapshot) -> String {
    let mut output = format_header_text(snapshot);
    push_outputs_text(&mut output, snapshot);
    output
}

fn format_header_text(snapshot: &OutputSnapshot) -> String {
    format!(
        "layout generation={} root={}x{} primary={}\n",
        snapshot.generation,
        snapshot.root_width,
        snapshot.root_height,
        output_id(snapshot.primary_output_id)
    )
}

fn push_outputs_text(output: &mut String, snapshot: &OutputSnapshot) {
    for (id, state) in &snapshot.outputs {
        let metadata = snapshot.descriptors.get(id);
        let name = metadata.map_or("unknown", |value| value.name.as_str());
        let kind = metadata.map_or("unknown", |value| kind_name(value.kind));
        let capabilities = metadata.map_or(0, |value| value.capability_flags);
        write!(
            output,
            "output {} name={} kind={} enabled={} connected={} primary={} physical={}x{}@{} logical={},{} {}x{} scale={}/{} transform={} capabilities={}",
            output_id(*id),
            name,
            kind,
            boolean(state.enabled),
            boolean(capabilities & 1 != 0),
            boolean(*id == snapshot.primary_output_id),
            state.physical_pixel_width,
            state.physical_pixel_height,
            state.refresh_millihertz,
            state.logical_x,
            state.logical_y,
            state.logical_width,
            state.logical_height,
            state.scale_numerator,
            state.scale_denominator,
            transform_name(state.transform),
            capabilities,
        )
        .expect("writing into a String cannot fail");
        if snapshot.vrr_queried {
            push_output_vrr_text(output, snapshot, *id);
        }
        output.push('\n');
        for mode in snapshot.modes.iter().filter(|mode| mode.output_id == *id) {
            writeln!(
                output,
                "  mode {} {}x{}@{} preferred={} current={}",
                output_id(mode.mode_id),
                mode.physical_width,
                mode.physical_height,
                mode.refresh_millihertz,
                boolean(mode.preferred),
                boolean(mode.current),
            )
            .expect("writing into a String cannot fail");
        }
    }
}

fn push_windows_json(output: &mut String, snapshot: &OutputSnapshot) {
    output.push('[');
    for (index, (&window_id, window)) in snapshot.windows.iter().enumerate() {
        if index != 0 {
            output.push(',');
        }
        write!(
            output,
            "{{\"window_id\":{},\"logical_x\":{},\"logical_y\":{},\"logical_width\":{},\"logical_height\":{},\"primary_output_id\":\"{}\",\"output_ids\":[",
            window_id,
            window.logical_x,
            window.logical_y,
            window.logical_width,
            window.logical_height,
            output_id(window.primary_output_id),
        )
        .expect("writing into a String cannot fail");
        for (index, output_value) in window.output_ids.iter().enumerate() {
            if index != 0 {
                output.push(',');
            }
            write!(output, "\"{}\"", output_id(*output_value))
                .expect("writing into a String cannot fail");
        }
        write!(
            output,
            "],\"preferred_scale_numerator\":{},\"preferred_scale_denominator\":{},\"client_buffer_scale\":{},\"scale_mode\":{},\"visible\":{},\"focused\":{},\"fullscreen\":{}",
            window.preferred_scale_numerator,
            window.preferred_scale_denominator,
            window.client_buffer_scale,
            json_string(match window.scale_mode {
                gw_wire::SurfaceScaleMode::ScaledPixmap => "scaled-pixmap",
                gw_wire::SurfaceScaleMode::Legacy => "legacy",
            }),
            boolean(window.visible),
            boolean(window.focused),
            boolean(window.fullscreen),
        )
        .expect("writing into a String cannot fail");
        if snapshot.vrr_queried {
            push_window_vrr_json(output, snapshot, window_id);
        }
        output.push('}');
    }
    output.push(']');
}

fn push_windows_text(output: &mut String, snapshot: &OutputSnapshot) {
    for (&window_id, window) in &snapshot.windows {
        write!(
            output,
            "window {} logical={},{} {}x{} primary={} outputs=",
            window_id,
            window.logical_x,
            window.logical_y,
            window.logical_width,
            window.logical_height,
            output_id(window.primary_output_id),
        )
        .expect("writing into a String cannot fail");
        for (index, output_value) in window.output_ids.iter().enumerate() {
            if index != 0 {
                output.push(',');
            }
            output.push_str(&output_id(*output_value));
        }
        write!(
            output,
            " preferred-scale={}/{} client-buffer-scale={} mode={} visible={} focused={} fullscreen={}",
            window.preferred_scale_numerator,
            window.preferred_scale_denominator,
            window.client_buffer_scale,
            match window.scale_mode {
                gw_wire::SurfaceScaleMode::ScaledPixmap => "scaled-pixmap",
                gw_wire::SurfaceScaleMode::Legacy => "legacy",
            },
            boolean(window.visible),
            boolean(window.focused),
            boolean(window.fullscreen),
        )
        .expect("writing into a String cannot fail");
        if snapshot.vrr_queried {
            push_window_vrr_text(output, snapshot, window_id);
        }
        output.push('\n');
    }
}

fn output_id(value: u64) -> String {
    format!("{value:016x}")
}

fn boolean(value: bool) -> &'static str {
    if value { "true" } else { "false" }
}

fn kind_name(value: OutputKind) -> &'static str {
    match value {
        OutputKind::Headless => "headless",
        OutputKind::Drm => "drm",
    }
}

fn transform_name(value: Transform) -> &'static str {
    match value {
        Transform::Normal => "normal",
        Transform::Rotate90 => "rotate-90",
        Transform::Rotate180 => "rotate-180",
        Transform::Rotate270 => "rotate-270",
        Transform::Flipped => "flipped",
        Transform::Flipped90 => "flipped-90",
        Transform::Flipped180 => "flipped-180",
        Transform::Flipped270 => "flipped-270",
    }
}

fn json_string(value: &str) -> String {
    let mut output = String::with_capacity(value.len() + 2);
    output.push('"');
    for character in value.chars() {
        match character {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\u{8}' => output.push_str("\\b"),
            '\u{c}' => output.push_str("\\f"),
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
    use std::collections::BTreeMap;

    use gw_wire::compositor::{OutputUpsert, SdrColorMetadata, Transform};
    use gw_wire::{OutputDescriptorUpsert, OutputKind, OutputModeUpsert};

    use super::*;

    fn snapshot() -> OutputSnapshot {
        let descriptor = OutputDescriptorUpsert {
            output_id: 11,
            kind: OutputKind::Headless,
            capability_flags: 59,
            name: "LEFT".to_owned(),
            physical_width_millimeters: 0,
            physical_height_millimeters: 0,
            supported_transform_mask: 0xff,
            minimum_scale_numerator: 1,
            minimum_scale_denominator: 1,
            maximum_scale_numerator: 4,
            maximum_scale_denominator: 1,
            maximum_scale_denominator_value: 120,
            maximum_physical_width: 4096,
            maximum_physical_height: 4096,
        };
        let state = OutputUpsert {
            output_id: 11,
            enabled: true,
            logical_x: 0,
            logical_y: 0,
            logical_width: 640,
            logical_height: 480,
            physical_pixel_width: 640,
            physical_pixel_height: 480,
            refresh_millihertz: 60_000,
            scale_numerator: 1,
            scale_denominator: 1,
            transform: Transform::Normal,
            color: SdrColorMetadata::default(),
        };
        OutputSnapshot {
            generation: 1,
            primary_output_id: 11,
            root_width: 640,
            root_height: 480,
            enabled_output_count: 1,
            descriptors: BTreeMap::from([(11, descriptor)]),
            modes: vec![OutputModeUpsert {
                output_id: 11,
                mode_id: 21,
                physical_width: 640,
                physical_height: 480,
                refresh_millihertz: 60_000,
                preferred: true,
                current: true,
                flags: 0,
            }],
            outputs: BTreeMap::from([(11, state)]),
            ..OutputSnapshot::default()
        }
    }

    #[test]
    fn json_is_byte_stable_and_newline_terminated() {
        assert_eq!(
            format_outputs(&snapshot(), true),
            "{\"layout_generation\":1,\"root_width\":640,\"root_height\":480,\"primary_output_id\":\"000000000000000b\",\"outputs\":[{\"id\":\"000000000000000b\",\"name\":\"LEFT\",\"kind\":\"headless\",\"enabled\":true,\"connected\":true,\"primary\":true,\"physical_width\":640,\"physical_height\":480,\"physical_width_mm\":0,\"physical_height_mm\":0,\"refresh_millihertz\":60000,\"logical_x\":0,\"logical_y\":0,\"logical_width\":640,\"logical_height\":480,\"scale_numerator\":1,\"scale_denominator\":1,\"transform\":\"normal\",\"capabilities\":59,\"modes\":[{\"id\":\"0000000000000015\",\"width\":640,\"height\":480,\"refresh_millihertz\":60000,\"preferred\":true,\"current\":true}]}]}\n"
        );
    }

    #[test]
    fn text_matches_legacy_field_order() {
        assert_eq!(
            format_outputs(&snapshot(), false),
            "layout generation=1 root=640x480 primary=000000000000000b\noutput 000000000000000b name=LEFT kind=headless enabled=true connected=true primary=true physical=640x480@60000 logical=0,0 640x480 scale=1/1 transform=normal capabilities=59\n  mode 0000000000000015 640x480@60000 preferred=true current=true\n"
        );
    }

    #[test]
    fn json_escaping_matches_the_legacy_formatter() {
        assert_eq!(json_string("a\n\"\\\u{1}"), "\"a\\n\\\"\\\\\\u0001\"");
    }
}
