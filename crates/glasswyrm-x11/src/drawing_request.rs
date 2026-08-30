use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

pub const GC_VALUE_MASK: u32 = (1 << 0)
    | (1 << 1)
    | (1 << 2)
    | (1 << 3)
    | (1 << 4)
    | (1 << 5)
    | (1 << 6)
    | (1 << 7)
    | (1 << 8)
    | (1 << 11)
    | (1 << 14)
    | (1 << 15)
    | (1 << 16)
    | (1 << 17)
    | (1 << 18)
    | (1 << 19);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CreatePixmapRequest {
    pub depth: u8,
    pub xid: u32,
    pub drawable: u32,
    pub width: u16,
    pub height: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CreateGcRequest<'a> {
    pub xid: u32,
    pub drawable: u32,
    pub value_mask: u32,
    values: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChangeGcRequest<'a> {
    pub gc: u32,
    pub value_mask: u32,
    values: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GcValue {
    pub bit: u8,
    pub value: u32,
}

#[derive(Clone, Copy, Debug)]
pub struct GcValues<'a> {
    order: ByteOrder,
    mask: u32,
    next_bit: u8,
    remaining: &'a [u8],
}

impl Iterator for GcValues<'_> {
    type Item = GcValue;

    fn next(&mut self) -> Option<Self::Item> {
        while self.next_bit < 32 {
            let bit = self.next_bit;
            self.next_bit += 1;
            if self.mask & (1_u32 << bit) == 0 {
                continue;
            }
            let value = read_u32(self.order, self.remaining, 0);
            self.remaining = &self.remaining[4..];
            return Some(GcValue { bit, value });
        }
        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = if self.next_bit == 32 {
            0
        } else {
            usize::try_from((self.mask >> self.next_bit).count_ones())
                .expect("a u32 population count fits usize")
        };
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for GcValues<'_> {}

impl<'a> CreateGcRequest<'a> {
    #[must_use]
    pub fn values(self, order: ByteOrder) -> GcValues<'a> {
        gc_values(order, self.value_mask, self.values)
    }
}

impl<'a> ChangeGcRequest<'a> {
    #[must_use]
    pub fn values(self, order: ByteOrder) -> GcValues<'a> {
        gc_values(order, self.value_mask, self.values)
    }
}

