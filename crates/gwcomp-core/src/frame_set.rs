use std::collections::BTreeMap;

use crate::{
    OutputSpec, OutputTransform, RationalScale, Rectangle, SoftwareFrame,
    hash_visible_xrgb8888_measured, valid_output_scale,
};

const FNV_OFFSET: u64 = 14_695_981_039_346_656_037;
const FNV_PRIME: u64 = 1_099_511_628_211;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutputFrameResult {
    pub output: OutputSpec,
    pub logical: Rectangle,
    pub scale: RationalScale,
    pub transform: OutputTransform,
    pub frame: SoftwareFrame,
    pub damage: Vec<Rectangle>,
    pub visible_hash: u64,
    pub frame_hash_bytes: u64,
    pub frame_hash_reused: bool,
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SoftwareFrameSet {
    outputs: BTreeMap<u64, OutputFrameResult>,
    hash_history: BTreeMap<u64, Vec<(Vec<u32>, u64)>>,
    total_pixels: u64,
    aggregate_hash: u64,
    layout_generation: u64,
    primary_output_id: u64,
    commit_id: u64,
    generation: u64,
    ordinal: u64,
    finalized: bool,
}

impl SoftwareFrameSet {
    #[must_use]
    pub fn with_previous(previous: Option<&Self>) -> Self {
        Self {
            hash_history: previous
                .filter(|value| value.finalized)
                .map_or_else(BTreeMap::new, |value| value.hash_history.clone()),
            ..Self::default()
        }
    }

    pub fn append(&mut self, mut output: OutputFrameResult) -> Result<(), FrameSetError> {
        if self.finalized {
            return Err(FrameSetError::AlreadyFinalized);
        }
        if self.outputs.len() == crate::MAXIMUM_OUTPUTS {
            return Err(FrameSetError::OutputLimit);
        }
        if output.output.output_id == 0
            || !output.frame.is_enabled()
            || output.frame.id() != output.output.output_id
            || output.frame.width() != output.output.width
            || output.frame.height() != output.output.height
            || output.logical.x < 0
            || output.logical.y < 0
            || output.logical.is_empty()
            || !valid_output_scale(output.scale)
        {
            return Err(FrameSetError::InconsistentMetadata);
        }
        for rectangle in &output.damage {
            if rectangle.x < 0
                || rectangle.y < 0
                || rectangle.is_empty()
                || u64::try_from(rectangle.x).unwrap_or(u64::MAX) + u64::from(rectangle.width)
                    > u64::from(output.output.width)
                || u64::try_from(rectangle.y).unwrap_or(u64::MAX) + u64::from(rectangle.height)
                    > u64::from(output.output.height)
            {
                return Err(FrameSetError::InvalidDamage);
            }
        }
        let pixels = u64::from(output.output.width) * u64::from(output.output.height);
        if pixels > crate::MAXIMUM_TOTAL_OUTPUT_PIXELS.saturating_sub(self.total_pixels) {
            return Err(FrameSetError::PixelLimit);
        }
        let id = output.output.output_id;
        if self.outputs.contains_key(&id) {
            return Err(FrameSetError::DuplicateOutput);
        }

        let mut reused = None;
        if let Some(entries) = self.hash_history.get_mut(&id)
            && let Some(index) = entries
                .iter()
                .position(|(candidate, _)| candidate == output.frame.pixels())
        {
            let entry = entries.remove(index);
            reused = Some(entry.1);
            entries.insert(0, entry);
        }
        let hash = if let Some(hash) = reused {
            output.frame_hash_reused = true;
            hash
        } else {
            let measurement = hash_visible_xrgb8888_measured(output.frame.pixels());
            let entries = self.hash_history.entry(id).or_default();
            entries.insert(0, (output.frame.pixels().to_vec(), measurement.hash));
            entries.truncate(2);
            measurement.hash
        };
        output.visible_hash = hash;
        output.frame_hash_bytes = pixels.saturating_mul(3);
        self.outputs.insert(id, output);
        self.total_pixels += pixels;
        Ok(())
    }

    pub fn finalize(
        &mut self,
        layout_generation: u64,
        primary_output_id: u64,
        commit_id: u64,
        generation: u64,
        ordinal: u64,
    ) -> Result<(), FrameSetError> {
        if self.finalized {
            return Err(FrameSetError::AlreadyFinalized);
        }
        if self.outputs.is_empty()
            || layout_generation == 0
            || primary_output_id == 0
            || commit_id == 0
            || generation == 0
            || ordinal == 0
            || !self.outputs.contains_key(&primary_output_id)
        {
            return Err(FrameSetError::IncompleteCommit);
        }
        self.layout_generation = layout_generation;
        self.primary_output_id = primary_output_id;
        self.commit_id = commit_id;
        self.generation = generation;
        self.ordinal = ordinal;
        self.aggregate_hash =
            calculate_frame_set_aggregate_hash(&self.outputs, layout_generation, primary_output_id);
        self.hash_history
            .retain(|output_id, _| self.outputs.contains_key(output_id));
        self.finalized = true;
        Ok(())
    }

    #[must_use]
    pub const fn is_finalized(&self) -> bool {
        self.finalized
    }

    #[must_use]
    pub fn outputs(&self) -> &BTreeMap<u64, OutputFrameResult> {
        &self.outputs
    }

    #[must_use]
    pub const fn aggregate_hash(&self) -> u64 {
        self.aggregate_hash
    }

    #[must_use]
    pub const fn layout_generation(&self) -> u64 {
        self.layout_generation
    }

    #[must_use]
    pub const fn primary_output_id(&self) -> u64 {
        self.primary_output_id
    }
}

fn append_byte(hash: &mut u64, value: u8) {
    *hash ^= u64::from(value);
    *hash = hash.wrapping_mul(FNV_PRIME);
}

fn append_u32(hash: &mut u64, value: u32) {
    for byte in value.to_le_bytes() {
        append_byte(hash, byte);
    }
}

fn append_u64(hash: &mut u64, value: u64) {
    for byte in value.to_le_bytes() {
        append_byte(hash, byte);
    }
}

#[must_use]
pub fn calculate_frame_set_aggregate_hash(
    outputs: &BTreeMap<u64, OutputFrameResult>,
    layout_generation: u64,
    primary_output_id: u64,
) -> u64 {
    let mut hash = FNV_OFFSET;
    for byte in b"glasswyrm-output-frame-set-v1" {
        append_byte(&mut hash, *byte);
    }
    append_u64(&mut hash, layout_generation);
    append_u64(&mut hash, primary_output_id);
    for (&output_id, output) in outputs {
        append_u64(&mut hash, output_id);
        append_u32(&mut hash, output.output.width);
        append_u32(&mut hash, output.output.height);
        append_u32(&mut hash, output.scale.numerator);
        append_u32(&mut hash, output.scale.denominator);
        append_u32(&mut hash, output.transform as u32);
        append_u64(&mut hash, output.visible_hash);
    }
    hash
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FrameSetError {
    AlreadyFinalized,
    OutputLimit,
    InconsistentMetadata,
    InvalidDamage,
    PixelLimit,
    DuplicateOutput,
    IncompleteCommit,
}
