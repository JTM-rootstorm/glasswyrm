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
        for (&id, output) in &self.outputs {
            if id == 0 || id != output.output_id || (output.enabled && output.mapping().is_none()) {
                return Err(SceneError::InvalidOutput);
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
            let Some(membership) = self.surface_outputs.get(&id) else {
                return Err(SceneError::InvalidMembership);
            };
            if membership.layout_generation != self.configuration_generation
                || membership.client_buffer_scale != surface.client_buffer_scale
                || !membership
                    .output_ids
                    .contains(&membership.primary_output_id)
                || membership.output_ids.iter().any(|output_id| {
                    !self
                        .outputs
                        .get(output_id)
                        .is_some_and(|output| output.enabled)
                })
            {
                return Err(SceneError::InvalidMembership);
            }
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SceneError {
    IncompleteMetadata,
    OutputLimit,
    InvalidOutput,
    InvalidSurface,
    InvalidMembership,
}