fn gc_values(order: ByteOrder, mask: u32, values: &[u8]) -> GcValues<'_> {
    GcValues {
        order,
        mask,
        next_bit: 0,
        remaining: values,
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum CoordinateMode {
    Origin = 0,
    Previous = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum PolygonShape {
    Complex = 0,
    Nonconvex = 1,
    Convex = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ImageFormat {
    XyBitmap = 0,
    XyPixmap = 1,
    ZPixmap = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ClearAreaRequest {
    pub exposures: bool,
    pub window: u32,
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CopyAreaRequest {
    pub source: u32,
    pub destination: u32,
    pub gc: u32,
    pub source_x: i16,
    pub source_y: i16,
    pub destination_x: i16,
    pub destination_y: i16,
    pub width: u16,
    pub height: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Point {
    pub x: i16,
    pub y: i16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ResolvedPoint {
    pub x: i32,
    pub y: i32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Segment {
    pub first: Point,
    pub second: Point,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Rectangle {
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Arc {
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub angle1: i16,
    pub angle2: i16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolyLineRequest<'a> {
    pub coordinate_mode: CoordinateMode,
    pub drawable: u32,
    pub gc: u32,
    points: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolySegmentRequest<'a> {
    pub drawable: u32,
    pub gc: u32,
    segments: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FillPolyRequest<'a> {
    pub shape: PolygonShape,
    pub coordinate_mode: CoordinateMode,
    pub drawable: u32,
    pub gc: u32,
    points: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolyFillRectangleRequest<'a> {
    pub drawable: u32,
    pub gc: u32,
    rectangles: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolyFillArcRequest<'a> {
    pub drawable: u32,
    pub gc: u32,
    arcs: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PutImageRequest<'a> {
    pub format: ImageFormat,
    pub drawable: u32,
    pub gc: u32,
    pub width: u16,
    pub height: u16,
    pub destination_x: i16,
    pub destination_y: i16,
    pub left_pad: u8,
    pub depth: u8,
    pub data: &'a [u8],
}

#[derive(Clone, Copy, Debug)]
pub struct Points<'a> {
    order: ByteOrder,
    remaining: &'a [u8],
}

impl Iterator for Points<'_> {
    type Item = Point;

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining.is_empty() {
            return None;
        }
        let point = point(self.order, self.remaining, 0);
        self.remaining = &self.remaining[4..];
        Some(point)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.remaining.len() / 4;
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for Points<'_> {}

#[derive(Clone, Copy, Debug)]
pub struct ResolvedPoints<'a> {
    order: ByteOrder,
    mode: CoordinateMode,
    previous: Option<ResolvedPoint>,
    remaining: &'a [u8],
    failed: bool,
}

impl Iterator for ResolvedPoints<'_> {
    type Item = Result<ResolvedPoint, CoreRequestDecodeError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.failed || self.remaining.is_empty() {
            return None;
        }
        let wire = point(self.order, self.remaining, 0);
        self.remaining = &self.remaining[4..];
        let resolved = match (self.mode, self.previous) {
            (CoordinateMode::Origin, _) | (CoordinateMode::Previous, None) => ResolvedPoint {
                x: i32::from(wire.x),
                y: i32::from(wire.y),
            },
            (CoordinateMode::Previous, Some(previous)) => {
                let Some(x) = previous.x.checked_add(i32::from(wire.x)) else {
                    self.failed = true;
                    return Some(Err(CoreRequestDecodeError::bad_value(0)));
                };
                let Some(y) = previous.y.checked_add(i32::from(wire.y)) else {
                    self.failed = true;
                    return Some(Err(CoreRequestDecodeError::bad_value(0)));
                };
                ResolvedPoint { x, y }
            }
        };
        self.previous = Some(resolved);
        Some(Ok(resolved))
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Segments<'a> {
    order: ByteOrder,
    remaining: &'a [u8],
}

impl Iterator for Segments<'_> {
    type Item = Segment;

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining.is_empty() {
            return None;
        }
        let segment = Segment {
            first: point(self.order, self.remaining, 0),
            second: point(self.order, self.remaining, 4),
        };
        self.remaining = &self.remaining[8..];
        Some(segment)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.remaining.len() / 8;
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for Segments<'_> {}

#[derive(Clone, Copy, Debug)]
pub struct Rectangles<'a> {
    order: ByteOrder,
    remaining: &'a [u8],
}

impl Iterator for Rectangles<'_> {
    type Item = Rectangle;

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining.is_empty() {
            return None;
        }
        let rectangle = Rectangle {
            x: read_u16(self.order, self.remaining, 0) as i16,
            y: read_u16(self.order, self.remaining, 2) as i16,
            width: read_u16(self.order, self.remaining, 4),
            height: read_u16(self.order, self.remaining, 6),
        };
        self.remaining = &self.remaining[8..];
        Some(rectangle)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.remaining.len() / 8;
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for Rectangles<'_> {}

#[derive(Clone, Copy, Debug)]
pub struct Arcs<'a> {
    order: ByteOrder,
    remaining: &'a [u8],
}

impl Iterator for Arcs<'_> {
    type Item = Arc;

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining.is_empty() {
            return None;
        }
        let arc = Arc {
            x: read_u16(self.order, self.remaining, 0) as i16,
            y: read_u16(self.order, self.remaining, 2) as i16,
            width: read_u16(self.order, self.remaining, 4),
            height: read_u16(self.order, self.remaining, 6),
            angle1: read_u16(self.order, self.remaining, 8) as i16,
            angle2: read_u16(self.order, self.remaining, 10) as i16,
        };
        self.remaining = &self.remaining[12..];
        Some(arc)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.remaining.len() / 12;
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for Arcs<'_> {}

impl<'a> PolyLineRequest<'a> {
    #[must_use]
    pub fn points(self, order: ByteOrder) -> Points<'a> {
        Points {
            order,
            remaining: self.points,
        }
    }

    /// Resolves `Previous` coordinates without allocating. The retained
    /// server performs this overflow check after GC and drawable validation,
    /// so dispatchers must call this only after those resource checks.
    #[must_use]
    pub fn resolved_points(self, order: ByteOrder) -> ResolvedPoints<'a> {
        ResolvedPoints {
            order,
            mode: self.coordinate_mode,
            previous: None,
            remaining: self.points,
            failed: false,
        }
    }
}

impl<'a> PolySegmentRequest<'a> {
    #[must_use]
    pub fn segments(self, order: ByteOrder) -> Segments<'a> {
        Segments {
            order,
            remaining: self.segments,
        }
    }
}

impl<'a> FillPolyRequest<'a> {
    #[must_use]
    pub fn points(self, order: ByteOrder) -> Points<'a> {
        Points {
            order,
            remaining: self.points,
        }
    }
}

impl<'a> PolyFillRectangleRequest<'a> {
    #[must_use]
    pub fn rectangles(self, order: ByteOrder) -> Rectangles<'a> {
        Rectangles {
            order,
            remaining: self.rectangles,
        }
    }
}

