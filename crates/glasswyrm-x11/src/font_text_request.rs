use crate::{ByteOrder, CoreErrorCode, CoreRequestDecodeError, FramedRequest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum FontIdentity {
    Fixed,
    Cursor,
    Nil2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OpenFontRequest<'a> {
    pub xid: u32,
    pub name: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ListFontsRequest<'a> {
    pub maximum_names: u16,
    pub pattern: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryTextExtentsRequest<'a> {
    pub fontable: u32,
    pub odd_length: bool,
    text_and_padding: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryTextExtentsText<'a> {
    pub characters: usize,
    pub text: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ImageText8Request<'a> {
    pub drawable: u32,
    pub gc: u32,
    pub x: i16,
    pub y: i16,
    pub text: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolyText8Request<'a> {
    pub drawable: u32,
    pub gc: u32,
    pub x: i16,
    pub y: i16,
    items: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PolyText8Item<'a> {
    Font(u32),
    Text { delta: i8, text: &'a [u8] },
}

#[derive(Clone, Copy, Debug)]
pub struct PolyText8Items<'a> {
    order: ByteOrder,
    remaining: &'a [u8],
    failed: bool,
}

impl<'a> Iterator for PolyText8Items<'a> {
    type Item = Result<PolyText8Item<'a>, CoreRequestDecodeError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.failed {
            return None;
        }
        loop {
            let (&length, rest) = self.remaining.split_first()?;
            self.remaining = rest;
            if length == 0 {
                continue;
            }
            if length == 255 {
                if self.remaining.len() < 4 {
                    self.failed = true;
                    return Some(Err(CoreRequestDecodeError::bad_length()));
                }
                let font = read_u32(self.order, self.remaining, 0);
                self.remaining = &self.remaining[4..];
                return Some(Ok(PolyText8Item::Font(font)));
            }
            if self.remaining.len() < 1 + usize::from(length) {
                self.failed = true;
                return Some(Err(CoreRequestDecodeError::bad_length()));
            }
            let delta = self.remaining[0] as i8;
            let text = &self.remaining[1..1 + usize::from(length)];
            self.remaining = &self.remaining[1 + usize::from(length)..];
            return Some(Ok(PolyText8Item::Text { delta, text }));
        }
    }
}

impl<'a> PolyText8Request<'a> {
    #[must_use]
    pub fn items(self, order: ByteOrder) -> PolyText8Items<'a> {
        PolyText8Items {
            order,
            remaining: self.items,
            failed: false,
        }
    }
}

