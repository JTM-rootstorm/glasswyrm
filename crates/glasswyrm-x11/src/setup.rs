use crate::wire::{Writer, align_four, padding_for};
use crate::{ByteOrder, SCREEN_MODEL, ScreenModel};

pub const PROTOCOL_MAJOR: u16 = 11;
pub const PROTOCOL_MINOR: u16 = 0;
pub const SETUP_REQUEST_HEADER_SIZE: usize = 12;
pub const DEFAULT_MAXIMUM_SETUP_SIZE: usize = 4096;
const VENDOR: &[u8; 21] = b"Glasswyrm Milestone 1";

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SetupRequest {
    pub byte_order: ByteOrder,
    pub protocol_major: u16,
    pub protocol_minor: u16,
    pub authorization_name: Vec<u8>,
    pub authorization_data: Vec<u8>,
}

impl Default for SetupRequest {
    fn default() -> Self {
        Self {
            byte_order: ByteOrder::LittleEndian,
            protocol_major: 0,
            protocol_minor: 0,
            authorization_name: Vec::new(),
            authorization_data: Vec::new(),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseStatus {
    NeedMore,
    Complete,
    InvalidByteOrder,
    MessageTooLarge,
    LengthOverflow,
    TruncatedInput,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ParseResult {
    pub status: ParseStatus,
    pub consumed: usize,
}

#[derive(Debug)]
pub struct SetupParser {
    maximum_size: usize,
    expected_size: usize,
    authorization_name_size: usize,
    authorization_name_padded_size: usize,
    authorization_data_size: usize,
    status: ParseStatus,
    request: SetupRequest,
    bytes: Vec<u8>,
}

impl Default for SetupParser {
    fn default() -> Self {
        Self::new(DEFAULT_MAXIMUM_SETUP_SIZE)
    }
}

impl SetupParser {
    #[must_use]
    pub fn new(maximum_size: usize) -> Self {
        let status = if maximum_size < SETUP_REQUEST_HEADER_SIZE {
            ParseStatus::MessageTooLarge
        } else {
            ParseStatus::NeedMore
        };
        Self {
            maximum_size,
            expected_size: SETUP_REQUEST_HEADER_SIZE,
            authorization_name_size: 0,
            authorization_name_padded_size: 0,
            authorization_data_size: 0,
            status,
            request: SetupRequest::default(),
            bytes: Vec::with_capacity(maximum_size.min(DEFAULT_MAXIMUM_SETUP_SIZE)),
        }
    }

    pub fn consume(&mut self, input: &[u8]) -> ParseResult {
        if self.status != ParseStatus::NeedMore {
            return ParseResult {
                status: self.status,
                consumed: 0,
            };
        }

        let mut consumed = 0;
        while consumed < input.len() && self.status == ParseStatus::NeedMore {
            if self.bytes.is_empty() {
                let marker = input[consumed];
                self.bytes.push(marker);
                consumed += 1;
                let Some(order) = ByteOrder::from_marker(marker) else {
                    self.status = ParseStatus::InvalidByteOrder;
                    break;
                };
                self.request.byte_order = order;
            }

            let needed = self.expected_size - self.bytes.len();
            let copy_size = needed.min(input.len() - consumed);
            self.bytes
                .extend_from_slice(&input[consumed..consumed + copy_size]);
            consumed += copy_size;

            if self.bytes.len() == SETUP_REQUEST_HEADER_SIZE
                && self.expected_size == SETUP_REQUEST_HEADER_SIZE
            {
                self.status = self.inspect_header();
                if self.status != ParseStatus::NeedMore {
                    break;
                }
            }

            if self.bytes.len() == self.expected_size {
                self.finish_request();
                self.status = ParseStatus::Complete;
            }
        }

        ParseResult {
            status: self.status,
            consumed,
        }
    }

    #[must_use]
    pub const fn eof(&self) -> ParseStatus {
        if matches!(self.status, ParseStatus::NeedMore) {
            ParseStatus::TruncatedInput
        } else {
            self.status
        }
    }

    #[must_use]
    pub const fn request(&self) -> &SetupRequest {
        &self.request
    }

    #[must_use]
    pub const fn expected_size(&self) -> usize {
        self.expected_size
    }

    fn inspect_header(&mut self) -> ParseStatus {
        let order = self.request.byte_order;
        self.request.protocol_major = order.read_u16([self.bytes[2], self.bytes[3]]);
        self.request.protocol_minor = order.read_u16([self.bytes[4], self.bytes[5]]);
        self.authorization_name_size = usize::from(order.read_u16([self.bytes[6], self.bytes[7]]));
        self.authorization_data_size = usize::from(order.read_u16([self.bytes[8], self.bytes[9]]));

        let Some(padded_name) = align_four(self.authorization_name_size) else {
            return ParseStatus::LengthOverflow;
        };
        let Some(padded_data) = align_four(self.authorization_data_size) else {
            return ParseStatus::LengthOverflow;
        };
        self.authorization_name_padded_size = padded_name;
        let Some(total_size) = SETUP_REQUEST_HEADER_SIZE
            .checked_add(padded_name)
            .and_then(|size| size.checked_add(padded_data))
        else {
            return ParseStatus::LengthOverflow;
        };
        if total_size > self.maximum_size {
            return ParseStatus::MessageTooLarge;
        }
        self.expected_size = total_size;
        ParseStatus::NeedMore
    }

    fn finish_request(&mut self) {
        let name_offset = SETUP_REQUEST_HEADER_SIZE;
        let data_offset = name_offset + self.authorization_name_padded_size;
        self.request.authorization_name =
            self.bytes[name_offset..name_offset + self.authorization_name_size].to_vec();
        self.request.authorization_data =
            self.bytes[data_offset..data_offset + self.authorization_data_size].to_vec();
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SetupDecision {
    Accepted,
    UnsupportedVersion,
    UnsupportedAuthorization,
}

#[must_use]
pub fn evaluate_setup_request(request: &SetupRequest) -> SetupDecision {
    if request.protocol_major != PROTOCOL_MAJOR || request.protocol_minor != PROTOCOL_MINOR {
        SetupDecision::UnsupportedVersion
    } else if !request.authorization_name.is_empty() || !request.authorization_data.is_empty() {
        SetupDecision::UnsupportedAuthorization
    } else {
        SetupDecision::Accepted
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SetupReplyConfig {
    pub resource_id_base: u32,
    pub resource_id_mask: u32,
    pub screen: ScreenModel,
    pub game_compat: bool,
}

impl Default for SetupReplyConfig {
    fn default() -> Self {
        Self {
            resource_id_base: 0x0040_0000,
            resource_id_mask: SCREEN_MODEL.resource_id_mask,
            screen: SCREEN_MODEL,
            game_compat: false,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SetupEncodeError {
    SuccessReplyTooLarge,
    FailureReasonTooLarge,
}

pub fn encode_setup_success(
    order: ByteOrder,
    config: &SetupReplyConfig,
) -> Result<Vec<u8>, SetupEncodeError> {
    let mut body = Writer::new(order);
    body.write_u32(1);
    body.write_u32(config.resource_id_base);
    body.write_u32(config.resource_id_mask);
    body.write_u32(0);
    body.write_u16(VENDOR.len() as u16);
    body.write_u16(config.screen.maximum_request_length);
    body.write_u8(1);
    body.write_u8(if config.game_compat { 4 } else { 2 });
    body.write_u8(0);
    body.write_u8(0);
    body.write_u8(32);
    body.write_u8(32);
    body.write_u8(8);
    body.write_u8(255);
    body.write_padding(4);
    body.write_bytes(VENDOR);
    body.write_padding(padding_for(VENDOR.len()));

    write_pixmap_format(&mut body, 1, 1);
    if config.game_compat {
        write_pixmap_format(&mut body, 8, 8);
    }
    write_pixmap_format(&mut body, 24, 32);
    if config.game_compat {
        write_pixmap_format(&mut body, 32, 32);
    }

    body.write_u32(config.screen.root_window);
    body.write_u32(config.screen.default_colormap);
    body.write_u32(0x00ff_ffff);
    body.write_u32(0);
    body.write_u32(0);
    body.write_u16(config.screen.width_pixels);
    body.write_u16(config.screen.height_pixels);
    body.write_u16(config.screen.width_millimeters);
    body.write_u16(config.screen.height_millimeters);
    body.write_u16(1);
    body.write_u16(1);
    body.write_u32(config.screen.root_visual);
    body.write_u8(0);
    body.write_u8(0);
    body.write_u8(config.screen.root_depth);
    body.write_u8(if config.game_compat { 4 } else { 2 });

    body.write_u8(config.screen.root_depth);
    body.write_padding(1);
    body.write_u16(1);
    body.write_padding(4);
    body.write_u32(config.screen.root_visual);
    body.write_u8(4);
    body.write_u8(8);
    body.write_u16(256);
    body.write_u32(config.screen.red_mask);
    body.write_u32(config.screen.green_mask);
    body.write_u32(config.screen.blue_mask);
    body.write_padding(4);
    write_empty_depth(&mut body, 1);
    if config.game_compat {
        write_empty_depth(&mut body, 8);
        write_empty_depth(&mut body, 32);
    }

    let body_size = body.len();
    if body_size & 3 != 0 || body_size / 4 > usize::from(u16::MAX) {
        return Err(SetupEncodeError::SuccessReplyTooLarge);
    }
    let mut reply = Writer::new(order);
    reply.write_u8(1);
    reply.write_u8(0);
    reply.write_u16(PROTOCOL_MAJOR);
    reply.write_u16(PROTOCOL_MINOR);
    reply.write_u16((body_size / 4) as u16);
    reply.write_bytes(&body.into_bytes());
    Ok(reply.into_bytes())
}

pub fn encode_setup_failure(order: ByteOrder, reason: &[u8]) -> Result<Vec<u8>, SetupEncodeError> {
    let reason_size =
        u8::try_from(reason.len()).map_err(|_| SetupEncodeError::FailureReasonTooLarge)?;
    let padding = padding_for(reason.len());
    let mut reply = Writer::new(order);
    reply.write_u8(0);
    reply.write_u8(reason_size);
    reply.write_u16(PROTOCOL_MAJOR);
    reply.write_u16(PROTOCOL_MINOR);
    reply.write_u16(((reason.len() + padding) / 4) as u16);
    reply.write_bytes(reason);
    reply.write_padding(padding);
    Ok(reply.into_bytes())
}

fn write_pixmap_format(body: &mut Writer, depth: u8, bits_per_pixel: u8) {
    body.write_u8(depth);
    body.write_u8(bits_per_pixel);
    body.write_u8(32);
    body.write_padding(5);
}

fn write_empty_depth(body: &mut Writer, depth: u8) {
    body.write_u8(depth);
    body.write_padding(1);
    body.write_u16(0);
    body.write_padding(4);
}
