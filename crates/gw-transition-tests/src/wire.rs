use gw_types::{MessageFlags, MessageType, Sequence};
use gw_wire::{ByteReader, ByteWriter, Envelope, PrimitiveDecodeError};

pub const OUTPUT_QUERY_LAYOUT: u32 = 1 << 2;
pub const OUTPUT_QUERY_VRR: u32 = 1 << 4;
pub const OUTPUT_CONFIGURATION_ACCEPTED: u16 = 1;
pub const OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED: u16 = 10;

#[derive(Clone, Debug)]
pub struct Snapshot {
    pub generation: u64,
    pub primary_output: u64,
    pub root_width: u32,
    pub root_height: u32,
    pub result: u16,
    pub outputs: Vec<Vec<u8>>,
    pub vrr_policies: Vec<Vec<u8>>,
    pub vrr_state_count: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Acknowledgement {
    pub request_id: u64,
    pub generation: u64,
    pub result: u16,
    pub primary_output: u64,
    pub root_width: u32,
    pub root_height: u32,
    pub enabled_output_count: u32,
}

pub fn request_envelope(
    message_type: MessageType,
    flags: MessageFlags,
    sequence: u64,
    payload_size: usize,
) -> Result<Envelope, String> {
    let payload_size = u32::try_from(payload_size)
        .map_err(|_| "GWIPC payload cannot be represented as u32".to_owned())?;
    let mut envelope = Envelope::request(message_type, Sequence::new(sequence), payload_size);
    envelope.flags = flags;
    Ok(envelope)
}

pub fn encode_query(query_id: u64) -> Vec<u8> {
    let mut writer = ByteWriter::with_capacity(16);
    writer.write_u64(query_id);
    writer.write_u32(OUTPUT_QUERY_LAYOUT | OUTPUT_QUERY_VRR);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn encode_snapshot_begin(
    configuration_id: u64,
    generation: u64,
    expected_items: u32,
) -> Vec<u8> {
    let mut writer = ByteWriter::with_capacity(28);
    writer.write_u64(configuration_id);
    writer.write_u16(1); // SnapshotDomain::Outputs
    writer.write_u16(0);
    writer.write_u64(generation);
    writer.write_u32(expected_items);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn encode_snapshot_end(configuration_id: u64, generation: u64, actual_items: u32) -> Vec<u8> {
    let mut writer = ByteWriter::with_capacity(24);
    writer.write_u64(configuration_id);
    writer.write_u64(generation);
    writer.write_u32(actual_items);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn encode_commit(configuration_id: u64, generation: u64, primary_output: u64) -> Vec<u8> {
    let mut writer = ByteWriter::with_capacity(32);
    writer.write_u64(configuration_id);
    writer.write_u64(generation);
    writer.write_u64(primary_output);
    writer.write_u32(0);
    writer.write_u32(0);
    writer.into_bytes()
}

pub fn decode_acknowledgement(payload: &[u8]) -> Result<Acknowledgement, String> {
    let mut reader = ByteReader::new(payload);
    let value = Acknowledgement {
        request_id: reader.read_u64().map_err(decode_error)?,
        generation: reader.read_u64().map_err(decode_error)?,
        result: reader.read_u16().map_err(decode_error)?,
        primary_output: {
            let reserved = reader.read_u16().map_err(decode_error)?;
            let flags = reader.read_u32().map_err(decode_error)?;
            if reserved != 0 || flags != 0 {
                return Err("output acknowledgement has nonzero reserved fields".to_owned());
            }
            reader.read_u64().map_err(decode_error)?
        },
        root_width: reader.read_u32().map_err(decode_error)?,
        root_height: reader.read_u32().map_err(decode_error)?,
        enabled_output_count: reader.read_u32().map_err(decode_error)?,
    };
    if reader.read_u32().map_err(decode_error)? != 0 {
        return Err("output acknowledgement has a nonzero trailing reserved field".to_owned());
    }
    reader.finish().map_err(decode_error)?;
    Ok(value)
}

pub fn output_id(payload: &[u8]) -> Result<u64, String> {
    let mut reader = ByteReader::new(payload);
    reader.read_u64().map_err(decode_error)
}

pub fn vertical_output_payloads(outputs: &[Vec<u8>]) -> Result<Vec<Vec<u8>>, String> {
    if outputs.len() != 2 {
        return Err(format!("expected two outputs, observed {}", outputs.len()));
    }
    outputs
        .iter()
        .enumerate()
        .map(|(index, output)| {
            if output.len() < 20 {
                return Err(
                    "legacy output payload is truncated before layout coordinates".to_owned(),
                );
            }
            let mut output = output.clone();
            output[12..16].copy_from_slice(&0_i32.to_le_bytes());
            let y = if index == 0 { 0_i32 } else { 480_i32 };
            output[16..20].copy_from_slice(&y.to_le_bytes());
            Ok(output)
        })
        .collect()
}

fn decode_error(error: PrimitiveDecodeError) -> String {
    format!("invalid legacy GWIPC payload: {error}")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acknowledgement_uses_the_legacy_field_order() {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&601_u64.to_le_bytes());
        bytes.extend_from_slice(&1_u64.to_le_bytes());
        bytes.extend_from_slice(&10_u16.to_le_bytes());
        bytes.extend_from_slice(&0_u16.to_le_bytes());
        bytes.extend_from_slice(&0_u32.to_le_bytes());
        bytes.extend_from_slice(&17_u64.to_le_bytes());
        bytes.extend_from_slice(&1280_u32.to_le_bytes());
        bytes.extend_from_slice(&480_u32.to_le_bytes());
        bytes.extend_from_slice(&2_u32.to_le_bytes());
        bytes.extend_from_slice(&0_u32.to_le_bytes());
        assert_eq!(
            decode_acknowledgement(&bytes).unwrap(),
            Acknowledgement {
                request_id: 601,
                generation: 1,
                result: OUTPUT_CONFIGURATION_COMPOSITOR_REJECTED,
                primary_output: 17,
                root_width: 1280,
                root_height: 480,
                enabled_output_count: 2,
            }
        );
    }

    #[test]
    fn vertical_edit_changes_only_the_coordinate_fields() {
        let first = (0_u8..68).collect::<Vec<_>>();
        let second = (68_u8..136).collect::<Vec<_>>();
        let edited = vertical_output_payloads(&[first.clone(), second.clone()]).unwrap();
        assert_eq!(&edited[0][..12], &first[..12]);
        assert_eq!(&edited[0][12..20], &[0; 8]);
        assert_eq!(&edited[0][20..], &first[20..]);
        assert_eq!(&edited[1][12..16], &[0; 4]);
        assert_eq!(&edited[1][16..20], &480_i32.to_le_bytes());
        assert_eq!(&edited[1][20..], &second[20..]);
    }
}
