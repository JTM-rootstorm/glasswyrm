use std::collections::BTreeMap;

use gw_types::OutputId;

use crate::{OutputContext, Rectangle};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct OutputSelection {
    pub geometry: Rectangle,
    pub inherited_output_id: OutputId,
    pub previous_output_id: OutputId,
    pub preferred_output_id: OutputId,
    pub retain_previous: bool,
}

fn intersection_area(left: Rectangle, right: Rectangle) -> u64 {
    let right_edge = i64::from(left.x) + i64::from(left.width);
    let other_right = i64::from(right.x) + i64::from(right.width);
    let bottom = i64::from(left.y) + i64::from(left.height);
    let other_bottom = i64::from(right.y) + i64::from(right.height);
    let width = (right_edge.min(other_right) - i64::from(left.x.max(right.x))).max(0);
    let height = (bottom.min(other_bottom) - i64::from(left.y.max(right.y))).max(0);
    (width as u64) * (height as u64)
}

fn enabled(outputs: &BTreeMap<OutputId, OutputContext>, id: OutputId) -> bool {
    outputs.get(&id).is_some_and(|output| output.enabled)
}

/// Selects an output using the legacy M13 intersection and tie-break rules.
#[must_use]
pub fn select_output(
    outputs: &BTreeMap<OutputId, OutputContext>,
    primary_output_id: OutputId,
    selection: OutputSelection,
) -> OutputId {
    if enabled(outputs, selection.inherited_output_id) {
        return selection.inherited_output_id;
    }
    if selection.retain_previous && enabled(outputs, selection.previous_output_id) {
        return selection.previous_output_id;
    }

    let maximum_area = outputs
        .values()
        .filter(|output| output.enabled)
        .map(|output| intersection_area(selection.geometry, output.logical))
        .max()
        .unwrap_or(0);
    let tied: Vec<_> = outputs
        .iter()
        .filter(|(_, output)| output.enabled)
        .filter_map(|(id, output)| {
            (intersection_area(selection.geometry, output.logical) == maximum_area).then_some(*id)
        })
        .collect();
    for candidate in [
        selection.previous_output_id,
        selection.preferred_output_id,
        primary_output_id,
    ] {
        if candidate.get() != 0 && tied.contains(&candidate) {
            return candidate;
        }
    }
    tied.first().copied().unwrap_or_default()
}

#[must_use]
pub fn initial_placement(
    output: OutputContext,
    width: u32,
    height: u32,
    cascade_slot: usize,
) -> Rectangle {
    let width = width.min(output.work.width);
    let height = height.min(output.work.height);
    let x_span = output.work.width - width;
    let y_span = output.work.height - height;
    Rectangle {
        x: output.work.x
            + i32::try_from(if x_span == 0 {
                0
            } else {
                (cascade_slot * 32) % (x_span as usize + 1)
            })
            .expect("validated work extents fit i32"),
        y: output.work.y
            + i32::try_from(if y_span == 0 {
                0
            } else {
                (cascade_slot * 32) % (y_span as usize + 1)
            })
            .expect("validated work extents fit i32"),
        width,
        height,
    }
}

/// Preserves the legacy rule that an off-screen managed window keeps one pixel visible.
#[must_use]
pub fn retain_visible_pixel(
    outputs: &BTreeMap<OutputId, OutputContext>,
    assigned_output_id: OutputId,
    mut geometry: Rectangle,
) -> Rectangle {
    if outputs
        .values()
        .any(|output| output.enabled && intersection_area(output.logical, geometry) != 0)
    {
        return geometry;
    }
    let Some(output) = outputs
        .get(&assigned_output_id)
        .filter(|output| output.enabled)
    else {
        return geometry;
    };
    let visible_coordinate = |coordinate: i32, origin: i32, extent: u32, window_extent: u32| {
        let low = i64::from(origin) - i64::from(window_extent) + 1;
        let high = i64::from(origin) + i64::from(extent) - 1;
        i32::try_from(i64::from(coordinate).clamp(low, high))
            .expect("validated output coordinates fit i32")
    };
    geometry.x = visible_coordinate(
        geometry.x,
        output.logical.x,
        output.logical.width,
        geometry.width,
    );
    geometry.y = visible_coordinate(
        geometry.y,
        output.logical.y,
        output.logical.height,
        geometry.height,
    );
    geometry
}
