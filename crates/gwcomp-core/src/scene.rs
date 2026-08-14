use std::collections::BTreeMap;

use crate::{OutputMapping, OutputTransform, PixelFormat, RationalScale, Rectangle};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum SurfacePresentation {
    #[default]
    Ordinary,
    MetadataOnly,
    Cursor,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceBuffer {
    pub width: u32,
    pub height: u32,
    pub stride_pixels: u32,
    pub format: PixelFormat,
    pub pixels: Vec<u32>,
}

impl SurfaceBuffer {
    #[must_use]
    pub fn is_valid(&self) -> bool {
        if self.width == 0 || self.height == 0 || self.stride_pixels < self.width {
            return false;
        }
        let required =
            u64::from(self.height - 1) * u64::from(self.stride_pixels) + u64::from(self.width);
        required <= self.pixels.len() as u64
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SceneOutput {
    pub output_id: u64,
    pub enabled: bool,
    pub logical: Rectangle,
    pub physical_width: u32,
    pub physical_height: u32,
    pub refresh_millihertz: u32,
    pub scale: RationalScale,
    pub transform: OutputTransform,
}

impl SceneOutput {
    #[must_use]
    pub fn mapping(&self) -> Option<OutputMapping> {
        if !self.enabled {
            return None;
        }
        let mapping = OutputMapping {
            logical_origin: crate::LogicalPoint {
                x: self.logical.x,
                y: self.logical.y,
            },
            logical_extent: crate::LogicalExtent {
                width: self.logical.width,
                height: self.logical.height,
            },
            physical_extent: crate::PhysicalExtent {
                width: self.physical_width,
                height: self.physical_height,
            },
            scale: self.scale,
            transform: self.transform,
        };
        crate::valid_output_mapping(mapping).then_some(mapping)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SceneSurface {
    pub surface_id: u64,
    pub output_id: u64,
    pub logical: Rectangle,
    pub stacking: i32,
    pub visible: bool,
    pub clip: Option<Rectangle>,
    pub opacity: u32,
    pub client_buffer_scale: u32,
    pub presentation: SurfacePresentation,
    pub buffer: SurfaceBuffer,
}

impl SceneSurface {
    #[must_use]
    pub fn effective_logical_bounds(&self) -> Option<Rectangle> {
        if !self.visible
            || self.opacity == 0
            || self.presentation == SurfacePresentation::MetadataOnly
        {
            return None;
        }
        let mut local = Rectangle::new(0, 0, self.logical.width, self.logical.height);
        if let Some(clip) = self.clip {
            local = local.intersection(clip)?;
        }
        local.translate(self.logical.x, self.logical.y)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceOutputMembership {
    pub primary_output_id: u64,
    pub output_ids: Vec<u64>,
    pub preferred_scale: RationalScale,
    pub client_buffer_scale: u32,
    pub layout_generation: u64,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Scene {
    pub outputs: BTreeMap<u64, SceneOutput>,
    pub surfaces: BTreeMap<u64, SceneSurface>,
    pub surface_outputs: BTreeMap<u64, SurfaceOutputMembership>,
    pub primary_output_id: u64,
    pub configuration_generation: u64,
}

impl Scene {
    #[must_use]
    pub fn stacking_order(&self) -> Vec<u64> {
        let mut surfaces: Vec<_> = self.surfaces.values().collect();
        surfaces.sort_by_key(|surface| {
            (
                surface.presentation == SurfacePresentation::Cursor,
                surface.stacking,
                surface.surface_id,
            )
        });
        surfaces
            .into_iter()
            .map(|surface| surface.surface_id)
            .collect()
    }

    pub fn validate(&self) -> Result<(), SceneError> {
        if self.configuration_generation == 0 || self.primary_output_id == 0 {
            return Err(SceneError::IncompleteMetadata);
        }
        let Some(primary) = self.outputs.get(&self.primary_output_id) else {
            return Err(SceneError::IncompleteMetadata);
        };
        if !primary.enabled {
            return Err(SceneError::IncompleteMetadata);
        }
        if self.outputs.is_empty() || self.outputs.len() > crate::MAXIMUM_OUTPUTS {
            return Err(SceneError::OutputLimit);
        }
        let mut total_pixels = 0_u64;
        for (&id, output) in &self.outputs {
            if id == 0 || id != output.output_id || (output.enabled && output.mapping().is_none()) {
                return Err(SceneError::InvalidOutput);
            }
            if output.enabled {
                let pixels = u64::from(output.physical_width) * u64::from(output.physical_height);
                if pixels > crate::MAXIMUM_TOTAL_OUTPUT_PIXELS.saturating_sub(total_pixels) {
                    return Err(SceneError::PhysicalLimitExceeded);
                }
                total_pixels += pixels;
            }
        }
        let enabled: Vec<_> = self
            .outputs
            .values()
            .filter(|output| output.enabled)
            .collect();
        for (index, left) in enabled.iter().enumerate() {
            if enabled[index + 1..]
                .iter()
                .any(|right| left.logical.intersection(right.logical).is_some())
            {
                return Err(SceneError::OverlappingOutputs);
            }
        }
        for (&id, surface) in &self.surfaces {
            if id == 0
                || id != surface.surface_id
                || surface.logical.width == 0
                || surface.logical.height == 0
                || surface.client_buffer_scale == 0
                || !surface.buffer.is_valid()
                || u64::from(surface.logical.width) * u64::from(surface.client_buffer_scale)
                    != u64::from(surface.buffer.width)
                || u64::from(surface.logical.height) * u64::from(surface.client_buffer_scale)
                    != u64::from(surface.buffer.height)
            {
                return Err(SceneError::InvalidSurface);
            }
            let Some(assigned_output) = self
                .outputs
                .get(&surface.output_id)
                .filter(|output| output.enabled)
            else {
                return Err(SceneError::InvalidMembership);
            };
            let membership = self.surface_outputs.get(&id);
            if surface.presentation == SurfacePresentation::MetadataOnly {
                if membership.is_some() {
                    return Err(SceneError::InvalidMembership);
                }
                continue;
            }
            let Some(membership) = self.surface_outputs.get(&id) else {
                return Err(SceneError::InvalidMembership);
            };
            let expected_memberships = self.geometric_memberships(surface);
            if membership.primary_output_id != surface.output_id
                || membership.layout_generation != self.configuration_generation
                || membership.preferred_scale != assigned_output.scale
                || membership.client_buffer_scale != surface.client_buffer_scale
                || membership.output_ids != expected_memberships
                || (!membership.output_ids.is_empty()
                    && !membership
                        .output_ids
                        .contains(&membership.primary_output_id))
            {
                return Err(SceneError::InvalidMembership);
            }
        }
        if self
            .surface_outputs
            .keys()
            .any(|surface_id| !self.surfaces.contains_key(surface_id))
        {
            return Err(SceneError::UnknownSurface);
        }
        Ok(())
    }

    fn geometric_memberships(&self, surface: &SceneSurface) -> Vec<u64> {
        if !surface.visible {
            return Vec::new();
        }
        let mut memberships: Vec<_> = self
            .outputs
            .values()
            .filter(|output| {
                output.enabled && surface.logical.intersection(output.logical).is_some()
            })
            .map(|output| output.output_id)
            .collect();
        memberships.sort_by_key(|output_id| {
            let output = &self.outputs[output_id];
            (output.logical.y, output.logical.x, *output_id)
        });
        memberships
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SceneError {
    IncompleteMetadata,
    OutputLimit,
    InvalidOutput,
    InvalidSurface,
    InvalidMembership,
    UnknownSurface,
    OverlappingOutputs,
    PhysicalLimitExceeded,
}
