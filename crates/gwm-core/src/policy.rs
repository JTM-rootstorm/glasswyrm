use std::collections::{BTreeMap, BTreeSet};

use gw_types::{Generation, OutputId, WindowId};

use crate::geometry::OutputSelection;
use crate::model::{
    AppliedState, DecorationPreference, EvaluationError, KNOWN_WINDOW_FLAGS, MAXIMUM_OUTPUTS,
    MAXIMUM_ROOT_EXTENT, MAXIMUM_WINDOW_EXTENT, MAXIMUM_WINDOWS, MAXIMUM_WORK_EXTENT,
    OutputContext, RawState, RawWindow, Rectangle, StackMode, TriState, WINDOW_ABOVE,
    WINDOW_INPUT_DISABLED, WindowState, WindowType,
};
use crate::{initial_placement, retain_visible_pixel, select_output};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyState {
    pub generation: Generation,
    pub context: crate::Context,
    pub outputs: BTreeMap<OutputId, OutputContext>,
    pub windows: BTreeMap<WindowId, WindowState>,
    /// Bottom-to-top visible order followed by hidden windows in ID order.
    pub output_order: Vec<WindowId>,
}

fn extent_fits(origin: i32, extent: u32) -> bool {
    extent != 0 && i64::from(origin) + i64::from(extent) - 1 <= i64::from(i32::MAX)
}

fn rectangle_valid(rectangle: Rectangle, maximum: u32) -> bool {
    rectangle.x >= 0
        && rectangle.y >= 0
        && rectangle.width != 0
        && rectangle.height != 0
        && rectangle.width <= maximum
        && rectangle.height <= maximum
        && extent_fits(rectangle.x, rectangle.width)
        && extent_fits(rectangle.y, rectangle.height)
}

fn contains(outer: Rectangle, inner: Rectangle) -> bool {
    inner.x >= outer.x
        && inner.y >= outer.y
        && i64::from(inner.x) + i64::from(inner.width)
            <= i64::from(outer.x) + i64::from(outer.width)
        && i64::from(inner.y) + i64::from(inner.height)
            <= i64::from(outer.y) + i64::from(outer.height)
}

fn overlaps(left: Rectangle, right: Rectangle) -> bool {
    i64::from(left.x) < i64::from(right.x) + i64::from(right.width)
        && i64::from(right.x) < i64::from(left.x) + i64::from(left.width)
        && i64::from(left.y) < i64::from(right.y) + i64::from(right.height)
        && i64::from(right.y) < i64::from(left.y) + i64::from(left.height)
}

