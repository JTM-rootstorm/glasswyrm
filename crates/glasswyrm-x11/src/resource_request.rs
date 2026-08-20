use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CreateColormapRequest {
    pub xid: u32,
    pub window: u32,
    pub visual: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Color16 {
    pub red: u16,
    pub green: u16,
    pub blue: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AllocColorRequest {
    pub colormap: u32,
    pub color: Color16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NamedColorRequest<'a> {
    pub colormap: u32,
    pub name: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FreeColorsRequest<'a> {
    pub colormap: u32,
    pub plane_mask: u32,
    pub pixels: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryColorsRequest<'a> {
    pub colormap: u32,
    pub pixels: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CreateCursorRequest {
    pub xid: u32,
    pub source: u32,
    pub mask: u32,
    pub foreground: Color16,
    pub background: Color16,
    pub hotspot_x: u16,
    pub hotspot_y: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CreateGlyphCursorRequest {
    pub xid: u32,
    pub source_font: u32,
    pub mask_font: u32,
    pub source_character: u16,
    pub mask_character: u16,
    pub foreground: Color16,
    pub background: Color16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RecolorCursorRequest {
    pub cursor: u32,
    pub foreground: Color16,
    pub background: Color16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryBestSizeRequest {
    pub class: u8,
    pub drawable: u32,
    pub width: u16,
    pub height: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum BestSizeClass {
    Cursor = 0,
    Tile = 1,
    Stipple = 2,
}

pub fn decode_create_colormap(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CreateColormapRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    if request.data != 0 {
        return Err(error(CoreErrorCode::BadValue, u32::from(request.data)));
    }
    let body = request.body();
    Ok(CreateColormapRequest {
        xid: read_u32(order, body, 0),
        window: read_u32(order, body, 4),
        visual: read_u32(order, body, 8),
    })
}

pub fn decode_colormap_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    exact(request, 8, 4)?;
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_alloc_color(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<AllocColorRequest, CoreRequestDecodeError> {
    exact(request, 16, 12)?;
    let body = request.body();
    Ok(AllocColorRequest {
        colormap: read_u32(order, body, 0),
        color: Color16 {
            red: read_u16(order, body, 4),
            green: read_u16(order, body, 6),
            blue: read_u16(order, body, 8),
        },
    })
}

pub fn decode_named_color(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<NamedColorRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 12 || request.body().len() < 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let colormap = read_u32(order, body, 0);
    let name_length = usize::from(read_u16(order, body, 4));
    let padded = align_four(name_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 12 + padded || body.len() != 8 + padded {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(NamedColorRequest {
        colormap,
        name: &body[8..8 + name_length],
    })
}

pub fn parse_color_name(name: &[u8]) -> Result<Color16, CoreRequestDecodeError> {
    if let Some(hex) = name.strip_prefix(b"#") {
        if !matches!(hex.len(), 3 | 6 | 12) {
            return Err(error(CoreErrorCode::BadName, 0));
        }
        let width = hex.len() / 3;
        let mut values = [0_u16; 3];
        for (component, output) in values.iter_mut().enumerate() {
            let mut value = 0_u16;
            for offset in 0..width {
                let nibble = parse_nibble(hex[component * width + offset])
                    .ok_or_else(|| error(CoreErrorCode::BadName, 0))?;
                value = (value << 4) | u16::from(nibble);
            }
            *output = match width {
                1 => value * 0x1111,
                2 => value * 0x0101,
                4 => value,
                _ => unreachable!("supported color widths are one, two, or four nibbles"),
            };
        }
        return Ok(Color16 {
            red: values[0],
            green: values[1],
            blue: values[2],
        });
    }
    let color = if name.eq_ignore_ascii_case(b"black") {
        Color16 {
            red: 0,
            green: 0,
            blue: 0,
        }
    } else if name.eq_ignore_ascii_case(b"white") {
        Color16 {
            red: 0xffff,
            green: 0xffff,
            blue: 0xffff,
        }
    } else if name.eq_ignore_ascii_case(b"red") {
        Color16 {
            red: 0xffff,
            green: 0,
            blue: 0,
        }
    } else if name.eq_ignore_ascii_case(b"green") {
        Color16 {
            red: 0,
            green: 0xffff,
            blue: 0,
        }
    } else if name.eq_ignore_ascii_case(b"blue") {
        Color16 {
            red: 0,
            green: 0,
            blue: 0xffff,
        }
    } else if name.eq_ignore_ascii_case(b"yellow") {
        Color16 {
            red: 0xffff,
            green: 0xffff,
            blue: 0,
        }
    } else if name.eq_ignore_ascii_case(b"cyan") {
        Color16 {
            red: 0,
            green: 0xffff,
            blue: 0xffff,
        }
    } else if name.eq_ignore_ascii_case(b"magenta") {
        Color16 {
            red: 0xffff,
            green: 0,
            blue: 0xffff,
        }
    } else if name.eq_ignore_ascii_case(b"gray") || name.eq_ignore_ascii_case(b"grey") {
        Color16 {
            red: 0x8080,
            green: 0x8080,
            blue: 0x8080,
        }
    } else if name.eq_ignore_ascii_case(b"light gray") || name.eq_ignore_ascii_case(b"light grey") {
        Color16 {
            red: 0xd3d3,
            green: 0xd3d3,
            blue: 0xd3d3,
        }
    } else if name.eq_ignore_ascii_case(b"dark gray") || name.eq_ignore_ascii_case(b"dark grey") {
        Color16 {
            red: 0xa9a9,
            green: 0xa9a9,
            blue: 0xa9a9,
        }
    } else {
        return Err(error(CoreErrorCode::BadName, 0));
    };
    Ok(color)
}

pub fn decode_free_colors(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<FreeColorsRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 12 || request.bytes.len() & 3 != 0 || request.body().len() < 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    Ok(FreeColorsRequest {
        colormap: read_u32(order, body, 0),
        plane_mask: read_u32(order, body, 4),
        pixels: &body[8..],
    })
}

pub fn decode_query_colors(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<QueryColorsRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 8 || request.bytes.len() & 3 != 0 || request.body().len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let pixels = &request.body()[4..];
    let count = pixels.len() / 4;
    if count > usize::from(u16::MAX) {
        return Err(error(CoreErrorCode::BadAlloc, 0));
    }
    Ok(QueryColorsRequest {
        colormap: read_u32(order, request.body(), 0),
        pixels,
    })
}

pub fn decode_create_cursor(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CreateCursorRequest, CoreRequestDecodeError> {
    exact(request, 32, 28)?;
    let body = request.body();
    Ok(CreateCursorRequest {
        xid: read_u32(order, body, 0),
        source: read_u32(order, body, 4),
        mask: read_u32(order, body, 8),
        foreground: read_color(order, body, 12),
        background: read_color(order, body, 18),
        hotspot_x: read_u16(order, body, 24),
        hotspot_y: read_u16(order, body, 26),
    })
}

pub fn decode_create_glyph_cursor(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<CreateGlyphCursorRequest, CoreRequestDecodeError> {
    exact(request, 32, 28)?;
    let body = request.body();
    Ok(CreateGlyphCursorRequest {
        xid: read_u32(order, body, 0),
        source_font: read_u32(order, body, 4),
        mask_font: read_u32(order, body, 8),
        source_character: read_u16(order, body, 12),
        mask_character: read_u16(order, body, 14),
        foreground: read_color(order, body, 16),
        background: read_color(order, body, 22),
    })
}

pub fn decode_cursor_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    decode_colormap_id(order, request)
}

pub fn decode_recolor_cursor(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<RecolorCursorRequest, CoreRequestDecodeError> {
    exact(request, 20, 16)?;
    let body = request.body();
    Ok(RecolorCursorRequest {
        cursor: read_u32(order, body, 0),
        foreground: read_color(order, body, 4),
        background: read_color(order, body, 10),
    })
}

pub fn decode_query_best_size(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<QueryBestSizeRequest, CoreRequestDecodeError> {
    exact(request, 12, 8)?;
    let body = request.body();
    Ok(QueryBestSizeRequest {
        class: request.data,
        drawable: read_u32(order, body, 0),
        width: read_u16(order, body, 4),
        height: read_u16(order, body, 6),
    })
}

pub fn validate_query_best_size_class(
    request: QueryBestSizeRequest,
) -> Result<BestSizeClass, CoreRequestDecodeError> {
    match request.class {
        0 => Ok(BestSizeClass::Cursor),
        1 => Ok(BestSizeClass::Tile),
        2 => Ok(BestSizeClass::Stipple),
        value => Err(error(CoreErrorCode::BadValue, u32::from(value))),
    }
}

fn read_color(order: ByteOrder, bytes: &[u8], offset: usize) -> Color16 {
    Color16 {
        red: read_u16(order, bytes, offset),
        green: read_u16(order, bytes, offset + 2),
        blue: read_u16(order, bytes, offset + 4),
    }
}

fn parse_nibble(value: u8) -> Option<u8> {
    match value {
        b'0'..=b'9' => Some(value - b'0'),
        b'a'..=b'f' => Some(value - b'a' + 10),
        b'A'..=b'F' => Some(value - b'A' + 10),
        _ => None,
    }
}

fn align_four(value: usize) -> Option<usize> {
    value.checked_add(3).map(|size| size & !3)
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

    #[test]
    fn colormap_and_alloc_color_fields_match_both_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = Vec::new();
            for value in [1_u32, 2, 3] {
                body.extend_from_slice(&order.u32_bytes(value));
            }
            assert_eq!(
                decode_create_colormap(order, &framed(order, 78, 0, &body)).unwrap(),
                CreateColormapRequest {
                    xid: 1,
                    window: 2,
                    visual: 3
                }
            );
            let mut color = Vec::new();
            color.extend_from_slice(&order.u32_bytes(4));
            for value in [0x1111_u16, 0x2222, 0x3333, 0xabcd] {
                color.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_alloc_color(order, &framed(order, 84, 0, &color)).unwrap(),
                AllocColorRequest {
                    colormap: 4,
                    color: Color16 {
                        red: 0x1111,
                        green: 0x2222,
                        blue: 0x3333
                    }
                }
            );
        }
    }

    #[test]
    fn named_color_shape_precedes_name_parsing() {
        let order = ByteOrder::LittleEndian;
        let name = b"No Such Color";
        let padded = (name.len() + 3) & !3;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(7));
        body.extend_from_slice(&order.u16_bytes(name.len() as u16));
        body.extend_from_slice(&[0xaa, 0xbb]);
        body.extend_from_slice(name);
        body.resize(8 + padded, 0xcc);
        let framed_request = framed(order, 85, 0, &body);
        let wire = decode_named_color(order, &framed_request).unwrap();
        assert_eq!(wire.colormap, 7);
        assert_eq!(wire.name, name);
        assert_eq!(
            parse_color_name(wire.name),
            Err(error(CoreErrorCode::BadName, 0))
        );
    }

    #[test]
    fn color_name_subset_matches_legacy_parser() {
        assert_eq!(
            parse_color_name(b"RED").unwrap(),
            Color16 {
                red: 0xffff,
                green: 0,
                blue: 0
            }
        );
        assert_eq!(
            parse_color_name(b"#abc").unwrap(),
            Color16 {
                red: 0xaaaa,
                green: 0xbbbb,
                blue: 0xcccc
            }
        );
        assert_eq!(
            parse_color_name(b"#102030").unwrap(),
            Color16 {
                red: 0x1010,
                green: 0x2020,
                blue: 0x3030
            }
        );
        assert_eq!(
            parse_color_name(b"light grey").unwrap(),
            Color16 {
                red: 0xd3d3,
                green: 0xd3d3,
                blue: 0xd3d3
            }
        );
        assert!(parse_color_name(b"#12").is_err());
    }

    #[test]
    fn free_and_query_color_lists_remain_borrowed_and_bounded() {
        let order = ByteOrder::BigEndian;
        let mut free = Vec::new();
        free.extend_from_slice(&order.u32_bytes(1));
        free.extend_from_slice(&order.u32_bytes(2));
        free.extend_from_slice(&order.u32_bytes(3));
        let framed_free = framed(order, 88, 0, &free);
        let decoded = decode_free_colors(order, &framed_free).unwrap();
        assert_eq!(decoded.colormap, 1);
        assert_eq!(decoded.plane_mask, 2);
        assert_eq!(decoded.pixels, &order.u32_bytes(3));

        let mut query = Vec::new();
        query.extend_from_slice(&order.u32_bytes(4));
        query.extend_from_slice(&order.u32_bytes(5));
        let framed_query = framed(order, 91, 0, &query);
        let decoded = decode_query_colors(order, &framed_query).unwrap();
        assert_eq!(decoded.colormap, 4);
        assert_eq!(decoded.pixels, &order.u32_bytes(5));
    }

    #[test]
    fn cursor_requests_preserve_all_wire_fields() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut cursor = Vec::new();
            for value in [1_u32, 2, 3] {
                cursor.extend_from_slice(&order.u32_bytes(value));
            }
            for value in [10_u16, 11, 12, 20, 21, 22, 4, 5] {
                cursor.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_create_cursor(order, &framed(order, 93, 0, &cursor)).unwrap(),
                CreateCursorRequest {
                    xid: 1,
                    source: 2,
                    mask: 3,
                    foreground: Color16 {
                        red: 10,
                        green: 11,
                        blue: 12
                    },
                    background: Color16 {
                        red: 20,
                        green: 21,
                        blue: 22
                    },
                    hotspot_x: 4,
                    hotspot_y: 5
                }
            );

            let mut glyph = Vec::new();
            for value in [1_u32, 2, 3] {
                glyph.extend_from_slice(&order.u32_bytes(value));
            }
            for value in [4_u16, 5, 10, 11, 12, 20, 21, 22] {
                glyph.extend_from_slice(&order.u16_bytes(value));
            }
            assert_eq!(
                decode_create_glyph_cursor(order, &framed(order, 94, 0, &glyph)).unwrap(),
                CreateGlyphCursorRequest {
                    xid: 1,
                    source_font: 2,
                    mask_font: 3,
                    source_character: 4,
                    mask_character: 5,
                    foreground: Color16 {
                        red: 10,
                        green: 11,
                        blue: 12
                    },
                    background: Color16 {
                        red: 20,
                        green: 21,
                        blue: 22
                    }
                }
            );
        }
    }

    #[test]
    fn query_best_size_class_is_deliberately_post_drawable_validation() {
        let order = ByteOrder::LittleEndian;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(0x1234));
        body.extend_from_slice(&order.u16_bytes(10));
        body.extend_from_slice(&order.u16_bytes(20));
        let request = decode_query_best_size(order, &framed(order, 97, 7, &body)).unwrap();
        assert_eq!(request.drawable, 0x1234);
        assert_eq!(
            validate_query_best_size_class(request),
            Err(error(CoreErrorCode::BadValue, 7))
        );
    }
}