impl<'a> PolyFillArcRequest<'a> {
    #[must_use]
    pub fn arcs(self, order: ByteOrder) -> Arcs<'a> {
        Arcs {
            order,
            remaining: self.arcs,
        }
    }
}

pub fn decode_create_pixmap(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CreatePixmapRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    let body = request.body();
    Ok(CreatePixmapRequest {
        depth: request.data,
        xid: read_u32(order, body, 0),
        drawable: read_u32(order, body, 4),
        width: read_u16(order, body, 8),
        height: read_u16(order, body, 10),
    })
}

pub fn decode_pixmap_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    decode_single_id(order, request)
}

pub fn decode_create_gc(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CreateGcRequest<'_>, CoreRequestDecodeError> {
    if request.core_size() < 16 || request.body().len() < 12 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let value_mask = read_u32(order, body, 8);
    let value_bytes = value_bytes(value_mask)?;
    if request.core_size() != 16 + value_bytes || body.len() != 12 + value_bytes {
        return Err(CoreRequestDecodeError::bad_length());
    }
    validate_gc_value_mask(value_mask)?;
    Ok(CreateGcRequest {
        xid: read_u32(order, body, 0),
        drawable: read_u32(order, body, 4),
        value_mask,
        values: &body[12..],
    })
}

pub fn decode_change_gc(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ChangeGcRequest<'_>, CoreRequestDecodeError> {
    if request.core_size() < 12 || request.body().len() < 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let value_mask = read_u32(order, body, 4);
    let value_bytes = value_bytes(value_mask)?;
    if request.core_size() != 12 + value_bytes || body.len() != 8 + value_bytes {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(ChangeGcRequest {
        gc: read_u32(order, body, 0),
        value_mask,
        values: &body[8..],
    })
}

pub fn decode_gc_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    decode_single_id(order, request)
}

pub fn decode_clear_area(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ClearAreaRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    if request.data > 1 {
        return Err(CoreRequestDecodeError::bad_value(u32::from(request.data)));
    }
    let body = request.body();
    Ok(ClearAreaRequest {
        exposures: request.data != 0,
        window: read_u32(order, body, 0),
        x: read_u16(order, body, 4) as i16,
        y: read_u16(order, body, 6) as i16,
        width: read_u16(order, body, 8),
        height: read_u16(order, body, 10),
    })
}