fn validate(raw: &RawState, generation: Generation) -> Result<(), EvaluationError> {
    if !raw.complete {
        return Err(EvaluationError::IncompleteSnapshot);
    }
    if generation.get() == 0 {
        return Err(EvaluationError::InvalidGeneration);
    }
    let maximum_extent = if raw.outputs.is_empty() {
        MAXIMUM_WORK_EXTENT
    } else {
        MAXIMUM_ROOT_EXTENT
    };
    if raw.context.root_window_id.get() == 0
        || raw.context.workspace_id == 0
        || raw.context.primary_output_id.get() == 0
        || raw.context.work.width == 0
        || raw.context.work.height == 0
        || raw.context.work.width > maximum_extent
        || raw.context.work.height > maximum_extent
        || !extent_fits(raw.context.work.x, raw.context.work.width)
        || !extent_fits(raw.context.work.y, raw.context.work.height)
    {
        return Err(EvaluationError::InvalidContext);
    }
    if raw.windows.len() > MAXIMUM_WINDOWS || raw.outputs.len() > MAXIMUM_OUTPUTS {
        return Err(EvaluationError::Limit);
    }
    if raw.outputs.is_empty() {
        if !raw.output_hints.is_empty() {
            return Err(EvaluationError::UnsupportedMetadata);
        }
    } else {
        validate_outputs(raw)?;
    }

    let mut creation_serials = BTreeSet::new();
    for (id, window) in &raw.windows {
        if *id != window.window_id
            || id.get() == 0
            || *id == raw.context.root_window_id
            || window.parent_window_id != raw.context.root_window_id
            || window.creation_serial == 0
            || window.requested.width == 0
            || window.requested.height == 0
            || window.requested.width > MAXIMUM_WINDOW_EXTENT
            || window.requested.height > MAXIMUM_WINDOW_EXTENT
            || window.flags & !KNOWN_WINDOW_FLAGS != 0
            || (window.wants_map && window.map_serial == 0)
            || !extent_fits(window.requested.x, window.requested.width)
            || !extent_fits(window.requested.y, window.requested.height)
            || !creation_serials.insert(window.creation_serial)
        {
            return Err(EvaluationError::InvalidWindow);
        }
        if window
            .workspace_id
            .is_some_and(|workspace| workspace != raw.context.workspace_id)
        {
            return Err(EvaluationError::UnsupportedMetadata);
        }
        if window.transient_for == Some(*id) {
            return Err(EvaluationError::InvalidWindow);
        }
        if let Some(parent) = window.transient_for {
            let Some(parent_window) = raw.windows.get(&parent) else {
                return Err(EvaluationError::UnknownReference);
            };
            if parent_window.override_redirect {
                return Err(EvaluationError::UnknownReference);
            }
        }
        match (window.stack_serial, window.stack_mode) {
            (0, StackMode::None) => {
                if window.stack_sibling.is_some() {
                    return Err(EvaluationError::InvalidWindow);
                }
            }
            (0, _) | (_, StackMode::None) => return Err(EvaluationError::InvalidWindow),
            _ => {
                if window.transient_for.is_some() {
                    return Err(EvaluationError::UnsupportedMetadata);
                }
                if let Some(sibling) = window.stack_sibling {
                    if sibling == *id {
                        return Err(EvaluationError::InvalidWindow);
                    }
                    let Some(sibling_window) = raw.windows.get(&sibling) else {
                        return Err(EvaluationError::UnknownReference);
                    };
                    if sibling_window.transient_for.is_some()
                        || sibling_window.override_redirect != window.override_redirect
                    {
                        return Err(EvaluationError::UnsupportedMetadata);
                    }
                }
            }
        }
    }
    for id in raw.windows.keys() {
        let mut visited = BTreeSet::new();
        let mut current = Some(*id);
        while let Some(window_id) = current {
            if !visited.insert(window_id) {
                return Err(EvaluationError::InvalidWindow);
            }
            current = raw
                .windows
                .get(&window_id)
                .and_then(|window| window.transient_for);
        }
    }
    if raw
        .output_hints
        .keys()
        .any(|window_id| !raw.windows.contains_key(window_id))
    {
        return Err(EvaluationError::InvalidWindow);
    }
    Ok(())
}

fn validate_outputs(raw: &RawState) -> Result<(), EvaluationError> {
    let enabled: Vec<_> = raw
        .outputs
        .values()
        .filter(|output| output.enabled)
        .collect();
    if enabled.is_empty()
        || enabled.iter().filter(|output| output.primary).count() != 1
        || raw
            .outputs
            .get(&raw.context.primary_output_id)
            .is_none_or(|output| !output.enabled || !output.primary)
    {
        return Err(EvaluationError::InvalidContext);
    }
    for (id, output) in &raw.outputs {
        if *id != output.output_id
            || id.get() == 0
            || (output.primary && !output.enabled)
            || (output.enabled
                && (!rectangle_valid(output.logical, MAXIMUM_WORK_EXTENT)
                    || !rectangle_valid(output.work, MAXIMUM_WORK_EXTENT)
                    || !contains(output.logical, output.work)))
            || (!output.enabled
                && (output.logical != Rectangle::default()
                    || output.work != Rectangle::default()
                    || output.primary))
        {
            return Err(EvaluationError::InvalidContext);
        }
    }
    for (index, left) in enabled.iter().enumerate() {
        if enabled[index + 1..]
            .iter()
            .any(|right| overlaps(left.logical, right.logical))
        {
            return Err(EvaluationError::InvalidContext);
        }
    }
    Ok(())
}

fn applied_state(window: &RawWindow) -> AppliedState {
    if window.override_redirect {
        AppliedState::Normal
    } else if window.minimized_requested {
        AppliedState::Minimized
    } else if window.fullscreen_requested {
        AppliedState::Fullscreen
    } else if window.maximized_requested {
        AppliedState::Maximized
    } else {
        AppliedState::Normal
    }
}

fn decorated(window: &RawWindow, state: AppliedState) -> bool {
    if window.override_redirect
        || state == AppliedState::Fullscreen
        || window.decoration_preference == DecorationPreference::False
    {
        return false;
    }
    match window.window_type {
        WindowType::Normal | WindowType::Dialog => true,
        WindowType::Utility | WindowType::Unknown => {
            window.decoration_preference == DecorationPreference::True
        }
    }
}

fn cascade_candidate(window: &RawWindow, state: AppliedState) -> bool {
    !window.override_redirect
        && window.transient_for.is_none()
        && window.geometry_serial == 0
        && state == AppliedState::Normal
        && matches!(
            window.window_type,
            WindowType::Normal | WindowType::Utility | WindowType::Unknown
        )
}

