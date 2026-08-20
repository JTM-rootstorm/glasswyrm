use crate::{
    ByteOrder, ClientMessageData, CoreErrorCode, CoreEventType, CoreRequestDecodeError, EventMask,
    FramedRequest,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SetSelectionOwnerRequest {
    pub owner: u32,
    pub selection: u32,
    pub time: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ConvertSelectionRequest {
    pub requestor: u32,
    pub selection: u32,
    pub target: u32,
    pub property: u32,
    pub time: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SendEventHeader<'a> {
    pub propagate: bool,
    pub destination: u32,
    pub event_mask_bits: u32,
    pub raw_type: u8,
    pub detail: u8,
    payload: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionNotifyWire {
    pub time: u32,
    pub requestor: u32,
    pub selection: u32,
    pub target: u32,
    pub property: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UnmapNotifyWire {
    pub event_window: u32,
    pub window: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ClientMessageWire<'a> {
    pub window: u32,
    pub message_type: u32,
    pub detail: u8,
    data: &'a [u8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SendEventPayload<'a> {
    SelectionNotify(SelectionNotifyWire),
    UnmapNotify(UnmapNotifyWire),
    ClientMessage(ClientMessageWire<'a>),
}

pub fn decode_set_selection_owner(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<SetSelectionOwnerRequest, CoreRequestDecodeError> {
    if request.core_size() != 16 || request.body().len() != 12 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    Ok(SetSelectionOwnerRequest {
        owner: read_u32(order, body, 0),
        selection: read_u32(order, body, 4),
        time: read_u32(order, body, 8),
    })
}

pub fn decode_get_selection_owner(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<u32, CoreRequestDecodeError> {
    if request.core_size() != 8 || request.body().len() != 4 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    Ok(read_u32(order, request.body(), 0))
}

pub fn decode_convert_selection(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<ConvertSelectionRequest, CoreRequestDecodeError> {
    if request.core_size() != 24 || request.body().len() != 20 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    let body = request.body();
    Ok(ConvertSelectionRequest {
        requestor: read_u32(order, body, 0),
        selection: read_u32(order, body, 4),
        target: read_u32(order, body, 8),
        property: read_u32(order, body, 12),
        time: read_u32(order, body, 16),
    })
}

pub fn decode_send_event_header(
    order: ByteOrder,
    request: &FramedRequest,
) -> Result<SendEventHeader<'_>, CoreRequestDecodeError> {
    if request.core_size() != 44 || request.body().len() != 40 {
        return Err(CoreRequestDecodeError::bad_length());
    }
    if request.data > 1 {
        return Err(error(CoreErrorCode::BadValue, u32::from(request.data)));
    }
    let body = request.body();
    Ok(SendEventHeader {
        propagate: request.data != 0,
        destination: read_u32(order, body, 0),
        event_mask_bits: read_u32(order, body, 4),
        raw_type: body[8],
        detail: body[9],
        payload: &body[12..],
    })
}

pub fn decode_send_event_after_destination(
    order: ByteOrder,
    header: SendEventHeader<'_>,
) -> Result<SendEventPayload<'_>, CoreRequestDecodeError> {
    EventMask::from_bits(header.event_mask_bits)
        .ok_or_else(|| error(CoreErrorCode::BadValue, header.event_mask_bits))?;
    let event_type = header.raw_type & 0x7f;
    if event_type == CoreEventType::SelectionNotify as u8 {
        if header.detail != 0 || header.event_mask_bits != 0 || header.propagate {
            return Err(error(CoreErrorCode::BadValue, u32::from(header.raw_type)));
        }
        return Ok(SendEventPayload::SelectionNotify(SelectionNotifyWire {
            time: read_u32(order, header.payload, 0),
            requestor: read_u32(order, header.payload, 4),
            selection: read_u32(order, header.payload, 8),
            target: read_u32(order, header.payload, 12),
            property: read_u32(order, header.payload, 16),
        }));
    }
    if event_type == CoreEventType::UnmapNotify as u8 {
        let withdraw_mask =
            EventMask::SUBSTRUCTURE_NOTIFY.bits() | EventMask::SUBSTRUCTURE_REDIRECT.bits();
        let event_window = read_u32(order, header.payload, 0);
        let window = read_u32(order, header.payload, 4);
        let from_configure = header.payload[8];
        if header.detail != 0
            || header.propagate
            || header.event_mask_bits != withdraw_mask
            || event_window != header.destination
            || from_configure != 0
        {
            return Err(error(CoreErrorCode::BadValue, u32::from(header.raw_type)));
        }
        return Ok(SendEventPayload::UnmapNotify(UnmapNotifyWire {
            event_window,
            window,
        }));
    }
    if event_type != CoreEventType::ClientMessage as u8 {
        return Err(error(CoreErrorCode::BadValue, u32::from(header.raw_type)));
    }
    Ok(SendEventPayload::ClientMessage(ClientMessageWire {
        window: read_u32(order, header.payload, 0),
        message_type: read_u32(order, header.payload, 4),
        detail: header.detail,
        data: &header.payload[8..],
    }))
}

pub fn decode_client_message_data(
    order: ByteOrder,
    message: ClientMessageWire<'_>,
) -> Result<ClientMessageData, CoreRequestDecodeError> {
    match message.detail {
        8 => {
            let mut values = [0_u8; 20];
            values.copy_from_slice(message.data);
            Ok(ClientMessageData::U8(values))
        }
        16 => {
            let mut values = [0_u16; 10];
            for (index, value) in values.iter_mut().enumerate() {
                *value = read_u16(order, message.data, index * 2);
            }
            Ok(ClientMessageData::U16(values))
        }
        32 => {
            let mut values = [0_u32; 5];
            for (index, value) in values.iter_mut().enumerate() {
                *value = read_u32(order, message.data, index * 4);
            }
            Ok(ClientMessageData::U32(values))
        }
        detail => Err(error(CoreErrorCode::BadValue, u32::from(detail))),
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

    fn framed(order: ByteOrder, opcode: u8, data: u8, body: &[u32]) -> FramedRequest {
        let mut bytes = vec![opcode, data, 0, 0];
        for value in body {
            bytes.extend_from_slice(&order.u32_bytes(*value));
        }
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

    fn send_event(
        order: ByteOrder,
        propagate: bool,
        destination: u32,
        event_mask: u32,
        raw_type: u8,
        detail: u8,
        payload: [u8; 28],
    ) -> FramedRequest {
        let mut bytes = vec![25, u8::from(propagate), 0, 0];
        bytes.extend_from_slice(&order.u32_bytes(destination));
        bytes.extend_from_slice(&order.u32_bytes(event_mask));
        bytes.push(raw_type);
        bytes.push(detail);
        bytes.extend_from_slice(&order.u16_bytes(0));
        bytes.extend_from_slice(&payload);
        bytes[2..4].copy_from_slice(&order.u16_bytes(11));
        FramedRequest {
            opcode: 25,
            data: u8::from(propagate),
            length_units: 11,
            header_size: 4,
            bytes,
        }
    }

    #[test]
    fn selection_request_fields_match_both_legacy_byte_orders() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let set = framed(order, 22, 0, &[1, 2, 3]);
            assert_eq!(
                decode_set_selection_owner(order, &set).unwrap(),
                SetSelectionOwnerRequest {
                    owner: 1,
                    selection: 2,
                    time: 3
                }
            );
            let get = framed(order, 23, 0, &[0x1122_3344]);
            assert_eq!(
                decode_get_selection_owner(order, &get).unwrap(),
                0x1122_3344
            );
            let convert = framed(order, 24, 0, &[1, 2, 3, 4, 5]);
            assert_eq!(
                decode_convert_selection(order, &convert).unwrap(),
                ConvertSelectionRequest {
                    requestor: 1,
                    selection: 2,
                    target: 3,
                    property: 4,
                    time: 5
                }
            );
        }
    }

    #[test]
    fn send_event_keeps_destination_before_mask_validation() {
        let order = ByteOrder::LittleEndian;
        let request = send_event(order, false, 0x1234, 1 << 25, 33, 8, [0; 28]);
        let header = decode_send_event_header(order, &request).unwrap();
        assert_eq!(header.destination, 0x1234);
        assert_eq!(
            decode_send_event_after_destination(order, header),
            Err(error(CoreErrorCode::BadValue, 1 << 25))
        );
    }

    #[test]
    fn selection_and_unmap_event_shapes_match_legacy_rules() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut selection = [0_u8; 28];
            for (index, value) in [1_u32, 2, 3, 4, 5].into_iter().enumerate() {
                selection[index * 4..index * 4 + 4].copy_from_slice(&order.u32_bytes(value));
            }
            let request = send_event(order, false, 2, 0, 31 | 0x80, 0, selection);
            let header = decode_send_event_header(order, &request).unwrap();
            assert_eq!(
                decode_send_event_after_destination(order, header).unwrap(),
                SendEventPayload::SelectionNotify(SelectionNotifyWire {
                    time: 1,
                    requestor: 2,
                    selection: 3,
                    target: 4,
                    property: 5
                })
            );

            let mut unmap = [0_u8; 28];
            unmap[..4].copy_from_slice(&order.u32_bytes(7));
            unmap[4..8].copy_from_slice(&order.u32_bytes(8));
            let mask =
                EventMask::SUBSTRUCTURE_NOTIFY.bits() | EventMask::SUBSTRUCTURE_REDIRECT.bits();
            let request = send_event(order, false, 7, mask, 18, 0, unmap);
            let header = decode_send_event_header(order, &request).unwrap();
            assert_eq!(
                decode_send_event_after_destination(order, header).unwrap(),
                SendEventPayload::UnmapNotify(UnmapNotifyWire {
                    event_window: 7,
                    window: 8
                })
            );
        }
    }

    #[test]
    fn client_message_data_validation_is_a_separate_post_atom_step() {
        for order in [ByteOrder::LittleEndian, ByteOrder::BigEndian] {
            let mut payload = [0_u8; 28];
            payload[..4].copy_from_slice(&order.u32_bytes(10));
            payload[4..8].copy_from_slice(&order.u32_bytes(20));
            for index in 0..5 {
                payload[8 + index * 4..12 + index * 4]
                    .copy_from_slice(&order.u32_bytes((index + 1) as u32));
            }
            let request = send_event(
                order,
                true,
                10,
                EventMask::STRUCTURE_NOTIFY.bits(),
                33,
                32,
                payload,
            );
            let header = decode_send_event_header(order, &request).unwrap();
            let SendEventPayload::ClientMessage(message) =
                decode_send_event_after_destination(order, header).unwrap()
            else {
                panic!("ClientMessage must stay typed")
            };
            assert_eq!(message.window, 10);
            assert_eq!(message.message_type, 20);
            assert_eq!(
                decode_client_message_data(order, message).unwrap(),
                ClientMessageData::U32([1, 2, 3, 4, 5])
            );

            let invalid = send_event(order, false, 10, 0, 33, 7, payload);
            let header = decode_send_event_header(order, &invalid).unwrap();
            let SendEventPayload::ClientMessage(message) =
                decode_send_event_after_destination(order, header).unwrap()
            else {
                panic!("invalid detail is deliberately deferred until after atom validation")
            };
            assert_eq!(
                decode_client_message_data(order, message),
                Err(error(CoreErrorCode::BadValue, 7))
            );
        }
    }

    #[test]
    fn send_event_rejects_request_flag_and_length_before_body_semantics() {
        let order = ByteOrder::LittleEndian;
        let mut invalid_flag = send_event(order, false, 1, 0, 33, 8, [0; 28]);
        invalid_flag.data = 2;
        invalid_flag.bytes[1] = 2;
        assert_eq!(
            decode_send_event_header(order, &invalid_flag),
            Err(error(CoreErrorCode::BadValue, 2))
        );
        let mut short = send_event(order, false, 1, 0, 33, 8, [0; 28]);
        short.bytes.pop();
        assert_eq!(
            decode_send_event_header(order, &short),
            Err(CoreRequestDecodeError::bad_length())
        );
    }
}