pub fn decode_copy_area(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CopyAreaRequest, CoreRequestDecodeError> {
    exact(request, 28, 24)?;
    let body = request.body();
    Ok(CopyAreaRequest {
        source: read_u32(order, body, 0),
        destination: read_u32(order, body, 4),
        gc: read_u32(order, body, 8),
        source_x: read_u16(order, body, 12) as i16,
        source_y: read_u16(order, body, 14) as i16,
        destination_x: read_u16(order, body, 16) as i16,
        destination_y: read_u16(order, body, 18) as i16,
        width: read_u16(order, body, 20),
        height: read_u16(order, body, 22),
    })
}

pub fn decode_poly_line(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PolyLineRequest<'_>, CoreRequestDecodeError> {
    let points = variable_items(request, 12, 8, 4)?;
    let coordinate_mode = coordinate_mode(request.data)?;
    Ok(PolyLineRequest {
        coordinate_mode,
        drawable: read_u32(order, request.body(), 0),
        gc: read_u32(order, request.body(), 4),
        points,
    })
}

pub fn decode_poly_segment(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PolySegmentRequest<'_>, CoreRequestDecodeError> {
    let segments = variable_items(request, 12, 8, 8)?;
    Ok(PolySegmentRequest {
        drawable: read_u32(order, request.body(), 0),
        gc: read_u32(order, request.body(), 4),
        segments,
    })
}

pub fn decode_fill_poly(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<FillPolyRequest<'_>, CoreRequestDecodeError> {
    let points = variable_items(request, 16, 12, 4)?;
    let body = request.body();
    let shape = polygon_shape(body[8])?;
    let coordinate_mode = coordinate_mode(body[9])?;
    if shape != PolygonShape::Convex || coordinate_mode != CoordinateMode::Origin {
        return Err(error(CoreErrorCode::BadImplementation, 0));
    }
    Ok(FillPolyRequest {
        shape,
        coordinate_mode,
        drawable: read_u32(order, body, 0),
        gc: read_u32(order, body, 4),
        points,
    })
}

pub fn decode_poly_fill_rectangle(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PolyFillRectangleRequest<'_>, CoreRequestDecodeError> {
    let rectangles = variable_items(request, 12, 8, 8)?;
    Ok(PolyFillRectangleRequest {
        drawable: read_u32(order, request.body(), 0),
        gc: read_u32(order, request.body(), 4),
        rectangles,
    })
}

pub fn decode_poly_fill_arc(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PolyFillArcRequest<'_>, CoreRequestDecodeError> {
    let arcs = variable_items(request, 12, 8, 12)?;
    Ok(PolyFillArcRequest {
        drawable: read_u32(order, request.body(), 0),
        gc: read_u32(order, request.body(), 4),
        arcs,
    })
}

/// Applies the retained full-ellipse restriction after GC and drawable
/// validation, preserving the legacy error ordering.
pub fn validate_poly_fill_arc_extents(
    order: ByteOrder,
    request: PolyFillArcRequest<'_>,
) -> Result<PolyFillArcRequest<'_>, CoreRequestDecodeError> {
    for arc in request.arcs(order) {
        if arc.angle2 != 360 * 64 && arc.angle2 != -360 * 64 {
            return Err(error(CoreErrorCode::BadImplementation, 0));
        }
    }
    Ok(request)
}

