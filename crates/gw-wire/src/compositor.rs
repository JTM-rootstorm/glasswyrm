use core::fmt;

use crate::{ByteReader, ByteWriter, PrimitiveDecodeError};

pub const MAXIMUM_DAMAGE_RECTANGLES: usize = 1024;
pub const OPACITY_ONE: u32 = 0x0001_0000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContractDecodeError {
    Truncated,
    TrailingData,
    InvalidValue,
    LimitExceeded,
    SizeMismatch,
}

impl fmt::Display for ContractDecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Truncated => "GWIPC payload is truncated",
            Self::TrailingData => "GWIPC payload has trailing data",
            Self::InvalidValue => "GWIPC payload contains an invalid value",
            Self::LimitExceeded => "GWIPC payload exceeds a contract limit",
            Self::SizeMismatch => "GWIPC payload has an unexpected size",
        })
    }
}
impl std::error::Error for ContractDecodeError {}
impl From<PrimitiveDecodeError> for ContractDecodeError {
    fn from(value: PrimitiveDecodeError) -> Self {
        match value {
            PrimitiveDecodeError::Truncated => Self::Truncated,
            PrimitiveDecodeError::TrailingData => Self::TrailingData,
            PrimitiveDecodeError::LimitExceeded => Self::LimitExceeded,
            PrimitiveDecodeError::InvalidUtf8 => Self::InvalidValue,
        }
    }
}