fn initial_output(raw: &RawState, window_id: WindowId) -> OutputId {
    let hint = raw
        .output_hints
        .get(&window_id)
        .copied()
        .unwrap_or_default();
    if raw
        .outputs
        .get(&hint.preferred_output_id)
        .is_some_and(|output| output.enabled)
    {
        return hint.preferred_output_id;
    }
    raw.context.primary_output_id
}

fn parent_first(raw: &RawState) -> Vec<WindowId> {
    fn depth(raw: &RawState, id: WindowId) -> usize {
        let mut depth = 0;
        let mut current = raw.windows[&id].transient_for;
        while let Some(parent) = current {
            depth += 1;
            current = raw.windows[&parent].transient_for;
        }
        depth
    }
    let mut ordered: Vec<_> = raw
        .windows
        .iter()
        .filter_map(|(id, window)| {
            (!window.override_redirect && window.transient_for.is_some()).then_some(*id)
        })
        .collect();
    ordered.sort_by_key(|id| (depth(raw, *id), *id));
    ordered
}

fn clamp_coordinate(value: i64, low: i32, high: i64) -> i32 {
    i32::try_from(value.clamp(i64::from(low), high)).expect("validated geometry fits i32")
}

fn apply_geometry(raw: &RawState, windows: &mut BTreeMap<WindowId, WindowState>) {
    if raw.outputs.is_empty() {
        let mut cascade: Vec<_> = raw
            .windows
            .values()
            .filter(|window| cascade_candidate(window, applied_state(window)) && window.wants_map)
            .collect();
        cascade.sort_by_key(|window| (window.creation_serial, window.window_id));
        let slots: BTreeMap<_, _> = cascade
            .iter()
            .enumerate()
            .map(|(slot, window)| (window.window_id, slot))
            .collect();
        for (id, window) in &raw.windows {
            let state = &mut windows.get_mut(id).expect("all states initialized");
            state.output_id = raw.context.primary_output_id;
            state.geometry.width = if window.override_redirect {
                window.requested.width
            } else {
                window.requested.width.min(raw.context.work.width)
            };
            state.geometry.height = if window.override_redirect {
                window.requested.height
            } else {
                window.requested.height.min(raw.context.work.height)
            };
            if window.override_redirect {
                state.geometry.x = window.requested.x;
                state.geometry.y = window.requested.y;
            } else if matches!(
                state.applied_state,
                AppliedState::Fullscreen | AppliedState::Maximized
            ) {
                state.geometry = raw.context.work;
            } else if window.transient_for.is_none() && window.geometry_serial != 0 {
                state.geometry.x = clamp_coordinate(
                    i64::from(window.requested.x),
                    raw.context.work.x,
                    i64::from(raw.context.work.x) + i64::from(raw.context.work.width)
                        - i64::from(state.geometry.width),
                );
                state.geometry.y = clamp_coordinate(
                    i64::from(window.requested.y),
                    raw.context.work.y,
                    i64::from(raw.context.work.y) + i64::from(raw.context.work.height)
                        - i64::from(state.geometry.height),
                );
            } else if window.transient_for.is_none() {
                let output = OutputContext {
                    output_id: raw.context.primary_output_id,
                    logical: raw.context.work,
                    work: raw.context.work,
                    enabled: true,
                    primary: true,
                };
                state.geometry = initial_placement(
                    output,
                    state.geometry.width,
                    state.geometry.height,
                    slots.get(id).copied().unwrap_or(0),
                );
            }
        }
    } else {
        let mut roots: Vec<_> = raw
            .windows
            .iter()
            .filter(|(_, window)| window.override_redirect || window.transient_for.is_none())
            .map(|(id, _)| *id)
            .collect();
        roots.sort_by_key(|id| (raw.windows[id].creation_serial, *id));
        let mut cascade_slots = BTreeMap::<OutputId, usize>::new();
        for id in roots {
            let window = &raw.windows[&id];
            let state = windows.get_mut(&id).expect("all states initialized");
            let hint = raw.output_hints.get(&id).copied().unwrap_or_default();
            let cascade = cascade_candidate(window, state.applied_state);
            state.output_id = if cascade {
                initial_output(raw, id)
            } else {
                select_output(
                    &raw.outputs,
                    raw.context.primary_output_id,
                    OutputSelection {
                        geometry: window.requested,
                        previous_output_id: hint.previous_output_id,
                        preferred_output_id: hint.preferred_output_id,
                        retain_previous: matches!(
                            state.applied_state,
                            AppliedState::Fullscreen | AppliedState::Maximized
                        ),
                        ..OutputSelection::default()
                    },
                )
            };
            let output = raw.outputs[&state.output_id];
            let slot = *cascade_slots.get(&state.output_id).unwrap_or(&0);
            state.geometry = match state.applied_state {
                AppliedState::Fullscreen | AppliedState::Maximized => output.work,
                _ if window.override_redirect => window.requested,
                _ if window.geometry_serial == 0 => initial_placement(
                    output,
                    window.requested.width,
                    window.requested.height,
                    slot,
                ),
                _ => retain_visible_pixel(&raw.outputs, state.output_id, window.requested),
            };
            if cascade && window.wants_map {
                *cascade_slots.entry(state.output_id).or_default() += 1;
            }
        }
    }

    for id in parent_first(raw) {
        let window = &raw.windows[&id];
        let parent = windows[&window.transient_for.expect("transient has parent")];
        let state = windows.get_mut(&id).expect("all states initialized");
        state.output_id = parent.output_id;
        let bounds = if raw.outputs.is_empty() {
            raw.context.work
        } else {
            raw.outputs[&state.output_id].work
        };
        if matches!(
            state.applied_state,
            AppliedState::Fullscreen | AppliedState::Maximized
        ) {
            state.geometry = bounds;
            continue;
        }
        state.geometry.width = window.requested.width.min(bounds.width);
        state.geometry.height = window.requested.height.min(bounds.height);
        let centered = Rectangle {
            x: i32::try_from(
                i64::from(parent.geometry.x)
                    + (i64::from(parent.geometry.width) - i64::from(state.geometry.width)) / 2,
            )
            .expect("validated geometry fits i32"),
            y: i32::try_from(
                i64::from(parent.geometry.y)
                    + (i64::from(parent.geometry.height) - i64::from(state.geometry.height)) / 2,
            )
            .expect("validated geometry fits i32"),
            width: state.geometry.width,
            height: state.geometry.height,
        };
        state.geometry = if raw.outputs.is_empty() {
            Rectangle {
                x: clamp_coordinate(
                    i64::from(centered.x),
                    bounds.x,
                    i64::from(bounds.x) + i64::from(bounds.width) - i64::from(centered.width),
                ),
                y: clamp_coordinate(
                    i64::from(centered.y),
                    bounds.y,
                    i64::from(bounds.y) + i64::from(bounds.height) - i64::from(centered.height),
                ),
                ..centered
            }
        } else {
            retain_visible_pixel(&raw.outputs, state.output_id, centered)
        };
    }
}