pub fn decode_put_image(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PutImageRequest<'_>, CoreRequestDecodeError> {
    if request.core_size() < 24 || request.body().len() < 20 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let format = image_format(request.data)?;
    let body = request.body();
    let width = read_u16(order, body, 8);
    let height = read_u16(order, body, 10);
    let left_pad = body[16];
    let depth = body[17];
    let bitmap_payload = format != ImageFormat::ZPixmap || depth == 1;
    let payload_size = if bitmap_payload {
        u64::from(width)
            .checked_add(31)
            .and_then(|bits| bits.checked_div(32))
            .and_then(|words| words.checked_mul(4))
            .and_then(|row| row.checked_mul(u64::from(height)))
    } else {
        u64::from(width)
            .checked_mul(u64::from(height))
            .and_then(|pixels| pixels.checked_mul(4))
    }
    .and_then(|size| usize::try_from(size).ok())
    .ok_or_else(CoreRequestDecodeError::bad_length)?;
    let expected_size = 24_usize
        .checked_add(payload_size)
        .ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != expected_size || body.len() != 20 + payload_size {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let depth_is_invalid = match format {
        ImageFormat::XyBitmap | ImageFormat::XyPixmap => depth != 1,
        ImageFormat::ZPixmap => !matches!(depth, 1 | 24),
    };
    let bad_value = if depth_is_invalid {
        Some(depth)
    } else if left_pad != 0 {
        Some(left_pad)
    } else {
        None
    };
    if let Some(value) = bad_value {
        return Err(CoreRequestDecodeError::bad_value(u32::from(value)));
    }
    Ok(PutImageRequest {
        format,
        drawable: read_u32(order, body, 0),
        gc: read_u32(order, body, 4),
        width,
        height,
        destination_x: read_u16(order, body, 12) as i16,
        destination_y: read_u16(order, body, 14) as i16,
        left_pad,
        depth,
        data: &body[20..],
    })
}

/// Validates the retained GC value-mask subset. `ChangeGC` dispatchers call
/// this after resolving the GC so BadGContext retains precedence.
pub fn validate_gc_value_mask(mask: u32) -> Result<(), CoreRequestDecodeError> {
    if mask & !GC_VALUE_MASK == 0 {
        Ok(())
    } else {
        Err(error(CoreErrorCode::BadImplementation, 0))
    }
}

fn value_bytes(mask: u32) -> Result<usize, CoreRequestDecodeError> {
    usize::try_from(mask.count_ones())
        .ok()
        .and_then(|count| count.checked_mul(4))
        .ok_or_else(CoreRequestDecodeError::bad_length)
}

fn variable_items(
    request: &FramedRequest,
    minimum_core_size: usize,
    fixed_body_size: usize,
    item_size: usize,
) -> Result<&[u8], CoreRequestDecodeError> {
    if request.core_size() < minimum_core_size || request.body().len() < fixed_body_size {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let items = &request.body()[fixed_body_size..];
    if !items.len().is_multiple_of(item_size) {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(items)
}

fn decode_single_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    exact(request, 8, 4)?;
    Ok(read_u32(order, request.body(), 0))
}

fn coordinate_mode(value: u8) -> Result<CoordinateMode, CoreRequestDecodeError> {
    match value {
        0 => Ok(CoordinateMode::Origin),
        1 => Ok(CoordinateMode::Previous),
        value => Err(CoreRequestDecodeError::bad_value(u32::from(value))),
    }
}

fn polygon_shape(value: u8) -> Result<PolygonShape, CoreRequestDecodeError> {
    match value {
        0 => Ok(PolygonShape::Complex),
        1 => Ok(PolygonShape::Nonconvex),
        2 => Ok(PolygonShape::Convex),
        value => Err(CoreRequestDecodeError::bad_value(u32::from(value))),
    }
}

fn image_format(value: u8) -> Result<ImageFormat, CoreRequestDecodeError> {
    match value {
        0 => Ok(ImageFormat::XyBitmap),
        1 => Ok(ImageFormat::XyPixmap),
        2 => Ok(ImageFormat::ZPixmap),
        value => Err(CoreRequestDecodeError::bad_value(u32::from(value))),
    }
}

fn exact(
    request: &FramedRequest,
    core_size: usize,
    body_size: usize,
) -> Result<(), CoreRequestDecodeError> {
    if request.core_size() == core_size && request.body().len() == body_size {
        Ok(())
    } else {
        Err(CoreRequestDecodeError::bad_length())
    }
}

fn error(code: CoreErrorCode, bad_value: u32) -> CoreRequestDecodeError {
    CoreRequestDecodeError { code, bad_value }
}

fn point(order: ByteOrder, bytes: &[u8], offset: usize) -> Point {
    Point {
        x: read_u16(order, bytes, offset) as i16,
        y: read_u16(order, bytes, offset + 2) as i16,
    }
}

fn read_u16(order: ByteOrder, bytes: &[u8], offset: usize) -> u16 {
    order.read_u16([bytes[offset], bytes[offset + 1]])
}

fn read_u32(order: ByteOrder, bytes: &[u8], offset: usize) -> u32 {
    order.read_u32([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn framed(order: ByteOrder, opcode: u8, data: u8, body: &[u8]) -> FramedRequest {
        let mut bytes = vec![opcode, data, 0, 0];
        bytes.extend_from_slice(body);
        let units = u16::try_from(bytes.len() / 4).unwrap();
        bytes[2..4].copy_from_slice(&order.u16_bytes(units));
        FramedRequest {
            opcode,
            data,
            length_units: u32::from(units),
            header_size: 4,
            bytes,
        }
    }

    fn values(order: ByteOrder, values: &[u32]) -> Vec<u8> {
        values
            .iter()
            .flat_map(|value| order.u32_bytes(*value))
            .collect()
    }

    #[test]
    fn pixmap_and_gc_shapes_match_both_legacy_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut pixmap = Vec::new();
            pixmap.extend_from_slice(&order.u32_bytes(0x0040_0001));
            pixmap.extend_from_slice(&order.u32_bytes(1));
            pixmap.extend_from_slice(&order.u16_bytes(640));
            pixmap.extend_from_slice(&order.u16_bytes(480));
            assert_eq!(
                decode_create_pixmap(order, &framed(order, 53, 24, &pixmap)).unwrap(),
                CreatePixmapRequest {
                    depth: 24,
                    xid: 0x0040_0001,
                    drawable: 1,
                    width: 640,
                    height: 480,
                }
            );

            let mask = (1 << 0) | (1 << 2) | (1 << 16);
            let mut gc = Vec::new();
            gc.extend_from_slice(&order.u32_bytes(0x0040_0002));
            gc.extend_from_slice(&order.u32_bytes(0x0040_0001));
            gc.extend_from_slice(&order.u32_bytes(mask));
            gc.extend_from_slice(&values(order, &[3, 0x112233, 1]));
            let frame = framed(order, 55, 0, &gc);
            let decoded = decode_create_gc(order, &frame).unwrap();
            assert_eq!(decoded.xid, 0x0040_0002);
            assert_eq!(decoded.drawable, 0x0040_0001);
            assert_eq!(
                decoded.values(order).collect::<Vec<_>>(),
                vec![
                    GcValue { bit: 0, value: 3 },
                    GcValue {
                        bit: 2,
                        value: 0x112233,
                    },
                    GcValue { bit: 16, value: 1 },
                ]
            );
        }
    }

    #[test]
    fn gc_mask_and_value_count_preserve_legacy_validation_order() {
        let order = ByteOrder::LittleEndian;
        let mut unsupported = Vec::new();
        unsupported.extend_from_slice(&order.u32_bytes(1));
        unsupported.extend_from_slice(&order.u32_bytes(2));
        unsupported.extend_from_slice(&order.u32_bytes(1 << 9));
        unsupported.extend_from_slice(&order.u32_bytes(0));
        assert_eq!(
            decode_create_gc(order, &framed(order, 55, 0, &unsupported)),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );

        let malformed = framed(order, 55, 0, &unsupported[..12]);
        assert_eq!(
            decode_create_gc(order, &malformed),
            Err(CoreRequestDecodeError::bad_length())
        );

        let mut change = Vec::new();
        change.extend_from_slice(&order.u32_bytes(0xdead_beef));
        change.extend_from_slice(&order.u32_bytes(1 << 9));
        change.extend_from_slice(&order.u32_bytes(0));
        let frame = framed(order, 56, 0, &change);
        let decoded = decode_change_gc(order, &frame).unwrap();
        assert_eq!(
            validate_gc_value_mask(decoded.value_mask),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );
    }

    #[test]
    fn clear_and_copy_preserve_signed_coordinates_and_exposure_flag() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut clear = Vec::new();
            clear.extend_from_slice(&order.u32_bytes(7));
            for value in [(-2_i16) as u16, 3, 4, 5] {
                clear.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_clear_area(order, &framed(order, 61, 1, &clear)).unwrap(),
                ClearAreaRequest {
                    exposures: true,
                    window: 7,
                    x: -2,
                    y: 3,
                    width: 4,
                    height: 5,
                }
            );

            let mut copy = Vec::new();
            for value in [1_u32, 2, 3] {
                copy.extend_from_slice(&order.u32_bytes(value));
            }
            for value in [(-1_i16) as u16, 2, 3, (-4_i16) as u16, 5, 6] {
                copy.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_copy_area(order, &framed(order, 62, 0, &copy)).unwrap(),
                CopyAreaRequest {
                    source: 1,
                    destination: 2,
                    gc: 3,
                    source_x: -1,
                    source_y: 2,
                    destination_x: 3,
                    destination_y: -4,
                    width: 5,
                    height: 6,
                }
            );
        }
    }

    #[test]
    fn primitive_lists_are_borrowed_and_decoded_lazily() {
        let order = ByteOrder::BigEndian;
        let mut line = values(order, &[1, 2]);
        for value in [(-1_i16) as u16, 2, 3, (-4_i16) as u16] {
            line.extend_from_slice(&order.u16_bytes(value));
        }
        let frame = framed(order, 65, CoordinateMode::Previous as u8, &line);
        let decoded = decode_poly_line(order, &frame).unwrap();
        assert_eq!(decoded.coordinate_mode, CoordinateMode::Previous);
        assert_eq!(
            decoded.points(order).collect::<Vec<_>>(),
            vec![Point { x: -1, y: 2 }, Point { x: 3, y: -4 }]
        );
        assert_eq!(
            decoded
                .resolved_points(order)
                .collect::<Result<Vec<_>, _>>(),
            Ok(vec![
                ResolvedPoint { x: -1, y: 2 },
                ResolvedPoint { x: 2, y: -2 },
            ])
        );

        let mut rectangles = values(order, &[1, 2]);
        for value in [(-5_i16) as u16, 6, 7, 8] {
            rectangles.extend_from_slice(&order.u16_bytes(value));
        }
        let frame = framed(order, 70, 0, &rectangles);
        assert_eq!(
            decode_poly_fill_rectangle(order, &frame)
                .unwrap()
                .rectangles(order)
                .collect::<Vec<_>>(),
            vec![Rectangle {
                x: -5,
                y: 6,
                width: 7,
                height: 8,
            }]
        );
    }

    #[test]
    fn polygon_and_arc_static_profile_errors_match_the_legacy_handler() {
        let order = ByteOrder::LittleEndian;
        let mut polygon = values(order, &[1, 2]);
        polygon.extend_from_slice(&[0, 0, 0, 0]);
        assert_eq!(
            decode_fill_poly(order, &framed(order, 69, 0, &polygon)),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );
        polygon[8] = 3;
        assert_eq!(
            decode_fill_poly(order, &framed(order, 69, 0, &polygon)),
            Err(CoreRequestDecodeError::bad_value(3))
        );

        let mut arcs = values(order, &[1, 2]);
        for value in [0_u16, 0, 10, 10, 0, 90 * 64] {
            arcs.extend_from_slice(&order.u16_bytes(value));
        }
        let frame = framed(order, 71, 0, &arcs);
        let decoded = decode_poly_fill_arc(order, &frame).unwrap();
        assert_eq!(
            validate_poly_fill_arc_extents(order, decoded),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );
    }

    #[test]
    fn previous_coordinate_overflow_is_deferred_until_after_resource_validation() {
        let order = ByteOrder::LittleEndian;
        let mut points = Vec::new();
        for _ in 0..65_540 {
            points.extend_from_slice(&order.u16_bytes(i16::MAX as u16));
            points.extend_from_slice(&order.u16_bytes(0));
        }
        let request = PolyLineRequest {
            coordinate_mode: CoordinateMode::Previous,
            drawable: 1,
            gc: 2,
            points: &points,
        };
        let mut resolved = request.resolved_points(order);
        assert!(resolved.any(|point| point == Err(CoreRequestDecodeError::bad_value(0))));
        assert_eq!(resolved.next(), None);
    }

    #[test]
    fn put_image_checks_shape_before_exposing_borrowed_payload() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut image = values(order, &[1, 2]);
            for value in [2_u16, 1, (-3_i16) as u16, 4] {
                image.extend_from_slice(&order.u16_bytes(value));
            }
            image.extend_from_slice(&[0, 24, 0, 0]);
            image.extend_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            let frame = framed(order, 72, ImageFormat::ZPixmap as u8, &image);
            let decoded = decode_put_image(order, &frame).unwrap();
            assert_eq!(decoded.destination_x, -3);
            assert_eq!(decoded.destination_y, 4);
            assert_eq!(decoded.data, &[1, 2, 3, 4, 5, 6, 7, 8]);

            let mut bad_depth = image.clone();
            bad_depth[17] = 8;
            assert_eq!(
                decode_put_image(
                    order,
                    &framed(order, 72, ImageFormat::ZPixmap as u8, &bad_depth)
                ),
                Err(CoreRequestDecodeError::bad_value(8))
            );
            let mut short = image.clone();
            short.truncate(short.len() - 4);
            assert_eq!(
                decode_put_image(order, &framed(order, 72, 2, &short)),
                Err(CoreRequestDecodeError::bad_length())
            );
        }
    }

    #[test]
    fn all_variable_shapes_reject_misaligned_payloads_without_allocation() {
        let order = ByteOrder::LittleEndian;
        for (opcode, fixed) in [(65_u8, 8_usize), (66, 8), (69, 12), (70, 8), (71, 8)] {
            let mut body = vec![0; fixed + 4];
            body.push(0);
            let frame = framed(order, opcode, 0, &body);
            let error = match opcode {
                65 => decode_poly_line(order, &frame).unwrap_err(),
                66 => decode_poly_segment(order, &frame).unwrap_err(),
                69 => decode_fill_poly(order, &frame).unwrap_err(),
                70 => decode_poly_fill_rectangle(order, &frame).unwrap_err(),
                71 => decode_poly_fill_arc(order, &frame).unwrap_err(),
                _ => unreachable!(),
            };
            assert_eq!(error, CoreRequestDecodeError::bad_length());
        }
    }
}
