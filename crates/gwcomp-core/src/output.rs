use crate::Rectangle;

pub const MAXIMUM_OUTPUTS: usize = 8;
pub const MAXIMUM_TOTAL_OUTPUT_PIXELS: u64 = 67_108_864;
pub const MAXIMUM_SCALE_DENOMINATOR: u32 = 120;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RationalScale {
    pub numerator: u32,
    pub denominator: u32,
}

impl Default for RationalScale {
    fn default() -> Self {
        Self {
            numerator: 1,
            denominator: 1,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum OutputTransform {
    #[default]
    Normal = 0,
    Rotate90 = 1,
    Rotate180 = 2,
    Rotate270 = 3,
    Flipped = 4,
    Flipped90 = 5,
    Flipped180 = 6,
    Flipped270 = 7,
}

impl OutputTransform {
    #[must_use]
    pub const fn swaps_axes(self) -> bool {
        matches!(
            self,
            Self::Rotate90 | Self::Rotate270 | Self::Flipped90 | Self::Flipped270
        )
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PhysicalExtent {
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct LogicalExtent {
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct LogicalPoint {
    pub x: i32,
    pub y: i32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PhysicalPoint {
    pub x: u32,
    pub y: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PhysicalRectangle {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

impl PhysicalRectangle {
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.width == 0 || self.height == 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputMapping {
    pub logical_origin: LogicalPoint,
    pub logical_extent: LogicalExtent,
    pub physical_extent: PhysicalExtent,
    pub scale: RationalScale,
    pub transform: OutputTransform,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LogicalSamplePoint {
    pub x_numerator: i64,
    pub y_numerator: i64,
    pub denominator: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum DamageFilterFootprint {
    #[default]
    Point,
    Bilinear,
}

#[must_use]
pub const fn greatest_common_divisor(mut left: u32, mut right: u32) -> u32 {
    while right != 0 {
        let remainder = left % right;
        left = right;
        right = remainder;
    }
    left
}

#[must_use]
pub const fn is_reduced(scale: RationalScale) -> bool {
    scale.numerator != 0
        && scale.denominator != 0
        && greatest_common_divisor(scale.numerator, scale.denominator) == 1
}

#[must_use]
pub fn valid_output_scale(scale: RationalScale) -> bool {
    is_reduced(scale)
        && scale.denominator <= MAXIMUM_SCALE_DENOMINATOR
        && u64::from(scale.numerator) >= u64::from(scale.denominator)
        && u64::from(scale.numerator) <= u64::from(scale.denominator) * 4
}

#[must_use]
pub fn derive_logical_dimension(physical: u32, scale: RationalScale) -> Option<u32> {
    if !is_reduced(scale) {
        return None;
    }
    let product = u64::from(physical).checked_mul(u64::from(scale.denominator))?;
    let value =
        product / u64::from(scale.numerator) + u64::from(product % u64::from(scale.numerator) != 0);
    value.try_into().ok()
}

#[must_use]
pub fn transformed_physical_extent(
    native: PhysicalExtent,
    transform: OutputTransform,
) -> PhysicalExtent {
    if transform.swaps_axes() {
        PhysicalExtent {
            width: native.height,
            height: native.width,
        }
    } else {
        native
    }
}

fn contains_boundary(extent: PhysicalExtent, point: PhysicalPoint) -> bool {
    point.x <= extent.width && point.y <= extent.height
}

#[must_use]
pub fn transform_boundary(
    point: PhysicalPoint,
    native: PhysicalExtent,
    transform: OutputTransform,
) -> Option<PhysicalPoint> {
    if !contains_boundary(transformed_physical_extent(native, transform), point) {
        return None;
    }
    let (u, v, width, height) = (point.x, point.y, native.width, native.height);
    Some(match transform {
        OutputTransform::Normal => PhysicalPoint { x: u, y: v },
        OutputTransform::Rotate90 => PhysicalPoint { x: width - v, y: u },
        OutputTransform::Rotate180 => PhysicalPoint {
            x: width - u,
            y: height - v,
        },
        OutputTransform::Rotate270 => PhysicalPoint {
            x: v,
            y: height - u,
        },
        OutputTransform::Flipped => PhysicalPoint { x: width - u, y: v },
        OutputTransform::Flipped90 => PhysicalPoint {
            x: width - v,
            y: height - u,
        },
        OutputTransform::Flipped180 => PhysicalPoint {
            x: u,
            y: height - v,
        },
        OutputTransform::Flipped270 => PhysicalPoint { x: v, y: u },
    })
}

#[must_use]
pub fn inverse_transform_boundary(
    point: PhysicalPoint,
    native: PhysicalExtent,
    transform: OutputTransform,
) -> Option<PhysicalPoint> {
    if !contains_boundary(native, point) {
        return None;
    }
    let (x, y, width, height) = (point.x, point.y, native.width, native.height);
    Some(match transform {
        OutputTransform::Normal => PhysicalPoint { x, y },
        OutputTransform::Rotate90 => PhysicalPoint { x: y, y: width - x },
        OutputTransform::Rotate180 => PhysicalPoint {
            x: width - x,
            y: height - y,
        },
        OutputTransform::Rotate270 => PhysicalPoint {
            x: height - y,
            y: x,
        },
        OutputTransform::Flipped => PhysicalPoint { x: width - x, y },
        OutputTransform::Flipped90 => PhysicalPoint {
            x: height - y,
            y: width - x,
        },
        OutputTransform::Flipped180 => PhysicalPoint { x, y: height - y },
        OutputTransform::Flipped270 => PhysicalPoint { x: y, y: x },
    })
}

fn map_rectangle_corners(
    rectangle: PhysicalRectangle,
    mapper: impl Fn(PhysicalPoint) -> Option<PhysicalPoint>,
) -> Option<PhysicalRectangle> {
    let right = rectangle.x.checked_add(rectangle.width)?;
    let bottom = rectangle.y.checked_add(rectangle.height)?;
    let corners = [
        PhysicalPoint {
            x: rectangle.x,
            y: rectangle.y,
        },
        PhysicalPoint {
            x: right,
            y: rectangle.y,
        },
        PhysicalPoint {
            x: rectangle.x,
            y: bottom,
        },
        PhysicalPoint {
            x: right,
            y: bottom,
        },
    ];
    let mapped = corners.map(mapper);
    let mapped: [PhysicalPoint; 4] = mapped
        .into_iter()
        .collect::<Option<Vec<_>>>()?
        .try_into()
        .ok()?;
    let minimum_x = mapped.iter().map(|point| point.x).min()?;
    let maximum_x = mapped.iter().map(|point| point.x).max()?;
    let minimum_y = mapped.iter().map(|point| point.y).min()?;
    let maximum_y = mapped.iter().map(|point| point.y).max()?;
    Some(PhysicalRectangle {
        x: minimum_x,
        y: minimum_y,
        width: maximum_x - minimum_x,
        height: maximum_y - minimum_y,
    })
}

#[must_use]
pub fn transform_rectangle(
    rectangle: PhysicalRectangle,
    native: PhysicalExtent,
    transform: OutputTransform,
) -> Option<PhysicalRectangle> {
    let transformed = transformed_physical_extent(native, transform);
    if u64::from(rectangle.x) + u64::from(rectangle.width) > u64::from(transformed.width)
        || u64::from(rectangle.y) + u64::from(rectangle.height) > u64::from(transformed.height)
    {
        return None;
    }
    map_rectangle_corners(rectangle, |point| {
        transform_boundary(point, native, transform)
    })
}

#[must_use]
pub fn inverse_transform_rectangle(
    rectangle: PhysicalRectangle,
    native: PhysicalExtent,
    transform: OutputTransform,
) -> Option<PhysicalRectangle> {
    if u64::from(rectangle.x) + u64::from(rectangle.width) > u64::from(native.width)
        || u64::from(rectangle.y) + u64::from(rectangle.height) > u64::from(native.height)
    {
        return None;
    }
    map_rectangle_corners(rectangle, |point| {
        inverse_transform_boundary(point, native, transform)
    })
}

fn logical_bounds(mapping: OutputMapping) -> Option<(i64, i64, i64, i64)> {
    let left = i64::from(mapping.logical_origin.x);
    let top = i64::from(mapping.logical_origin.y);
    let right = left.checked_add(i64::from(mapping.logical_extent.width))?;
    let bottom = top.checked_add(i64::from(mapping.logical_extent.height))?;
    (left >= 0 && top >= 0 && right <= i64::from(i32::MAX) && bottom <= i64::from(i32::MAX))
        .then_some((left, top, right, bottom))
}

#[must_use]
pub fn valid_output_mapping(mapping: OutputMapping) -> bool {
    if mapping.logical_extent.width == 0
        || mapping.logical_extent.height == 0
        || mapping.physical_extent.width == 0
        || mapping.physical_extent.height == 0
        || !valid_output_scale(mapping.scale)
        || logical_bounds(mapping).is_none()
    {
        return false;
    }
    let transformed = transformed_physical_extent(mapping.physical_extent, mapping.transform);
    derive_logical_dimension(transformed.width, mapping.scale) == Some(mapping.logical_extent.width)
        && derive_logical_dimension(transformed.height, mapping.scale)
            == Some(mapping.logical_extent.height)
}

fn scaled_floor(value: u64, scale: RationalScale) -> Option<u32> {
    value
        .checked_mul(u64::from(scale.numerator))?
        .checked_div(u64::from(scale.denominator))?
        .try_into()
        .ok()
}

fn scaled_ceil(value: u64, scale: RationalScale) -> Option<u32> {
    let product = value.checked_mul(u64::from(scale.numerator))?;
    let denominator = u64::from(scale.denominator);
    (product / denominator + u64::from(product % denominator != 0))
        .try_into()
        .ok()
}

#[must_use]
pub fn map_logical_point_to_native(
    mapping: OutputMapping,
    point: LogicalPoint,
) -> Option<PhysicalPoint> {
    if !valid_output_mapping(mapping) {
        return None;
    }
    let (left, top, right, bottom) = logical_bounds(mapping)?;
    if i64::from(point.x) < left
        || i64::from(point.x) > right
        || i64::from(point.y) < top
        || i64::from(point.y) > bottom
    {
        return None;
    }
    let transformed = transformed_physical_extent(mapping.physical_extent, mapping.transform);
    let x = scaled_floor((i64::from(point.x) - left).try_into().ok()?, mapping.scale)?
        .min(transformed.width);
    let y = scaled_floor((i64::from(point.y) - top).try_into().ok()?, mapping.scale)?
        .min(transformed.height);
    transform_boundary(
        PhysicalPoint { x, y },
        mapping.physical_extent,
        mapping.transform,
    )
}

#[must_use]
pub fn map_logical_rectangle_to_native(
    mapping: OutputMapping,
    rectangle: Rectangle,
) -> Option<PhysicalRectangle> {
    if !valid_output_mapping(mapping) || rectangle.is_empty() {
        return None;
    }
    let (output_left, output_top, output_right, output_bottom) = logical_bounds(mapping)?;
    let right = i64::from(rectangle.x).checked_add(i64::from(rectangle.width))?;
    let bottom = i64::from(rectangle.y).checked_add(i64::from(rectangle.height))?;
    let left = i64::from(rectangle.x).max(output_left);
    let top = i64::from(rectangle.y).max(output_top);
    let right = right.min(output_right);
    let bottom = bottom.min(output_bottom);
    if left >= right || top >= bottom {
        return None;
    }
    let transformed = transformed_physical_extent(mapping.physical_extent, mapping.transform);
    let x0 =
        scaled_floor((left - output_left).try_into().ok()?, mapping.scale)?.min(transformed.width);
    let y0 =
        scaled_floor((top - output_top).try_into().ok()?, mapping.scale)?.min(transformed.height);
    let x1 =
        scaled_ceil((right - output_left).try_into().ok()?, mapping.scale)?.min(transformed.width);
    let y1 =
        scaled_ceil((bottom - output_top).try_into().ok()?, mapping.scale)?.min(transformed.height);
    if x0 >= x1 || y0 >= y1 {
        return None;
    }
    transform_rectangle(
        PhysicalRectangle {
            x: x0,
            y: y0,
            width: x1 - x0,
            height: y1 - y0,
        },
        mapping.physical_extent,
        mapping.transform,
    )
}

#[must_use]
pub fn map_native_pixel_center_to_logical(
    mapping: OutputMapping,
    pixel: PhysicalPoint,
) -> Option<LogicalSamplePoint> {
    if !valid_output_mapping(mapping)
        || pixel.x >= mapping.physical_extent.width
        || pixel.y >= mapping.physical_extent.height
    {
        return None;
    }
    let transformed = inverse_transform_rectangle(
        PhysicalRectangle {
            x: pixel.x,
            y: pixel.y,
            width: 1,
            height: 1,
        },
        mapping.physical_extent,
        mapping.transform,
    )?;
    let denominator = mapping.scale.numerator.checked_mul(2)?;
    let x = u64::from(transformed.x).checked_mul(2)?.checked_add(1)?;
    let y = u64::from(transformed.y).checked_mul(2)?.checked_add(1)?;
    let local_x = x.checked_mul(u64::from(mapping.scale.denominator))?;
    let local_y = y.checked_mul(u64::from(mapping.scale.denominator))?;
    Some(LogicalSamplePoint {
        x_numerator: i64::from(mapping.logical_origin.x)
            .checked_mul(i64::from(denominator))?
            .checked_add(local_x.try_into().ok()?)?,
        y_numerator: i64::from(mapping.logical_origin.y)
            .checked_mul(i64::from(denominator))?
            .checked_add(local_y.try_into().ok()?)?,
        denominator,
    })
}

#[must_use]
pub fn map_logical_damage_to_native(
    mapping: OutputMapping,
    damage: Rectangle,
    footprint: DamageFilterFootprint,
) -> Option<PhysicalRectangle> {
    let rectangle = map_logical_rectangle_to_native(mapping, damage)?;
    if footprint == DamageFilterFootprint::Point {
        return Some(rectangle);
    }
    let left = rectangle.x.saturating_sub(1);
    let top = rectangle.y.saturating_sub(1);
    let right = (u64::from(rectangle.x) + u64::from(rectangle.width) + 1)
        .min(u64::from(mapping.physical_extent.width));
    let bottom = (u64::from(rectangle.y) + u64::from(rectangle.height) + 1)
        .min(u64::from(mapping.physical_extent.height));
    Some(PhysicalRectangle {
        x: left,
        y: top,
        width: u32::try_from(right - u64::from(left)).ok()?,
        height: u32::try_from(bottom - u64::from(top)).ok()?,
    })
}