macro_rules! wire_enum {
    ($name:ident: $ty:ty { $($variant:ident = $value:expr),+ $(,)? }) => {
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        #[repr($ty)] pub enum $name { $($variant = $value),+ }
        impl TryFrom<$ty> for $name { type Error = ContractDecodeError; fn try_from(raw: $ty) -> Result<Self, Self::Error> { match raw { $($value => Ok(Self::$variant),)+ _ => Err(ContractDecodeError::InvalidValue) } } }
    };
}
wire_enum!(Transform: u16 { Normal=0, Rotate90=1, Rotate180=2, Rotate270=3, Flipped=4, Flipped90=5, Flipped180=6, Flipped270=7 });
wire_enum!(SdrColorSpace: u16 { Srgb=1, DisplayP3=2 });
wire_enum!(TransferFunction: u16 { Srgb=1, Linear=2 });
wire_enum!(ColorPrimaries: u16 { Srgb=1, DisplayP3=2 });
wire_enum!(TriState: u8 { Unknown=0, False=1, True=2 });
wire_enum!(PixelFormat: u16 { Xrgb8888=1, Argb8888=2 });
wire_enum!(AlphaSemantics: u16 { Opaque=1, Premultiplied=2 });
wire_enum!(SynchronizationMode: u16 { None=0, EventFd=1 });
wire_enum!(BufferReleaseReason: u16 { Replaced=1, SurfaceRemoved=2, ConsumerDone=3, Invalid=4 });
wire_enum!(FrameResult: u16 { Accepted=1, RejectedIncompleteMetadata=2, RejectedInvalidBuffer=3, RejectedUnknownSurface=4, Dropped=5 });

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SdrColorMetadata {
    pub color_space: SdrColorSpace,
    pub transfer_function: TransferFunction,
    pub primaries: ColorPrimaries,
    pub luminance_available: bool,
    pub minimum_luminance_millinit: u32,
    pub maximum_luminance_millinit: u32,
    pub max_frame_average_luminance_millinit: u32,
}
impl Default for SdrColorMetadata {
    fn default() -> Self {
        Self {
            color_space: SdrColorSpace::Srgb,
            transfer_function: TransferFunction::Srgb,
            primaries: ColorPrimaries::Srgb,
            luminance_available: false,
            minimum_luminance_millinit: 0,
            maximum_luminance_millinit: 0,
            max_frame_average_luminance_millinit: 0,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutputUpsert {
    pub output_id: u64,
    pub enabled: bool,
    pub logical_x: i32,
    pub logical_y: i32,
    pub logical_width: u32,
    pub logical_height: u32,
    pub physical_pixel_width: u32,
    pub physical_pixel_height: u32,
    pub refresh_millihertz: u32,
    pub scale_numerator: u32,
    pub scale_denominator: u32,
    pub transform: Transform,
    pub color: SdrColorMetadata,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputRemove {
    pub output_id: u64,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceUpsert {
    pub surface_id: u64,
    pub x11_window_id: u32,
    pub parent_surface_id: u64,
    pub output_id: u64,
    pub logical_x: i32,
    pub logical_y: i32,
    pub logical_width: u32,
    pub logical_height: u32,
    pub stacking: i32,
    pub visible: bool,
    pub clipping: bool,
    pub clip_x: i32,
    pub clip_y: i32,
    pub clip_width: u32,
    pub clip_height: u32,
    pub transform: Transform,
    pub opacity: u32,
    pub scale_numerator: u32,
    pub scale_denominator: u32,
    pub color: SdrColorMetadata,
    pub presentation_flags: u32,
    pub fullscreen_eligible: TriState,
    pub direct_scanout_eligible: TriState,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SurfaceRemove {
    pub surface_id: u64,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BufferAttach {
    pub buffer_id: u64,
    pub surface_id: u64,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub byte_offset: u64,
    pub storage_size: u64,
    pub pixel_format: PixelFormat,
    pub modifier: u64,
    pub alpha_semantics: AlphaSemantics,
    pub color: SdrColorMetadata,
    pub synchronization: SynchronizationMode,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BufferDetach {
    pub surface_id: u64,
    pub buffer_id: u64,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BufferRelease {
    pub buffer_id: u64,
    pub reason: BufferReleaseReason,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DamageRectangle {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceDamage {
    pub surface_id: u64,
    pub rectangles: Vec<DamageRectangle>,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FrameCommit {
    pub commit_id: u64,
    pub output_id: u64,
    pub producer_generation: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FrameAcknowledged {
    pub commit_id: u64,
    pub output_id: u64,
    pub presented_generation: u64,
    pub result: FrameResult,
}

fn decode_bool(value: u8) -> Result<bool, ContractDecodeError> {
    match value {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(ContractDecodeError::InvalidValue),
    }
}
fn finish(r: ByteReader<'_>, valid: bool) -> Result<(), ContractDecodeError> {
    if !r.is_empty() {
        Err(ContractDecodeError::TrailingData)
    } else if valid {
        Ok(())
    } else {
        Err(ContractDecodeError::InvalidValue)
    }
}
fn write_color(w: &mut ByteWriter, c: &SdrColorMetadata) {
    w.write_u16(c.color_space as u16);
    w.write_u16(c.transfer_function as u16);
    w.write_u16(c.primaries as u16);
    w.write_u8(u8::from(c.luminance_available));
    w.write_u8(0);
    w.write_u32(c.minimum_luminance_millinit);
    w.write_u32(c.maximum_luminance_millinit);
    w.write_u32(c.max_frame_average_luminance_millinit);
}
struct RawSdrColorMetadata {
    color_space: u16,
    transfer_function: u16,
    primaries: u16,
    luminance_available: u8,
    reserved: u8,
    minimum_luminance_millinit: u32,
    maximum_luminance_millinit: u32,
    max_frame_average_luminance_millinit: u32,
}

fn read_color(r: &mut ByteReader<'_>) -> Result<RawSdrColorMetadata, ContractDecodeError> {
    Ok(RawSdrColorMetadata {
        color_space: r.read_u16()?,
        transfer_function: r.read_u16()?,
        primaries: r.read_u16()?,
        luminance_available: r.read_u8()?,
        reserved: r.read_u8()?,
        minimum_luminance_millinit: r.read_u32()?,
        maximum_luminance_millinit: r.read_u32()?,
        max_frame_average_luminance_millinit: r.read_u32()?,
    })
}

fn decode_color(raw: RawSdrColorMetadata) -> Result<SdrColorMetadata, ContractDecodeError> {
    let color = SdrColorMetadata {
        color_space: raw.color_space.try_into()?,
        transfer_function: raw.transfer_function.try_into()?,
        primaries: raw.primaries.try_into()?,
        luminance_available: decode_bool(raw.luminance_available)?,
        minimum_luminance_millinit: raw.minimum_luminance_millinit,
        maximum_luminance_millinit: raw.maximum_luminance_millinit,
        max_frame_average_luminance_millinit: raw.max_frame_average_luminance_millinit,
    };
    let valid = raw.reserved == 0
        && if color.luminance_available {
            color.maximum_luminance_millinit != 0
                && color.minimum_luminance_millinit <= color.maximum_luminance_millinit
                && color.max_frame_average_luminance_millinit <= color.maximum_luminance_millinit
        } else {
            color.minimum_luminance_millinit == 0
                && color.maximum_luminance_millinit == 0
                && color.max_frame_average_luminance_millinit == 0
        };
    if valid {
        Ok(color)
    } else {
        Err(ContractDecodeError::InvalidValue)
    }
}
fn valid_rectangle(v: &DamageRectangle) -> bool {
    if v.width == 0 || v.height == 0 {
        return false;
    }
    let max = i32::MAX as i64;
    i64::from(v.x) + i64::from(v.width - 1) <= max
        && i64::from(v.y) + i64::from(v.height - 1) <= max
}

#[must_use]
pub fn encode_output_upsert(v: &OutputUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u8(u8::from(v.enabled));
    w.write_u8(0);
    w.write_u16(v.transform as u16);
    w.write_i32(v.logical_x);
    w.write_i32(v.logical_y);
    w.write_u32(v.logical_width);
    w.write_u32(v.logical_height);
    w.write_u32(v.physical_pixel_width);
    w.write_u32(v.physical_pixel_height);
    w.write_u32(v.refresh_millihertz);
    w.write_u32(v.scale_numerator);
    w.write_u32(v.scale_denominator);
    write_color(&mut w, &v.color);
    w.into_bytes()
}
pub fn decode_output_upsert(bytes: &[u8]) -> Result<OutputUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let enabled = r.read_u8()?;
    let reserved = r.read_u8()?;
    let transform = r.read_u16()?;
    let logical_x = r.read_i32()?;
    let logical_y = r.read_i32()?;
    let logical_width = r.read_u32()?;
    let logical_height = r.read_u32()?;
    let physical_pixel_width = r.read_u32()?;
    let physical_pixel_height = r.read_u32()?;
    let refresh_millihertz = r.read_u32()?;
    let scale_numerator = r.read_u32()?;
    let scale_denominator = r.read_u32()?;
    let color = read_color(&mut r)?;
    finish(r, true)?;
    let v = OutputUpsert {
        output_id,
        enabled: decode_bool(enabled)?,
        transform: transform.try_into()?,
        logical_x,
        logical_y,
        logical_width,
        logical_height,
        physical_pixel_width,
        physical_pixel_height,
        refresh_millihertz,
        scale_numerator,
        scale_denominator,
        color: decode_color(color)?,
    };
    let valid = v.output_id != 0
        && v.scale_numerator != 0
        && v.scale_denominator != 0
        && (!v.enabled
            || (v.logical_width != 0
                && v.logical_height != 0
                && v.physical_pixel_width != 0
                && v.physical_pixel_height != 0
                && v.refresh_millihertz != 0));
    if reserved != 0 || !valid {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_output_remove(v: &OutputRemove) -> Vec<u8> {
    v.output_id.to_le_bytes().to_vec()
}
pub fn decode_output_remove(bytes: &[u8]) -> Result<OutputRemove, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = OutputRemove {
        output_id: r.read_u64()?,
    };
    finish(r, v.output_id != 0)?;
    Ok(v)
}

#[must_use]
pub fn encode_surface_upsert(v: &SurfaceUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.surface_id);
    w.write_u32(v.x11_window_id);
    w.write_u32(0);
    w.write_u64(v.parent_surface_id);
    w.write_u64(v.output_id);
    w.write_i32(v.logical_x);
    w.write_i32(v.logical_y);
    w.write_u32(v.logical_width);
    w.write_u32(v.logical_height);
    w.write_i32(v.stacking);
    w.write_u8(u8::from(v.visible));
    w.write_u8(u8::from(v.clipping));
    w.write_u16(v.transform as u16);
    w.write_i32(v.clip_x);
    w.write_i32(v.clip_y);
    w.write_u32(v.clip_width);
    w.write_u32(v.clip_height);
    w.write_u32(v.opacity);
    w.write_u32(v.scale_numerator);
    w.write_u32(v.scale_denominator);
    write_color(&mut w, &v.color);
    w.write_u32(v.presentation_flags);
    w.write_u8(v.fullscreen_eligible as u8);
    w.write_u8(v.direct_scanout_eligible as u8);
    w.write_u16(0);
    w.into_bytes()
}
pub fn decode_surface_upsert(bytes: &[u8]) -> Result<SurfaceUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let surface_id = r.read_u64()?;
    let x11_window_id = r.read_u32()?;
    let reserved1 = r.read_u32()?;
    let parent_surface_id = r.read_u64()?;
    let output_id = r.read_u64()?;
    let logical_x = r.read_i32()?;
    let logical_y = r.read_i32()?;
    let logical_width = r.read_u32()?;
    let logical_height = r.read_u32()?;
    let stacking = r.read_i32()?;
    let visible = r.read_u8()?;
    let clipping = r.read_u8()?;
    let transform = r.read_u16()?;
    let clip_x = r.read_i32()?;
    let clip_y = r.read_i32()?;
    let clip_width = r.read_u32()?;
    let clip_height = r.read_u32()?;
    let opacity = r.read_u32()?;
    let scale_numerator = r.read_u32()?;
    let scale_denominator = r.read_u32()?;
    let color = read_color(&mut r)?;
    let presentation_flags = r.read_u32()?;
    let fullscreen_eligible = r.read_u8()?;
    let direct_scanout_eligible = r.read_u8()?;
    let reserved2 = r.read_u16()?;
    finish(r, true)?;
    let v = SurfaceUpsert {
        surface_id,
        x11_window_id,
        parent_surface_id,
        output_id,
        logical_x,
        logical_y,
        logical_width,
        logical_height,
        stacking,
        visible: decode_bool(visible)?,
        clipping: decode_bool(clipping)?,
        clip_x,
        clip_y,
        clip_width,
        clip_height,
        transform: transform.try_into()?,
        opacity,
        scale_numerator,
        scale_denominator,
        color: decode_color(color)?,
        presentation_flags,
        fullscreen_eligible: fullscreen_eligible.try_into()?,
        direct_scanout_eligible: direct_scanout_eligible.try_into()?,
    };
    let valid = v.surface_id != 0
        && v.logical_width != 0
        && v.logical_height != 0
        && reserved1 == 0
        && reserved2 == 0
        && v.opacity <= OPACITY_ONE
        && v.scale_numerator != 0
        && v.scale_denominator != 0
        && (v.presentation_flags & !3) == 0
        && v.presentation_flags != 3
        && (!v.clipping
            || valid_rectangle(&DamageRectangle {
                x: v.clip_x,
                y: v.clip_y,
                width: v.clip_width,
                height: v.clip_height,
            }));
    if !valid {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_surface_remove(v: &SurfaceRemove) -> Vec<u8> {
    v.surface_id.to_le_bytes().to_vec()
}
pub fn decode_surface_remove(bytes: &[u8]) -> Result<SurfaceRemove, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = SurfaceRemove {
        surface_id: r.read_u64()?,
    };
    finish(r, v.surface_id != 0)?;
    Ok(v)
}

#[must_use]
pub fn encode_buffer_attach(v: &BufferAttach) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.buffer_id);
    w.write_u64(v.surface_id);
    w.write_u32(v.width);
    w.write_u32(v.height);
    w.write_u32(v.stride);
    w.write_u32(0);
    w.write_u64(v.byte_offset);
    w.write_u64(v.storage_size);
    w.write_u16(v.pixel_format as u16);
    w.write_u16(v.alpha_semantics as u16);
    w.write_u64(v.modifier);
    write_color(&mut w, &v.color);
    w.write_u16(v.synchronization as u16);
    w.write_u16(0);
    w.write_u32(v.flags);
    w.into_bytes()
}
pub fn decode_buffer_attach(bytes: &[u8]) -> Result<BufferAttach, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let buffer_id = r.read_u64()?;
    let surface_id = r.read_u64()?;
    let width = r.read_u32()?;
    let height = r.read_u32()?;
    let stride = r.read_u32()?;
    let reserved1 = r.read_u32()?;
    let byte_offset = r.read_u64()?;
    let storage_size = r.read_u64()?;
    let pixel_format = r.read_u16()?;
    let alpha_semantics = r.read_u16()?;
    let modifier = r.read_u64()?;
    let color = read_color(&mut r)?;
    let synchronization = r.read_u16()?;
    let reserved2 = r.read_u16()?;
    let flags = r.read_u32()?;
    finish(r, true)?;
    let v = BufferAttach {
        buffer_id,
        surface_id,
        width,
        height,
        stride,
        byte_offset,
        storage_size,
        pixel_format: pixel_format.try_into()?,
        modifier,
        alpha_semantics: alpha_semantics.try_into()?,
        color: decode_color(color)?,
        synchronization: synchronization.try_into()?,
        flags,
    };
    let row = u64::from(width) * 4;
    let required = u64::from(height.saturating_sub(1))
        .checked_mul(u64::from(stride))
        .and_then(|x| x.checked_add(row));
    let geometry = width != 0
        && height != 0
        && width <= u32::MAX / 4
        && u64::from(stride) >= row
        && required.is_some_and(|n| byte_offset <= storage_size && n <= storage_size - byte_offset);
    let format = (v.pixel_format == PixelFormat::Xrgb8888
        && v.alpha_semantics == AlphaSemantics::Opaque)
        || (v.pixel_format == PixelFormat::Argb8888
            && v.alpha_semantics == AlphaSemantics::Premultiplied);
    if !(buffer_id != 0
        && surface_id != 0
        && reserved1 == 0
        && reserved2 == 0
        && geometry
        && format
        && modifier == 0
        && flags == 0)
    {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[must_use]
pub fn encode_buffer_detach(v: &BufferDetach) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.surface_id);
    w.write_u64(v.buffer_id);
    w.into_bytes()
}
pub fn decode_buffer_detach(bytes: &[u8]) -> Result<BufferDetach, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = BufferDetach {
        surface_id: r.read_u64()?,
        buffer_id: r.read_u64()?,
    };
    finish(r, v.surface_id != 0 && v.buffer_id != 0)?;
    Ok(v)
}
#[must_use]
pub fn encode_buffer_release(v: &BufferRelease) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.buffer_id);
    w.write_u16(v.reason as u16);
    w.write_u16(0);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_buffer_release(bytes: &[u8]) -> Result<BufferRelease, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let buffer_id = r.read_u64()?;
    let reason = r.read_u16()?;
    let a = r.read_u16()?;
    let b = r.read_u32()?;
    finish(r, true)?;
    let v = BufferRelease {
        buffer_id,
        reason: reason.try_into()?,
    };
    if v.buffer_id == 0 || a != 0 || b != 0 {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

pub fn encode_surface_damage(v: &SurfaceDamage) -> Result<Vec<u8>, ContractDecodeError> {
    if v.rectangles.len() > MAXIMUM_DAMAGE_RECTANGLES {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let mut w = ByteWriter::new();
    w.write_u64(v.surface_id);
    w.write_u32(v.rectangles.len() as u32);
    w.write_u32(0);
    for x in &v.rectangles {
        w.write_i32(x.x);
        w.write_i32(x.y);
        w.write_u32(x.width);
        w.write_u32(x.height)
    }
    Ok(w.into_bytes())
}
pub fn decode_surface_damage(bytes: &[u8]) -> Result<SurfaceDamage, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let surface_id = r.read_u64()?;
    let count = r.read_u32()? as usize;
    let reserved = r.read_u32()?;
    if count > MAXIMUM_DAMAGE_RECTANGLES {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let required = count
        .checked_mul(16)
        .ok_or(ContractDecodeError::LimitExceeded)?;
    if r.remaining() < required {
        return Err(ContractDecodeError::Truncated);
    }
    if r.remaining() > required {
        return Err(ContractDecodeError::TrailingData);
    }
    let mut rectangles = Vec::with_capacity(count);
    let mut valid = surface_id != 0 && reserved == 0;
    for _ in 0..count {
        let x = DamageRectangle {
            x: r.read_i32()?,
            y: r.read_i32()?,
            width: r.read_u32()?,
            height: r.read_u32()?,
        };
        valid &= valid_rectangle(&x);
        rectangles.push(x)
    }
    finish(r, valid)?;
    Ok(SurfaceDamage {
        surface_id,
        rectangles,
    })
}

#[must_use]
pub fn encode_frame_commit(v: &FrameCommit) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.commit_id);
    w.write_u64(v.output_id);
    w.write_u64(v.producer_generation);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_frame_commit(bytes: &[u8]) -> Result<FrameCommit, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = FrameCommit {
        commit_id: r.read_u64()?,
        output_id: r.read_u64()?,
        producer_generation: r.read_u64()?,
        flags: r.read_u32()?,
    };
    let reserved = r.read_u32()?;
    finish(r, v.commit_id != 0 && v.flags == 0 && reserved == 0)?;
    Ok(v)
}
#[must_use]
pub fn encode_frame_acknowledged(v: &FrameAcknowledged) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.commit_id);
    w.write_u64(v.output_id);
    w.write_u64(v.presented_generation);
    w.write_u16(v.result as u16);
    w.write_u16(0);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_frame_acknowledged(bytes: &[u8]) -> Result<FrameAcknowledged, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let commit_id = r.read_u64()?;
    let output_id = r.read_u64()?;
    let presented_generation = r.read_u64()?;
    let result = r.read_u16()?;
    let a = r.read_u16()?;
    let b = r.read_u32()?;
    finish(r, true)?;
    let v = FrameAcknowledged {
        commit_id,
        output_id,
        presented_generation,
        result: result.try_into()?,
    };
    if v.commit_id == 0 || a != 0 || b != 0 {
        return Err(ContractDecodeError::InvalidValue);
    }
    Ok(v)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn malformed_value_does_not_mask_payload_shape() {
        let invalid = vec![0; 32];
        assert_eq!(
            decode_frame_acknowledged(&invalid),
            Err(ContractDecodeError::InvalidValue)
        );
        assert_eq!(
            decode_frame_acknowledged(&invalid[..31]),
            Err(ContractDecodeError::Truncated)
        );

        let mut trailing = invalid;
        trailing.push(0);
        assert_eq!(
            decode_frame_acknowledged(&trailing),
            Err(ContractDecodeError::TrailingData)
        );
    }
}
