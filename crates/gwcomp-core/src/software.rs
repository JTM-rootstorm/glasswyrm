use core::fmt;
use std::time::Instant;

use crate::geometry::Rectangle;

pub const FULL_OPACITY: u32 = 65_536;
const FNV_OFFSET: u64 = 14_695_981_039_346_656_037;
const FNV_PRIME: u64 = 1_099_511_628_211;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PixelFormat {
    Xrgb8888,
    Argb8888Premultiplied,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Pixel {
    pub red: u8,
    pub green: u8,
    pub blue: u8,
    pub alpha: u8,
}

impl Pixel {
    #[must_use]
    pub const fn new(red: u8, green: u8, blue: u8, alpha: u8) -> Self {
        Self {
            red,
            green,
            blue,
            alpha,
        }
    }

    #[must_use]
    pub const fn is_premultiplied(self) -> bool {
        self.red <= self.alpha && self.green <= self.alpha && self.blue <= self.alpha
    }
}

#[must_use]
pub const fn unpack_xrgb8888(value: u32) -> Pixel {
    Pixel::new((value >> 16) as u8, (value >> 8) as u8, value as u8, 255)
}

#[must_use]
pub const fn unpack_argb8888(value: u32) -> Pixel {
    Pixel::new(
        (value >> 16) as u8,
        (value >> 8) as u8,
        value as u8,
        (value >> 24) as u8,
    )
}

#[must_use]
pub const fn pack_xrgb8888(pixel: Pixel) -> u32 {
    0xff00_0000 | ((pixel.red as u32) << 16) | ((pixel.green as u32) << 8) | pixel.blue as u32
}

fn scale(value: u8, opacity: u32) -> u8 {
    let bounded = opacity.min(FULL_OPACITY);
    ((u32::from(value) * bounded + 32_768) >> 16) as u8
}

#[must_use]
pub fn apply_opacity(source: Pixel, opacity: u32) -> Pixel {
    Pixel::new(
        scale(source.red, opacity),
        scale(source.green, opacity),
        scale(source.blue, opacity),
        scale(source.alpha, opacity),
    )
}

fn over_channel(source: u8, destination: u8, alpha: u8) -> u8 {
    let value = u32::from(source) + (u32::from(destination) * (255 - u32::from(alpha)) + 127) / 255;
    value.min(255) as u8
}

#[must_use]
pub fn source_over(source: Pixel, destination: Pixel) -> Pixel {
    Pixel::new(
        over_channel(source.red, destination.red, source.alpha),
        over_channel(source.green, destination.green, source.alpha),
        over_channel(source.blue, destination.blue, source.alpha),
        255,
    )
}

#[must_use]
pub fn blend(source: Pixel, destination: Pixel, opacity: u32) -> Pixel {
    source_over(apply_opacity(source, opacity), destination)
}

#[derive(Clone, Copy, Debug)]
pub struct ImageView<'a> {
    pub bytes: &'a [u8],
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: PixelFormat,
}

#[derive(Debug)]
pub struct FramebufferView<'a> {
    pub bytes: &'a mut [u8],
    pub width: u32,
    pub height: u32,
    pub stride: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RenderResult {
    Success,
    InvalidView,
    InvalidPremultipliedPixel,
}

fn required_bytes(width: u32, height: u32, stride: u32) -> Option<u64> {
    let row = u64::from(width).checked_mul(4)?;
    if width == 0 || height == 0 || u64::from(stride) < row {
        return None;
    }
    u64::from(height - 1)
        .checked_mul(u64::from(stride))?
        .checked_add(row)
}

fn valid_image(image: &ImageView<'_>) -> bool {
    required_bytes(image.width, image.height, image.stride)
        .is_some_and(|required| required <= image.bytes.len() as u64)
}

fn valid_framebuffer(image: &FramebufferView<'_>) -> bool {
    required_bytes(image.width, image.height, image.stride)
        .is_some_and(|required| required <= image.bytes.len() as u64)
}

fn load_u32(bytes: &[u8], offset: usize) -> Option<u32> {
    let value: [u8; 4] = bytes.get(offset..offset.checked_add(4)?)?.try_into().ok()?;
    Some(u32::from_ne_bytes(value))
}