fn apply_visibility_and_focus(raw: &RawState, windows: &mut BTreeMap<WindowId, WindowState>) {
    for (id, window) in &raw.windows {
        let state = windows.get_mut(id).expect("all states initialized");
        state.visible = window.wants_map
            && (window.override_redirect || state.applied_state != AppliedState::Minimized);
    }
    for id in parent_first(raw) {
        let parent_visible = windows[&raw.windows[&id]
            .transient_for
            .expect("transient has parent")]
            .visible;
        windows
            .get_mut(&id)
            .expect("all states initialized")
            .visible &= parent_visible;
    }
    for state in windows.values_mut() {
        state.fullscreen_eligible = if state.override_redirect {
            TriState::Unknown
        } else if state.visible && state.applied_state == AppliedState::Fullscreen {
            TriState::True
        } else {
            TriState::False
        };
    }

    let candidates: Vec<_> = raw
        .windows
        .iter()
        .filter_map(|(id, window)| {
            (windows[id].visible
                && !window.override_redirect
                && window.flags & WINDOW_INPUT_DISABLED == 0)
                .then_some(*id)
        })
        .collect();
    let has_explicit = candidates
        .iter()
        .any(|id| raw.windows[id].focus_serial != 0);
    let has_fullscreen = candidates
        .iter()
        .any(|id| windows[id].applied_state == AppliedState::Fullscreen);
    let rank = |id: WindowId| {
        let window = &raw.windows[&id];
        let focus_rank = if has_fullscreen && window.transient_for.is_some() {
            2
        } else if windows[&id].applied_state == AppliedState::Fullscreen {
            1
        } else {
            0
        };
        (
            focus_rank,
            if has_explicit { window.focus_serial } else { 0 },
            window.map_serial,
            window.creation_serial,
            id,
        )
    };
    if let Some(selected) = candidates.into_iter().max_by_key(|id| rank(*id)) {
        windows
            .get_mut(&selected)
            .expect("candidate exists")
            .focused = true;
    }
    for state in windows.values_mut() {
        state.direct_scanout_eligible = if !state.override_redirect
            && state.fullscreen_eligible == TriState::True
            && !state.focused
        {
            TriState::False
        } else {
            TriState::Unknown
        };
    }
}