pub fn decode_open_font(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<OpenFontRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 12 || request.body().len() < 8 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let xid = read_u32(order, body, 0);
    let name_length = usize::from(read_u16(order, body, 4));
    let padded = align_four(name_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 12 + padded || body.len() != 8 + padded {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(OpenFontRequest {
        xid,
        name: &body[8..8 + name_length],
    })
}

pub fn font_identity(name: &[u8]) -> Result<FontIdentity, CoreRequestDecodeError> {
    if name.len() > 255 {
        return Err(error(CoreErrorCode::BadName, 0));
    }
    if name.eq_ignore_ascii_case(b"cursor") {
        Ok(FontIdentity::Cursor)
    } else if name.eq_ignore_ascii_case(b"nil2") {
        Ok(FontIdentity::Nil2)
    } else if matches_fixed_font(name) {
        Ok(FontIdentity::Fixed)
    } else {
        Err(error(CoreErrorCode::BadName, 0))
    }
}

#[must_use]
pub fn matches_fixed_font(name: &[u8]) -> bool {
    if name.len() > 255 {
        return false;
    }
    if name.eq_ignore_ascii_case(b"fixed") || name.eq_ignore_ascii_case(b"6x13") {
        return true;
    }
    const PREFIX: &[u8] = b"-misc-fixed-";
    name.len() > PREFIX.len()
        && name[..PREFIX.len()].eq_ignore_ascii_case(PREFIX)
        && name.last() == Some(&b'*')
}

pub fn decode_fontable_id(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    if request.core_size() != 8 || request.body().len() != 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_query_text_extents(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<QueryTextExtentsRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 8 || request.bytes.len() & 3 != 0 || request.body().len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(QueryTextExtentsRequest {
        fontable: read_u32(order, request.body(), 0),
        odd_length: request.data != 0,
        text_and_padding: &request.body()[4..],
    })
}

pub fn validate_query_text_extents_text(
    request_data: u8,
    request: QueryTextExtentsRequest<'_>,
) -> Result<QueryTextExtentsText<'_>, CoreRequestDecodeError> {
    let bytes = request.text_and_padding;
    if bytes.len() & 1 != 0 || request_data > 1 || (request_data != 0 && bytes.len() < 2) {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let characters = bytes.len() / 2 - usize::from(request_data != 0);
    let text_size = characters * 2;
    for pair in bytes[..text_size].chunks_exact(2) {
        if pair[0] != 0 {
            return Err(error(CoreErrorCode::BadImplementation, 0));
        }
    }
    Ok(QueryTextExtentsText {
        characters,
        text: &bytes[..text_size],
    })
}

pub fn decode_list_fonts(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ListFontsRequest<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 8 || request.body().len() < 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    let maximum_names = read_u16(order, body, 0);
    let pattern_length = usize::from(read_u16(order, body, 2));
    let padded = align_four(pattern_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 8 + padded || body.len() != 4 + padded {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(ListFontsRequest {
        maximum_names,
        pattern: &body[4..4 + pattern_length],
    })
}

pub fn decode_image_text8(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ImageText8Request<'_>, CoreRequestDecodeError> {
    let text_length = usize::from(request.data);
    let padded = align_four(text_length).ok_or_else(CoreRequestDecodeError::bad_length)?;
    if request.core_size() != 16 + padded || request.body().len() != 12 + padded {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    Ok(ImageText8Request {
        drawable: read_u32(order, body, 0),
        gc: read_u32(order, body, 4),
        x: read_u16(order, body, 8) as i16,
        y: read_u16(order, body, 10) as i16,
        text: &body[12..12 + text_length],
    })
}

pub fn decode_poly_text8(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<PolyText8Request<'_>, CoreRequestDecodeError> {
    if request.bytes.len() < 16 || request.bytes.len() & 3 != 0 || request.body().len() < 12 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    Ok(PolyText8Request {
        drawable: read_u32(order, body, 0),
        gc: read_u32(order, body, 4),
        x: read_u16(order, body, 8) as i16,
        y: read_u16(order, body, 10) as i16,
        items: &body[12..],
    })
}

fn align_four(value: usize) -> Option<usize> {
    value.checked_add(3).map(|size| size & !3)
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
    fn font_names_and_patterns_match_the_legacy_subset() {
        assert_eq!(font_identity(b"FIXED"), Ok(FontIdentity::Fixed));
        assert_eq!(font_identity(b"cursor"), Ok(FontIdentity::Cursor));
        assert_eq!(font_identity(b"NIL2"), Ok(FontIdentity::Nil2));
        assert!(matches_fixed_font(b"-misc-fixed-medium-r-normal--13-*"));
        assert!(!matches_fixed_font(b"variable"));
        assert_eq!(
            font_identity(b"variable"),
            Err(error(CoreErrorCode::BadName, 0))
        );
    }

    #[test]
    fn open_and_list_font_requests_borrow_names_for_both_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let name = b"fixed";
            let mut open = Vec::new();
            open.extend_from_slice(&order.u32_bytes(7));
            open.extend_from_slice(&order.u16_bytes(name.len() as u16));
            open.extend_from_slice(&[0xaa, 0xbb]);
            open.extend_from_slice(name);
            open.resize(8 + ((name.len() + 3) & !3), 0xcc);
            let open_frame = framed(order, 45, 0, &open);
            assert_eq!(
                decode_open_font(order, &open_frame).unwrap(),
                OpenFontRequest { xid: 7, name }
            );

            let mut list = Vec::new();
            list.extend_from_slice(&order.u16_bytes(3));
            list.extend_from_slice(&order.u16_bytes(name.len() as u16));
            list.extend_from_slice(name);
            list.resize(4 + ((name.len() + 3) & !3), 0xdd);
            let list_frame = framed(order, 49, 0, &list);
            assert_eq!(
                decode_list_fonts(order, &list_frame).unwrap(),
                ListFontsRequest {
                    maximum_names: 3,
                    pattern: name
                }
            );
        }
    }

    #[test]
    fn query_text_extents_keeps_font_validation_before_text_validation() {
        let order = ByteOrder::LittleEndian;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(0x1234));
        body.extend_from_slice(&[1, b'A', 0, 0]);
        let frame = framed(order, 48, 0, &body);
        let shape = decode_query_text_extents(order, &frame).unwrap();
        assert_eq!(shape.fontable, 0x1234);
        assert_eq!(
            validate_query_text_extents_text(frame.data, shape),
            Err(error(CoreErrorCode::BadImplementation, 0))
        );

        let mut valid_body = Vec::new();
        valid_body.extend_from_slice(&order.u32_bytes(9));
        valid_body.extend_from_slice(&[0, b'A', 0, b'B']);
        let valid_frame = framed(order, 48, 0, &valid_body);
        let shape = decode_query_text_extents(order, &valid_frame).unwrap();
        let text = validate_query_text_extents_text(valid_frame.data, shape).unwrap();
        assert_eq!(text.characters, 2);
        assert_eq!(text.text, &[0, b'A', 0, b'B']);
    }

    #[test]
    fn image_text8_preserves_padding_and_signed_coordinates() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut body = Vec::new();
            body.extend_from_slice(&order.u32_bytes(1));
            body.extend_from_slice(&order.u32_bytes(2));
            body.extend_from_slice(&order.u16_bytes((-3_i16) as u16));
            body.extend_from_slice(&order.u16_bytes(4));
            body.extend_from_slice(b"abc");
            body.push(0xee);
            let frame = framed(order, 76, 3, &body);
            assert_eq!(
                decode_image_text8(order, &frame).unwrap(),
                ImageText8Request {
                    drawable: 1,
                    gc: 2,
                    x: -3,
                    y: 4,
                    text: b"abc"
                }
            );
        }
    }

    #[test]
    fn poly_text8_items_are_borrowed_and_report_truncation_lazily() {
        let order = ByteOrder::BigEndian;
        let mut body = Vec::new();
        body.extend_from_slice(&order.u32_bytes(1));
        body.extend_from_slice(&order.u32_bytes(2));
        body.extend_from_slice(&order.u16_bytes(3));
        body.extend_from_slice(&order.u16_bytes(4));
        body.extend_from_slice(&[0, 2, 0xff, b'A', b'B', 255]);
        body.extend_from_slice(&order.u32_bytes(9));
        body.extend_from_slice(&[1, 1, b'Z', 0, 0, 0]);
        let frame = framed(order, 74, 0, &body);
        let request = decode_poly_text8(order, &frame).unwrap();
        let items = request.items(order).collect::<Result<Vec<_>, _>>().unwrap();
        assert_eq!(
            items,
            vec![
                PolyText8Item::Text {
                    delta: -1,
                    text: b"AB"
                },
                PolyText8Item::Font(9),
                PolyText8Item::Text {
                    delta: 1,
                    text: b"Z"
                },
            ]
        );

        let mut broken_body = body[..12].to_vec();
        broken_body.extend_from_slice(&[3, 0, b'A', 0]);
        let broken = framed(order, 74, 0, &broken_body);
        let request = decode_poly_text8(order, &broken).unwrap();
        let mut items = request.items(order);
        assert_eq!(
            items.next(),
            Some(Err(CoreRequestDecodeError::bad_length()))
        );
    }
}