fn store_u32(bytes: &mut [u8], offset: usize, value: u32) -> bool {
    let Some(destination) = offset
        .checked_add(4)
        .and_then(|end| bytes.get_mut(offset..end))
    else {
        return false;
    };
    destination.copy_from_slice(&value.to_ne_bytes());
    true
}

pub fn clear(destination: &mut FramebufferView<'_>, rectangle: Rectangle) -> RenderResult {
    if !valid_framebuffer(destination) {
        return RenderResult::InvalidView;
    }
    let bounds = Rectangle::new(0, 0, destination.width, destination.height);
    let Some(clipped) = rectangle.intersection(bounds) else {
        return RenderResult::Success;
    };
    for y in 0..clipped.height {
        for x in 0..clipped.width {
            let offset = (clipped.y as usize + y as usize) * destination.stride as usize
                + (clipped.x as usize + x as usize) * 4;
            if !store_u32(destination.bytes, offset, SoftwareFrame::CLEAR_PIXEL) {
                return RenderResult::InvalidView;
            }
        }
    }
    RenderResult::Success
}

pub fn composite(
    destination: &mut FramebufferView<'_>,
    source: &ImageView<'_>,
    source_rectangle: Rectangle,
    destination_x: i32,
    destination_y: i32,
    opacity: u32,
) -> RenderResult {
    if !valid_framebuffer(destination)
        || !valid_image(source)
        || !source_rectangle.has_valid_extents()
    {
        return RenderResult::InvalidView;
    }
    let source_bounds = Rectangle::new(0, 0, source.width, source.height);
    let Some(sampled) = source_rectangle.intersection(source_bounds) else {
        return RenderResult::Success;
    };
    let Some(translated) = sampled.translate(destination_x, destination_y) else {
        return RenderResult::InvalidView;
    };
    let destination_bounds = Rectangle::new(0, 0, destination.width, destination.height);
    let Some(painted) = translated.intersection(destination_bounds) else {
        return RenderResult::Success;
    };

    let source_start_x = i64::from(sampled.x) + i64::from(painted.x) - i64::from(translated.x);
    let source_start_y = i64::from(sampled.y) + i64::from(painted.y) - i64::from(translated.y);
    if source_start_x < 0 || source_start_y < 0 {
        return RenderResult::InvalidView;
    }
    let source_start_x = source_start_x as usize;
    let source_start_y = source_start_y as usize;

    if source.format == PixelFormat::Argb8888Premultiplied {
        for y in 0..painted.height as usize {
            for x in 0..painted.width as usize {
                let offset =
                    (source_start_y + y) * source.stride as usize + (source_start_x + x) * 4;
                let Some(pixel) = load_u32(source.bytes, offset) else {
                    return RenderResult::InvalidView;
                };
                if !unpack_argb8888(pixel).is_premultiplied() {
                    return RenderResult::InvalidPremultipliedPixel;
                }
            }
        }
    }

    for y in 0..painted.height as usize {
        for x in 0..painted.width as usize {
            let source_offset =
                (source_start_y + y) * source.stride as usize + (source_start_x + x) * 4;
            let destination_offset = (painted.y as usize + y) * destination.stride as usize
                + (painted.x as usize + x) * 4;
            let Some(source_word) = load_u32(source.bytes, source_offset) else {
                return RenderResult::InvalidView;
            };
            let source_pixel = match source.format {
                PixelFormat::Xrgb8888 => unpack_xrgb8888(source_word),
                PixelFormat::Argb8888Premultiplied => unpack_argb8888(source_word),
            };
            let Some(destination_word) = load_u32(destination.bytes, destination_offset) else {
                return RenderResult::InvalidView;
            };
            let output = pack_xrgb8888(blend(
                source_pixel,
                unpack_xrgb8888(destination_word),
                opacity,
            ));
            if !store_u32(destination.bytes, destination_offset, output) {
                return RenderResult::InvalidView;
            }
        }
    }
    RenderResult::Success
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct OutputSpec {
    pub output_id: u64,
    pub width: u32,
    pub height: u32,
    pub refresh_millihertz: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct FrameHashMeasurement {
    pub hash: u64,
    pub bytes: u64,
    pub nanoseconds: u64,
}

#[must_use]
pub fn hash_visible_xrgb8888_measured(pixels: &[u32]) -> FrameHashMeasurement {
    let started = Instant::now();
    let mut hash = FNV_OFFSET;
    for pixel in pixels {
        for byte in [(pixel >> 16) as u8, (pixel >> 8) as u8, *pixel as u8] {
            hash ^= u64::from(byte);
            hash = hash.wrapping_mul(FNV_PRIME);
        }
    }
    FrameHashMeasurement {
        hash,
        bytes: u64::try_from(pixels.len())
            .unwrap_or(u64::MAX)
            .saturating_mul(3),
        nanoseconds: u64::try_from(started.elapsed().as_nanos()).unwrap_or(u64::MAX),
    }
}

#[must_use]
pub fn hash_visible_xrgb8888(pixels: &[u32]) -> u64 {
    hash_visible_xrgb8888_measured(pixels).hash
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SoftwareFrameError {
    ZeroOutputId,
    DimensionsOutOfRange,
    FramebufferTooLarge,
    AllocationFailed,
}

impl fmt::Display for SoftwareFrameError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::ZeroOutputId => "headless output ID must be nonzero",
            Self::DimensionsOutOfRange => "headless output dimensions are outside supported limits",
            Self::FramebufferTooLarge => "headless framebuffer exceeds supported limits",
            Self::AllocationFailed => "headless framebuffer allocation failed",
        })
    }
}

