use gw_wire::compositor::Transform;
use gw_wire::vrr::VrrPolicyMode;

use crate::OutputSnapshot;

const OUTPUT_CAP_ARBITRARY_HEADLESS_MODE: u32 = 1 << 1;
const OUTPUT_CAP_SCALE_CONFIGURABLE: u32 = 1 << 3;
const OUTPUT_CAP_TRANSFORM_CONFIGURABLE: u32 = 1 << 4;
const OUTPUT_CAP_PRIMARY_ELIGIBLE: u32 = 1 << 5;
const MAXIMUM_ROOT_LOGICAL_WIDTH: u64 = 32_767;
const MAXIMUM_ROOT_LOGICAL_HEIGHT: u64 = 32_767;

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct OutputEdit {
    pub enabled: Option<bool>,
    pub mode: Option<(u32, u32)>,
    pub refresh_millihertz: Option<u32>,
    pub position: Option<(i32, i32)>,
    pub scale: Option<(u32, u32)>,
    pub transform: Option<Transform>,
    pub vrr_policy: Option<VrrPolicyMode>,
    pub primary: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct EditError(pub(crate) String);

impl std::fmt::Display for EditError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl std::error::Error for EditError {}

pub fn apply_output_edit(
    snapshot: &mut OutputSnapshot,
    selector: &str,
    edit: &OutputEdit,
) -> Result<(), EditError> {
    let output_id = find_output(snapshot, selector)
        .ok_or_else(|| EditError(format!("unknown output '{selector}'")))?;
    let descriptor = snapshot
        .descriptors
        .get(&output_id)
        .cloned()
        .ok_or_else(|| EditError("output inventory is incomplete".to_owned()))?;
    let output = snapshot
        .outputs
        .get_mut(&output_id)
        .expect("selected output exists");
    let was_enabled = output.enabled;
    if let Some(enabled) = edit.enabled {
        output.enabled = enabled;
    }
    if !output.enabled && snapshot.primary_output_id == output.output_id {
        return Err(EditError(
            "refusing to disable the primary output without selecting another".to_owned(),
        ));
    }
    if edit.primary {
        if !output.enabled || descriptor.capability_flags & OUTPUT_CAP_PRIMARY_ELIGIBLE == 0 {
            return Err(EditError(
                "selected output cannot become primary".to_owned(),
            ));
        }
        snapshot.primary_output_id = output.output_id;
    }
    if !was_enabled && output.enabled && edit.mode.is_none() {
        let mode = snapshot
            .modes
            .iter()
            .find(|mode| mode.output_id == output_id && mode.current)
            .or_else(|| {
                snapshot
                    .modes
                    .iter()
                    .find(|mode| mode.output_id == output_id && mode.preferred)
            })
            .ok_or_else(|| EditError("re-enabled output requires an available mode".to_owned()))?;
        output.physical_pixel_width = mode.physical_width;
        output.physical_pixel_height = mode.physical_height;
        output.refresh_millihertz = mode.refresh_millihertz;
    }
    if let Some((width, height)) = edit.mode {
        if descriptor.kind == gw_wire::OutputKind::Drm {
            return Err(EditError(
                "DRM mode changes are not supported in Milestone 13".to_owned(),
            ));
        }
        let matching_mode = snapshot.modes.iter().find(|mode| {
            mode.output_id == output_id
                && mode.physical_width == width
                && mode.physical_height == height
                && edit
                    .refresh_millihertz
                    .is_none_or(|refresh| mode.refresh_millihertz == refresh)
        });
        if descriptor.capability_flags & OUTPUT_CAP_ARBITRARY_HEADLESS_MODE == 0
            && matching_mode.is_none()
        {
            return Err(EditError(
                "requested mode is not in the output inventory".to_owned(),
            ));
        }
        output.physical_pixel_width = width;
        output.physical_pixel_height = height;
        if let Some(refresh) = edit.refresh_millihertz {
            output.refresh_millihertz = refresh;
        } else if let Some(mode) = matching_mode {
            output.refresh_millihertz = mode.refresh_millihertz;
        }
    }
    if let Some((x, y)) = edit.position {
        output.logical_x = x;
        output.logical_y = y;
    }
    if let Some((numerator, denominator)) = edit.scale {
        if descriptor.capability_flags & OUTPUT_CAP_SCALE_CONFIGURABLE == 0
            || denominator > descriptor.maximum_scale_denominator_value
            || !scale_at_least(
                (numerator, denominator),
                descriptor.minimum_scale_numerator,
                descriptor.minimum_scale_denominator,
            )
            || !scale_at_most(
                (numerator, denominator),
                descriptor.maximum_scale_numerator,
                descriptor.maximum_scale_denominator,
            )
        {
            return Err(EditError("requested exact scale is unsupported".to_owned()));
        }
        output.scale_numerator = numerator;
        output.scale_denominator = denominator;
    }
    if let Some(transform) = edit.transform {
        let bit = 1_u32 << transform as u16;
        if descriptor.capability_flags & OUTPUT_CAP_TRANSFORM_CONFIGURABLE == 0
            || descriptor.supported_transform_mask & bit == 0
        {
            return Err(EditError("requested transform is unsupported".to_owned()));
        }
        output.transform = transform;
    }
    if !output.enabled {
        output.logical_x = 0;
        output.logical_y = 0;
        output.logical_width = 0;
        output.logical_height = 0;
        output.physical_pixel_width = 0;
        output.physical_pixel_height = 0;
        output.refresh_millihertz = 0;
    } else {
        let (physical_width, physical_height) = if swaps_axes(output.transform) {
            (output.physical_pixel_height, output.physical_pixel_width)
        } else {
            (output.physical_pixel_width, output.physical_pixel_height)
        };
        output.logical_width = logical_dimension(
            physical_width,
            output.scale_numerator,
            output.scale_denominator,
        );
        output.logical_height = logical_dimension(
            physical_height,
            output.scale_numerator,
            output.scale_denominator,
        );
    }
    validate_layout(snapshot)
}

pub fn apply_vrr_edit(
    snapshot: &mut OutputSnapshot,
    selector: &str,
    mode: VrrPolicyMode,
) -> Result<(), EditError> {
    let output_id = find_output(snapshot, selector)
        .ok_or_else(|| EditError(format!("unknown output '{selector}'")))?;
    let capability = snapshot
        .vrr_capabilities
        .get(&output_id)
        .ok_or_else(|| EditError("selected output has no negotiated VRR metadata".to_owned()))?;
    if mode != VrrPolicyMode::Off && !capability.kms_controllable {
        return Err(EditError(
            "selected output does not provide controllable VRR".to_owned(),
        ));
    }
    snapshot.vrr_policies.insert(output_id, mode);
    Ok(())
}

pub fn parse_transform(value: &str) -> Option<Transform> {
    match value {
        "normal" => Some(Transform::Normal),
        "rotate-90" => Some(Transform::Rotate90),
        "rotate-180" => Some(Transform::Rotate180),
        "rotate-270" => Some(Transform::Rotate270),
        "flipped" => Some(Transform::Flipped),
        "flipped-90" => Some(Transform::Flipped90),
        "flipped-180" => Some(Transform::Flipped180),
        "flipped-270" => Some(Transform::Flipped270),
        _ => None,
    }
}

pub fn parse_vrr_policy(value: &str) -> Option<VrrPolicyMode> {
    match value {
        "off" => Some(VrrPolicyMode::Off),
        "fullscreen" => Some(VrrPolicyMode::Fullscreen),
        "focused" => Some(VrrPolicyMode::Focused),
        "app-requested" => Some(VrrPolicyMode::AppRequested),
        "always-eligible" => Some(VrrPolicyMode::AlwaysEligible),
        _ => None,
    }
}

pub fn parse_scale(value: &str) -> Option<(u32, u32)> {
    let (numerator, denominator) = value.split_once('/')?;
    let numerator = numerator.parse().ok()?;
    let denominator = denominator.parse().ok()?;
    (numerator != 0 && denominator != 0 && gcd(numerator, denominator) == 1)
        .then_some((numerator, denominator))
}

pub fn parse_position(value: &str) -> Option<(i32, i32)> {
    let (x, y) = value.split_once(',')?;
    Some((x.parse().ok()?, y.parse().ok()?))
}

pub fn parse_mode(value: &str) -> Option<((u32, u32), Option<u32>)> {
    let (dimensions, refresh) = value
        .split_once('@')
        .map_or((value, None), |(dimensions, refresh)| {
            (dimensions, Some(refresh))
        });
    let (width, height) = dimensions.split_once('x')?;
    let width: u32 = width.parse().ok()?;
    let height: u32 = height.parse().ok()?;
    if width == 0 || height == 0 {
        return None;
    }
    let refresh = refresh.map(str::parse::<u32>).transpose().ok().flatten();
    if value.contains('@') && refresh.is_none() || refresh == Some(0) {
        return None;
    }
    Some(((width, height), refresh))
}

fn find_output(snapshot: &OutputSnapshot, selector: &str) -> Option<u64> {
    snapshot
        .descriptors
        .iter()
        .find_map(|(&id, descriptor)| (descriptor.name == selector).then_some(id))
        .or_else(|| parse_output_id(selector))
        .filter(|id| snapshot.outputs.contains_key(id))
}

fn parse_output_id(value: &str) -> Option<u64> {
    if let Some(value) = value.strip_prefix("0x") {
        u64::from_str_radix(value, 16).ok()
    } else if value.len() == 16 {
        u64::from_str_radix(value, 16).ok()
    } else {
        value.parse().ok()
    }
}

fn validate_layout(snapshot: &mut OutputSnapshot) -> Result<(), EditError> {
    let enabled: Vec<_> = snapshot
        .outputs
        .values()
        .filter(|output| output.enabled)
        .collect();
    if enabled.is_empty() {
        return Err(EditError(
            "refusing to disable the last enabled output".to_owned(),
        ));
    }
    if !snapshot
        .outputs
        .get(&snapshot.primary_output_id)
        .is_some_and(|output| output.enabled)
    {
        return Err(EditError(
            "the primary output must remain enabled".to_owned(),
        ));
    }
    if enabled.iter().any(|output| {
        output.logical_x < 0
            || output.logical_y < 0
            || !snapshot.descriptors.contains_key(&output.output_id)
    }) {
        return Err(EditError(
            "enabled output positions must be nonnegative".to_owned(),
        ));
    }
    for (index, left) in enabled.iter().enumerate() {
        for right in enabled.iter().skip(index + 1) {
            if overlaps(left, right) {
                return Err(EditError("enabled output rectangles overlap".to_owned()));
            }
        }
    }
    let root_width = enabled
        .iter()
        .map(|output| output.logical_x as u64 + u64::from(output.logical_width))
        .max()
        .unwrap_or(0);
    let root_height = enabled
        .iter()
        .map(|output| output.logical_y as u64 + u64::from(output.logical_height))
        .max()
        .unwrap_or(0);
    if root_width > MAXIMUM_ROOT_LOGICAL_WIDTH || root_height > MAXIMUM_ROOT_LOGICAL_HEIGHT {
        return Err(EditError(
            "edited output layout exceeds the X11 root bounds".to_owned(),
        ));
    }
    snapshot.root_width = root_width as u32;
    snapshot.root_height = root_height as u32;
    snapshot.enabled_output_count = enabled.len() as u32;
    Ok(())
}

fn overlaps(
    left: &&gw_wire::compositor::OutputUpsert,
    right: &&gw_wire::compositor::OutputUpsert,
) -> bool {
    i64::from(left.logical_x) + i64::from(left.logical_width) > i64::from(right.logical_x)
        && i64::from(right.logical_x) + i64::from(right.logical_width) > i64::from(left.logical_x)
        && i64::from(left.logical_y) + i64::from(left.logical_height) > i64::from(right.logical_y)
        && i64::from(right.logical_y) + i64::from(right.logical_height) > i64::from(left.logical_y)
}

fn logical_dimension(physical: u32, numerator: u32, denominator: u32) -> u32 {
    let product = u64::from(physical) * u64::from(denominator);
    (product / u64::from(numerator) + u64::from(product % u64::from(numerator) != 0)) as u32
}

fn scale_at_least(value: (u32, u32), numerator: u32, denominator: u32) -> bool {
    u64::from(value.0) * u64::from(denominator) >= u64::from(numerator) * u64::from(value.1)
}

fn scale_at_most(value: (u32, u32), numerator: u32, denominator: u32) -> bool {
    u64::from(value.0) * u64::from(denominator) <= u64::from(numerator) * u64::from(value.1)
}

fn swaps_axes(transform: Transform) -> bool {
    matches!(
        transform,
        Transform::Rotate90 | Transform::Rotate270 | Transform::Flipped90 | Transform::Flipped270
    )
}

fn gcd(mut left: u32, mut right: u32) -> u32 {
    while right != 0 {
        (left, right) = (right, left % right);
    }
    left
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parsers_match_the_legacy_cli_boundaries() {
        assert_eq!(parse_scale("5/4"), Some((5, 4)));
        assert_eq!(parse_scale("10/8"), None);
        assert_eq!(parse_position("-2,9"), Some((-2, 9)));
        assert_eq!(
            parse_mode("640x480@60000"),
            Some(((640, 480), Some(60_000)))
        );
        assert_eq!(parse_mode("640x0"), None);
        assert_eq!(parse_transform("flipped-90"), Some(Transform::Flipped90));
        assert_eq!(parse_vrr_policy("focused"), Some(VrrPolicyMode::Focused));
    }
}
