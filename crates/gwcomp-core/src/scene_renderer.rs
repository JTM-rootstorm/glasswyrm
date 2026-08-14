use std::collections::BTreeMap;

use crate::{
    LogicalSamplePoint, OutputFrameResult, OutputMapping, OutputSpec, Pixel, PixelFormat,
    RationalScale, Rectangle, Scene, SoftwareFrame, SoftwareFrameSet, SurfaceBuffer,
    SurfacePresentation, blend, map_logical_rectangle_to_native,
    map_native_pixel_center_to_logical, pack_xrgb8888, unpack_argb8888, unpack_xrgb8888,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SamplingFilter {
    Direct,
    Nearest,
    Bilinear,
}

#[must_use]
pub fn select_sampling_filter(
    output_scale: RationalScale,
    client_buffer_scale: u32,
) -> SamplingFilter {
    if client_buffer_scale == 0 || output_scale.numerator == 0 || output_scale.denominator == 0 {
        return SamplingFilter::Bilinear;
    }
    let client_denominator = u64::from(output_scale.denominator) * u64::from(client_buffer_scale);
    if u64::from(output_scale.numerator) == client_denominator {
        SamplingFilter::Direct
    } else if u64::from(output_scale.numerator) > client_denominator
        && u64::from(output_scale.numerator) % client_denominator == 0
    {
        SamplingFilter::Nearest
    } else {
        SamplingFilter::Bilinear
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct OutputSoftwareRenderMetrics {
    pub damage_rectangles: u64,
    pub rendered_pixels: u64,
    pub sampled_pixels: u64,
    pub used_direct: bool,
    pub used_nearest: bool,
    pub used_bilinear: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SoftwareRenderResult {
    pub frames: SoftwareFrameSet,
    pub metrics: BTreeMap<u64, OutputSoftwareRenderMetrics>,
}

pub struct SoftwareRenderRequest<'a> {
    pub scene: &'a Scene,
    pub damage: &'a BTreeMap<u64, Vec<Rectangle>>,
    pub previous: Option<&'a SoftwareFrameSet>,
    pub commit_id: u64,
    pub generation: u64,
    pub ordinal: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SoftwareRenderError {
    InvalidScene,
    InvalidPreviousFrameSet,
    UnknownDamageOutput,
    InvalidDamage,
    InvalidBuffer,
    InvalidFrameSet,
}

#[derive(Clone, Copy)]
struct FractionFloor {
    whole: i64,
    remainder: u64,
}

fn floor_fraction(numerator: i64, denominator: u64) -> FractionFloor {
    let denominator = i64::try_from(denominator).unwrap_or(i64::MAX);
    let mut whole = numerator / denominator;
    let mut remainder = numerator % denominator;
    if remainder < 0 {
        whole -= 1;
        remainder += denominator;
    }
    FractionFloor {
        whole,
        remainder: remainder as u64,
    }
}

fn inside(point: LogicalSamplePoint, rectangle: Rectangle) -> bool {
    let denominator = i64::from(point.denominator);
    let left = i64::from(rectangle.x) * denominator;
    let top = i64::from(rectangle.y) * denominator;
    let right = (i64::from(rectangle.x) + i64::from(rectangle.width)) * denominator;
    let bottom = (i64::from(rectangle.y) + i64::from(rectangle.height)) * denominator;
    point.x_numerator >= left
        && point.x_numerator < right
        && point.y_numerator >= top
        && point.y_numerator < bottom
}

fn source_pixel(buffer: &SurfaceBuffer, x: i64, y: i64) -> Pixel {
    let x = x.clamp(0, i64::from(buffer.width - 1)) as usize;
    let y = y.clamp(0, i64::from(buffer.height - 1)) as usize;
    let word = buffer.pixels[y * buffer.stride_pixels as usize + x];
    match buffer.format {
        PixelFormat::Xrgb8888 => unpack_xrgb8888(word),
        PixelFormat::Argb8888Premultiplied => unpack_argb8888(word),
    }
}

fn interpolate_channel(channels: [u8; 4], x_weight: u64, y_weight: u64, denominator: u64) -> u8 {
    let inverse_x = denominator - x_weight;
    let inverse_y = denominator - y_weight;
    let divisor = denominator * denominator;
    let weighted = u64::from(channels[0]) * inverse_x * inverse_y
        + u64::from(channels[1]) * x_weight * inverse_y
        + u64::from(channels[2]) * inverse_x * y_weight
        + u64::from(channels[3]) * x_weight * y_weight;
    ((weighted + divisor / 2) / divisor) as u8
}

fn sample(
    buffer: &SurfaceBuffer,
    surface_origin: Rectangle,
    client_buffer_scale: u32,
    point: LogicalSamplePoint,
    filter: SamplingFilter,
) -> Pixel {
    let denominator = i64::from(point.denominator);
    let local_x = point.x_numerator - i64::from(surface_origin.x) * denominator;
    let local_y = point.y_numerator - i64::from(surface_origin.y) * denominator;
    let client_scale = i64::from(client_buffer_scale);
    if filter != SamplingFilter::Bilinear {
        return source_pixel(
            buffer,
            local_x * client_scale / denominator,
            local_y * client_scale / denominator,
        );
    }
    let fixed_denominator = u64::from(point.denominator) * 2;
    let x = floor_fraction(local_x * client_scale * 2 - denominator, fixed_denominator);
    let y = floor_fraction(local_y * client_scale * 2 - denominator, fixed_denominator);
    let pixels = [
        source_pixel(buffer, x.whole, y.whole),
        source_pixel(buffer, x.whole + 1, y.whole),
        source_pixel(buffer, x.whole, y.whole + 1),
        source_pixel(buffer, x.whole + 1, y.whole + 1),
    ];
    Pixel {
        red: interpolate_channel(
            [pixels[0].red, pixels[1].red, pixels[2].red, pixels[3].red],
            x.remainder,
            y.remainder,
            fixed_denominator,
        ),
        green: interpolate_channel(
            [
                pixels[0].green,
                pixels[1].green,
                pixels[2].green,
                pixels[3].green,
            ],
            x.remainder,
            y.remainder,
            fixed_denominator,
        ),
        blue: interpolate_channel(
            [
                pixels[0].blue,
                pixels[1].blue,
                pixels[2].blue,
                pixels[3].blue,
            ],
            x.remainder,
            y.remainder,
            fixed_denominator,
        ),
        alpha: if buffer.format == PixelFormat::Xrgb8888 {
            255
        } else {
            interpolate_channel(
                [
                    pixels[0].alpha,
                    pixels[1].alpha,
                    pixels[2].alpha,
                    pixels[3].alpha,
                ],
                x.remainder,
                y.remainder,
                fixed_denominator,
            )
        },
    }
}

fn validate_buffer(buffer: &SurfaceBuffer) -> bool {
    buffer.is_valid()
        && (buffer.format != PixelFormat::Argb8888Premultiplied
            || buffer
                .pixels
                .iter()
                .all(|word| unpack_argb8888(*word).is_premultiplied()))
}

fn compatible_previous<'a>(
    previous: Option<&'a SoftwareFrameSet>,
    output: &crate::SceneOutput,
) -> Option<&'a OutputFrameResult> {
    let previous = previous.filter(|value| value.is_finalized())?;
    let candidate = previous.outputs().get(&output.output_id)?;
    (candidate.output.width == output.physical_width
        && candidate.output.height == output.physical_height
        && candidate.scale == output.scale
        && candidate.transform == output.transform)
        .then_some(candidate)
}

fn record_filter(metrics: &mut OutputSoftwareRenderMetrics, filter: SamplingFilter) {
    metrics.used_direct |= filter == SamplingFilter::Direct;
    metrics.used_nearest |= filter == SamplingFilter::Nearest;
    metrics.used_bilinear |= filter == SamplingFilter::Bilinear;
}

fn render_surface(
    frame: &mut OutputFrameResult,
    mapping: OutputMapping,
    damage: Rectangle,
    surface: &crate::SceneSurface,
    member: bool,
    metrics: &mut OutputSoftwareRenderMetrics,
) -> Result<(), SoftwareRenderError> {
    if !member
        || !surface.visible
        || surface.opacity == 0
        || surface.presentation == SurfacePresentation::MetadataOnly
    {
        return Ok(());
    }
    let Some(logical_bounds) = surface.effective_logical_bounds() else {
        return Ok(());
    };
    let Some(physical_bounds) = map_logical_rectangle_to_native(mapping, logical_bounds) else {
        return Ok(());
    };
    let physical_bounds = Rectangle::new(
        i32::try_from(physical_bounds.x).map_err(|_| SoftwareRenderError::InvalidScene)?,
        i32::try_from(physical_bounds.y).map_err(|_| SoftwareRenderError::InvalidScene)?,
        physical_bounds.width,
        physical_bounds.height,
    );
    let Some(painted) = damage.intersection(physical_bounds) else {
        return Ok(());
    };
    if !validate_buffer(&surface.buffer) {
        return Err(SoftwareRenderError::InvalidBuffer);
    }
    let filter = select_sampling_filter(mapping.scale, surface.client_buffer_scale);
    record_filter(metrics, filter);
    for y in 0..painted.height {
        for x in 0..painted.width {
            let native_x = u32::try_from(painted.x).unwrap_or(0) + x;
            let native_y = u32::try_from(painted.y).unwrap_or(0) + y;
            let Some(logical) = map_native_pixel_center_to_logical(
                mapping,
                crate::PhysicalPoint {
                    x: native_x,
                    y: native_y,
                },
            ) else {
                continue;
            };
            if !inside(logical, logical_bounds) {
                continue;
            }
            let source = sample(
                &surface.buffer,
                surface.logical,
                surface.client_buffer_scale,
                logical,
                filter,
            );
            let index = native_y as usize * frame.output.width as usize + native_x as usize;
            let destination = unpack_xrgb8888(frame.frame.pixels()[index]);
            frame.frame.pixels_mut()[index] =
                pack_xrgb8888(blend(source, destination, surface.opacity));
            metrics.sampled_pixels += 1;
        }
    }
    Ok(())
}

pub fn render_software_scene(
    request: SoftwareRenderRequest<'_>,
) -> Result<SoftwareRenderResult, SoftwareRenderError> {
    request
        .scene
        .validate()
        .map_err(|_| SoftwareRenderError::InvalidScene)?;
    if request.commit_id == 0 || request.generation == 0 || request.ordinal == 0 {
        return Err(SoftwareRenderError::InvalidScene);
    }
    if request
        .previous
        .is_some_and(|previous| !previous.is_finalized())
    {
        return Err(SoftwareRenderError::InvalidPreviousFrameSet);
    }
    for output_id in request.damage.keys() {
        if !request
            .scene
            .outputs
            .get(output_id)
            .is_some_and(|output| output.enabled)
        {
            return Err(SoftwareRenderError::UnknownDamageOutput);
        }
    }

    let stacking = request.scene.stacking_order();
    let mut frames = SoftwareFrameSet::with_previous(request.previous);
    let mut metrics: BTreeMap<u64, OutputSoftwareRenderMetrics> = BTreeMap::new();
    for output in request
        .scene
        .outputs
        .values()
        .filter(|output| output.enabled)
    {
        let mapping = output.mapping().ok_or(SoftwareRenderError::InvalidScene)?;
        let previous = compatible_previous(request.previous, output);
        let mut frame = if let Some(previous) = previous {
            previous.frame.clone()
        } else {
            let mut frame = SoftwareFrame::default();
            frame
                .configure(
                    output.output_id,
                    output.physical_width,
                    output.physical_height,
                )
                .map_err(|_| SoftwareRenderError::InvalidFrameSet)?;
            frame
        };
        let damage = if previous.is_none() {
            vec![Rectangle::new(
                0,
                0,
                output.physical_width,
                output.physical_height,
            )]
        } else {
            request
                .damage
                .get(&output.output_id)
                .cloned()
                .unwrap_or_default()
        };
        let mut result = OutputFrameResult {
            output: OutputSpec {
                output_id: output.output_id,
                width: output.physical_width,
                height: output.physical_height,
                refresh_millihertz: output.refresh_millihertz,
            },
            logical: output.logical,
            scale: output.scale,
            transform: output.transform,
            frame: std::mem::take(&mut frame),
            damage,
            visible_hash: 0,
            frame_hash_bytes: 0,
            frame_hash_reused: false,
        };
        let output_metrics = metrics.entry(output.output_id).or_default();
        output_metrics.damage_rectangles = result.damage.len() as u64;
        for damage in result.damage.clone() {
            if damage.x < 0
                || damage.y < 0
                || damage.is_empty()
                || u64::try_from(damage.x).unwrap_or(u64::MAX) + u64::from(damage.width)
                    > u64::from(output.physical_width)
                || u64::try_from(damage.y).unwrap_or(u64::MAX) + u64::from(damage.height)
                    > u64::from(output.physical_height)
            {
                return Err(SoftwareRenderError::InvalidDamage);
            }
            output_metrics.rendered_pixels = output_metrics
                .rendered_pixels
                .saturating_add(u64::from(damage.width) * u64::from(damage.height));
            for y in 0..damage.height {
                let start = (u32::try_from(damage.y).unwrap_or(0) + y) as usize
                    * output.physical_width as usize
                    + u32::try_from(damage.x).unwrap_or(0) as usize;
                result.frame.pixels_mut()[start..start + damage.width as usize]
                    .fill(SoftwareFrame::CLEAR_PIXEL);
            }
            for surface_id in &stacking {
                let surface = request
                    .scene
                    .surfaces
                    .get(surface_id)
                    .ok_or(SoftwareRenderError::InvalidScene)?;
                let member = request
                    .scene
                    .surface_outputs
                    .get(surface_id)
                    .is_some_and(|membership| membership.output_ids.contains(&output.output_id));
                render_surface(
                    &mut result,
                    mapping,
                    damage,
                    surface,
                    member,
                    output_metrics,
                )?;
            }
        }
        frames
            .append(result)
            .map_err(|_| SoftwareRenderError::InvalidFrameSet)?;
    }
    frames
        .finalize(
            request.scene.configuration_generation,
            request.scene.primary_output_id,
            request.commit_id,
            request.generation,
            request.ordinal,
        )
        .map_err(|_| SoftwareRenderError::InvalidFrameSet)?;
    Ok(SoftwareRenderResult { frames, metrics })
}