fn stack_key(raw: &RawState, id: WindowId) -> (bool, u64, u64, WindowId) {
    let window = &raw.windows[&id];
    (
        window.flags & WINDOW_ABOVE != 0,
        window.map_serial,
        window.creation_serial,
        id,
    )
}

fn apply_restack(raw: &RawState, band: &mut Vec<WindowId>) {
    let mut operations: Vec<_> = band
        .iter()
        .copied()
        .filter(|id| raw.windows[id].stack_serial != 0)
        .collect();
    operations.sort_by_key(|id| {
        let window = &raw.windows[id];
        (window.stack_serial, window.creation_serial, *id)
    });
    for id in operations {
        let window = &raw.windows[&id];
        band.retain(|candidate| *candidate != id);
        let index = match window.stack_sibling {
            None if window.stack_mode == StackMode::Above => band.len(),
            None => 0,
            Some(sibling) => {
                let sibling_index = band
                    .iter()
                    .position(|candidate| *candidate == sibling)
                    .expect("validated sibling remains in band");
                sibling_index + usize::from(window.stack_mode == StackMode::Above)
            }
        };
        band.insert(index, id);
    }
}

fn emit_transients(
    raw: &RawState,
    windows: &BTreeMap<WindowId, WindowState>,
    id: WindowId,
    out: &mut Vec<WindowId>,
) {
    out.push(id);
    let mut children: Vec<_> = raw
        .windows
        .iter()
        .filter_map(|(child_id, child)| {
            (!child.override_redirect
                && child.transient_for == Some(id)
                && windows[child_id].visible)
                .then_some(*child_id)
        })
        .collect();
    children.sort_by_key(|child| stack_key(raw, *child));
    for child in children {
        emit_transients(raw, windows, child, out);
    }
}

fn apply_stacking(raw: &RawState, windows: &mut BTreeMap<WindowId, WindowState>) -> Vec<WindowId> {
    let mut managed: Vec<_> = raw
        .windows
        .iter()
        .filter_map(|(id, window)| {
            (!window.override_redirect && window.transient_for.is_none()).then_some(*id)
        })
        .collect();
    let mut overrides: Vec<_> = raw
        .windows
        .iter()
        .filter_map(|(id, window)| window.override_redirect.then_some(*id))
        .collect();
    managed.sort_by_key(|id| stack_key(raw, *id));
    overrides.sort_by_key(|id| stack_key(raw, *id));
    apply_restack(raw, &mut managed);
    apply_restack(raw, &mut overrides);
    managed.sort_by_key(|id| windows[id].applied_state == AppliedState::Fullscreen);
    managed.retain(|id| windows[id].visible);
    overrides.retain(|id| windows[id].visible);

    let mut order = Vec::new();
    for id in managed {
        emit_transients(raw, windows, id, &mut order);
    }
    order.extend(overrides);
    for (stacking, id) in order.iter().copied().enumerate() {
        windows.get_mut(&id).expect("stacked state exists").stacking =
            Some(u32::try_from(stacking).expect("window limit guarantees u32 stacking"));
    }
    order.extend(
        windows
            .iter()
            .filter_map(|(id, state)| (!state.visible).then_some(*id)),
    );
    order
}

/// Evaluates one complete window-policy snapshot.
pub fn evaluate(raw: &RawState, generation: Generation) -> Result<PolicyState, EvaluationError> {
    validate(raw, generation)?;
    let mut windows = raw
        .windows
        .iter()
        .map(|(id, window)| {
            let applied_state = applied_state(window);
            (
                *id,
                WindowState {
                    window_id: *id,
                    transient_for: window.transient_for,
                    workspace_id: raw.context.workspace_id,
                    window_type: window.window_type,
                    applied_state,
                    managed: !window.override_redirect,
                    decoration_eligible: decorated(window, applied_state),
                    override_redirect: window.override_redirect,
                    attention_requested: window.attention_requested,
                    fullscreen_eligible: TriState::False,
                    ..WindowState::default()
                },
            )
        })
        .collect();
    apply_geometry(raw, &mut windows);
    apply_visibility_and_focus(raw, &mut windows);
    let output_order = apply_stacking(raw, &mut windows);
    Ok(PolicyState {
        generation,
        context: raw.context,
        outputs: raw.outputs.clone(),
        windows,
        output_order,
    })
}