impl std::error::Error for SoftwareFrameError {}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct SoftwareFrame {
    id: u64,
    width: u32,
    height: u32,
    enabled: bool,
    pixels: Vec<u32>,
}

impl SoftwareFrame {
    pub const MAXIMUM_WIDTH: u32 = 4096;
    pub const MAXIMUM_HEIGHT: u32 = 4096;
    pub const MAXIMUM_PIXELS: u64 = 16_777_216;
    pub const MAXIMUM_BYTES: u64 = 67_108_864;
    pub const CLEAR_PIXEL: u32 = 0xff00_0000;

    pub fn configure(
        &mut self,
        id: u64,
        width: u32,
        height: u32,
    ) -> Result<(), SoftwareFrameError> {
        if id == 0 {
            return Err(SoftwareFrameError::ZeroOutputId);
        }
        if width == 0 || height == 0 || width > Self::MAXIMUM_WIDTH || height > Self::MAXIMUM_HEIGHT
        {
            return Err(SoftwareFrameError::DimensionsOutOfRange);
        }
        let pixels = u64::from(width) * u64::from(height);
        if pixels > Self::MAXIMUM_PIXELS
            || pixels > Self::MAXIMUM_BYTES / size_of::<u32>() as u64
            || usize::try_from(pixels).is_err()
        {
            return Err(SoftwareFrameError::FramebufferTooLarge);
        }
        let length = pixels as usize;
        let mut replacement = Vec::new();
        replacement
            .try_reserve_exact(length)
            .map_err(|_| SoftwareFrameError::AllocationFailed)?;
        replacement.resize(length, Self::CLEAR_PIXEL);

        self.id = id;
        self.width = width;
        self.height = height;
        self.enabled = true;
        self.pixels = replacement;
        Ok(())
    }

    pub fn disable(&mut self) {
        self.id = 0;
        self.width = 0;
        self.height = 0;
        self.enabled = false;
        self.pixels.clear();
    }

    #[must_use]
    pub const fn id(&self) -> u64 {
        self.id
    }

    #[must_use]
    pub const fn width(&self) -> u32 {
        self.width
    }

    #[must_use]
    pub const fn height(&self) -> u32 {
        self.height
    }

    #[must_use]
    pub const fn is_enabled(&self) -> bool {
        self.enabled
    }

    #[must_use]
    pub const fn spec(&self, refresh_millihertz: u32) -> OutputSpec {
        OutputSpec {
            output_id: self.id,
            width: self.width,
            height: self.height,
            refresh_millihertz,
        }
    }

    #[must_use]
    pub fn visible_hash(&self) -> u64 {
        hash_visible_xrgb8888(&self.pixels)
    }

    #[must_use]
    pub fn pixels(&self) -> &[u32] {
        &self.pixels
    }

    pub fn pixels_mut(&mut self) -> &mut [u32] {
        &mut self.pixels
    }
}
