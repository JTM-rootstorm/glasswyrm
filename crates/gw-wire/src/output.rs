use crate::compositor::{ContractDecodeError, Transform};
use crate::{ByteReader, ByteWriter};

pub const MAXIMUM_OUTPUT_NAME_BYTES: usize = 63;
pub const MAXIMUM_MANAGED_OUTPUTS: usize = 8;
pub const KNOWN_OUTPUT_CAPABILITY_FLAGS: u32 = 0x7f;
pub const KNOWN_OUTPUT_QUERY_FLAGS: u32 = 0x1f;
pub const OUTPUT_PHYSICAL_DIMENSIONS_KNOWN: u32 = 1 << 6;
pub const OUTPUT_ARBITRARY_HEADLESS_MODE: u32 = 1 << 1;
pub const OUTPUT_MODE_FIXED: u32 = 1 << 2;

macro_rules! wire_enum { ($name:ident { $($variant:ident=$value:expr),+ $(,)? }) => { #[derive(Clone,Copy,Debug,Eq,PartialEq)] #[repr(u16)] pub enum $name{$($variant=$value),+} impl TryFrom<u16> for $name{type Error=ContractDecodeError;fn try_from(v:u16)->Result<Self,Self::Error>{match v{$($value=>Ok(Self::$variant),)+_=>Err(ContractDecodeError::InvalidValue)}}}};}
wire_enum!(OutputKind{Headless=1,Drm=2});
wire_enum!(SurfaceScaleMode{Legacy=1,ScaledPixmap=2});
wire_enum!(OutputConfigurationResult{Accepted=1,StaleGeneration=2,Busy=3,InvalidLayout=4,UnknownOutput=5,UnsupportedMode=6,UnsupportedScale=7,UnsupportedTransform=8,PolicyRejected=9,CompositorRejected=10,PresenterRejected=11,InternalError=12,UnsupportedVrr=13,VrrPolicyRejected=14,VrrPresenterRejected=15});

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OutputDescriptorUpsert {
    pub output_id: u64,
    pub kind: OutputKind,
    pub capability_flags: u32,
    pub name: String,
    pub physical_width_millimeters: u32,
    pub physical_height_millimeters: u32,
    pub supported_transform_mask: u32,
    pub minimum_scale_numerator: u32,
    pub minimum_scale_denominator: u32,
    pub maximum_scale_numerator: u32,
    pub maximum_scale_denominator: u32,
    pub maximum_scale_denominator_value: u32,
    pub maximum_physical_width: u32,
    pub maximum_physical_height: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputModeUpsert {
    pub output_id: u64,
    pub mode_id: u64,
    pub physical_width: u32,
    pub physical_height: u32,
    pub refresh_millihertz: u32,
    pub preferred: bool,
    pub current: bool,
    pub flags: u32,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceOutputState {
    pub surface_id: u64,
    pub primary_output_id: u64,
    pub output_ids: Vec<u64>,
    pub preferred_scale_numerator: u32,
    pub preferred_scale_denominator: u32,
    pub client_buffer_scale: u32,
    pub scale_mode: SurfaceScaleMode,
    pub layout_generation: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyOutputUpsert {
    pub output_id: u64,
    pub logical_x: i32,
    pub logical_y: i32,
    pub logical_width: u32,
    pub logical_height: u32,
    pub work_x: i32,
    pub work_y: i32,
    pub work_width: u32,
    pub work_height: u32,
    pub scale_numerator: u32,
    pub scale_denominator: u32,
    pub transform: Transform,
    pub enabled: bool,
    pub primary: bool,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PolicyWindowOutputHint {
    pub window_id: u32,
    pub previous_output_id: u64,
    pub preferred_output_id: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputStateQuery {
    pub query_id: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputConfigurationCommit {
    pub configuration_id: u64,
    pub base_generation: u64,
    pub primary_output_id: u64,
    pub flags: u32,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputConfigurationAcknowledged {
    pub request_id: u64,
    pub applied_generation: u64,
    pub result: OutputConfigurationResult,
    pub flags: u32,
    pub primary_output_id: u64,
    pub root_logical_width: u32,
    pub root_logical_height: u32,
    pub enabled_output_count: u32,
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
fn read_bool(r: &mut ByteReader<'_>) -> Result<bool, ContractDecodeError> {
    match r.read_u8()? {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(ContractDecodeError::InvalidValue),
    }
}
fn gcd(mut a: u32, mut b: u32) -> u32 {
    while b != 0 {
        let n = a % b;
        a = b;
        b = n
    }
    a
}
fn valid_scale(n: u32, d: u32, limit: u32) -> bool {
    n != 0
        && d != 0
        && d <= limit
        && gcd(n, d) == 1
        && u64::from(n) >= u64::from(d)
        && u64::from(n) <= 4 * u64::from(d)
}
fn valid_extent(o: i32, e: u32) -> bool {
    e != 0 && o >= 0 && (o as u64) + u64::from(e) <= i32::MAX as u64 + 1
}

pub fn encode_output_descriptor_upsert(
    v: &OutputDescriptorUpsert,
) -> Result<Vec<u8>, ContractDecodeError> {
    let n = u16::try_from(v.name.len()).map_err(|_| ContractDecodeError::LimitExceeded)?;
    if v.name.len() > MAXIMUM_OUTPUT_NAME_BYTES {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u16(v.kind as u16);
    w.write_u16(n);
    w.write_u32(v.capability_flags);
    w.write_u32(v.physical_width_millimeters);
    w.write_u32(v.physical_height_millimeters);
    w.write_u32(v.supported_transform_mask);
    w.write_u32(v.minimum_scale_numerator);
    w.write_u32(v.minimum_scale_denominator);
    w.write_u32(v.maximum_scale_numerator);
    w.write_u32(v.maximum_scale_denominator);
    w.write_u32(v.maximum_scale_denominator_value);
    w.write_u32(v.maximum_physical_width);
    w.write_u32(v.maximum_physical_height);
    w.write_bytes(v.name.as_bytes());
    Ok(w.into_bytes())
}
pub fn decode_output_descriptor_upsert(
    bytes: &[u8],
) -> Result<OutputDescriptorUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let output_id = r.read_u64()?;
    let kind = r.read_u16()?.try_into()?;
    let name_size = r.read_u16()? as usize;
    let capability_flags = r.read_u32()?;
    let physical_width_millimeters = r.read_u32()?;
    let physical_height_millimeters = r.read_u32()?;
    let supported_transform_mask = r.read_u32()?;
    let minimum_scale_numerator = r.read_u32()?;
    let minimum_scale_denominator = r.read_u32()?;
    let maximum_scale_numerator = r.read_u32()?;
    let maximum_scale_denominator = r.read_u32()?;
    let maximum_scale_denominator_value = r.read_u32()?;
    let maximum_physical_width = r.read_u32()?;
    let maximum_physical_height = r.read_u32()?;
    if name_size > MAXIMUM_OUTPUT_NAME_BYTES {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let name = std::str::from_utf8(r.read_bytes(name_size)?)
        .map_err(|_| ContractDecodeError::InvalidValue)?
        .to_owned();
    let v = OutputDescriptorUpsert {
        output_id,
        kind,
        capability_flags,
        name,
        physical_width_millimeters,
        physical_height_millimeters,
        supported_transform_mask,
        minimum_scale_numerator,
        minimum_scale_denominator,
        maximum_scale_numerator,
        maximum_scale_denominator,
        maximum_scale_denominator_value,
        maximum_physical_width,
        maximum_physical_height,
    };
    let dimensions = (capability_flags & OUTPUT_PHYSICAL_DIMENSIONS_KNOWN) != 0;
    let valid = output_id != 0
        && !v.name.is_empty()
        && (capability_flags & !KNOWN_OUTPUT_CAPABILITY_FLAGS) == 0
        && !((capability_flags & OUTPUT_ARBITRARY_HEADLESS_MODE) != 0
            && (capability_flags & OUTPUT_MODE_FIXED) != 0)
        && !(kind == OutputKind::Drm && (capability_flags & OUTPUT_ARBITRARY_HEADLESS_MODE) != 0)
        && (dimensions == (physical_width_millimeters != 0 && physical_height_millimeters != 0))
        && (dimensions != (physical_width_millimeters == 0 && physical_height_millimeters == 0))
        && supported_transform_mask != 0
        && (supported_transform_mask & !0xff) == 0
        && maximum_scale_denominator_value != 0
        && maximum_scale_denominator_value <= 120
        && valid_scale(
            minimum_scale_numerator,
            minimum_scale_denominator,
            maximum_scale_denominator_value,
        )
        && valid_scale(
            maximum_scale_numerator,
            maximum_scale_denominator,
            maximum_scale_denominator_value,
        )
        && u64::from(minimum_scale_numerator) * u64::from(maximum_scale_denominator)
            <= u64::from(maximum_scale_numerator) * u64::from(minimum_scale_denominator)
        && (1..=4096).contains(&maximum_physical_width)
        && (1..=4096).contains(&maximum_physical_height);
    finish(r, valid)?;
    Ok(v)
}

#[must_use]
pub fn encode_output_mode_upsert(v: &OutputModeUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_u64(v.mode_id);
    w.write_u32(v.physical_width);
    w.write_u32(v.physical_height);
    w.write_u32(v.refresh_millihertz);
    w.write_u8(u8::from(v.preferred));
    w.write_u8(u8::from(v.current));
    w.write_u16(0);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_output_mode_upsert(bytes: &[u8]) -> Result<OutputModeUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = OutputModeUpsert {
        output_id: r.read_u64()?,
        mode_id: r.read_u64()?,
        physical_width: r.read_u32()?,
        physical_height: r.read_u32()?,
        refresh_millihertz: r.read_u32()?,
        preferred: read_bool(&mut r)?,
        current: read_bool(&mut r)?,
        flags: {
            if r.read_u16()? != 0 {
                return Err(ContractDecodeError::InvalidValue);
            }
            r.read_u32()?
        },
    };
    let reserved = r.read_u32()?;
    let pixels = u64::from(v.physical_width) * u64::from(v.physical_height);
    finish(
        r,
        v.output_id != 0
            && v.mode_id != 0
            && (1..=4096).contains(&v.physical_width)
            && (1..=4096).contains(&v.physical_height)
            && pixels <= 16_777_216
            && v.refresh_millihertz != 0
            && v.flags == 0
            && reserved == 0,
    )?;
    Ok(v)
}

pub fn encode_surface_output_state(v: &SurfaceOutputState) -> Result<Vec<u8>, ContractDecodeError> {
    if v.output_ids.len() > MAXIMUM_MANAGED_OUTPUTS {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let mut w = ByteWriter::new();
    w.write_u64(v.surface_id);
    w.write_u64(v.primary_output_id);
    w.write_u64(v.layout_generation);
    w.write_u32(v.preferred_scale_numerator);
    w.write_u32(v.preferred_scale_denominator);
    w.write_u32(v.client_buffer_scale);
    w.write_u16(v.scale_mode as u16);
    w.write_u16(0);
    w.write_u32(v.flags);
    w.write_u32(v.output_ids.len() as u32);
    for id in &v.output_ids {
        w.write_u64(*id)
    }
    Ok(w.into_bytes())
}
pub fn decode_surface_output_state(
    bytes: &[u8],
) -> Result<SurfaceOutputState, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let surface_id = r.read_u64()?;
    let primary_output_id = r.read_u64()?;
    let layout_generation = r.read_u64()?;
    let preferred_scale_numerator = r.read_u32()?;
    let preferred_scale_denominator = r.read_u32()?;
    let client_buffer_scale = r.read_u32()?;
    let scale_mode = r.read_u16()?.try_into()?;
    let reserved = r.read_u16()?;
    let flags = r.read_u32()?;
    let count = r.read_u32()? as usize;
    if count > MAXIMUM_MANAGED_OUTPUTS {
        return Err(ContractDecodeError::LimitExceeded);
    }
    let mut output_ids = Vec::with_capacity(count);
    for _ in 0..count {
        output_ids.push(r.read_u64()?)
    }
    let v = SurfaceOutputState {
        surface_id,
        primary_output_id,
        output_ids,
        preferred_scale_numerator,
        preferred_scale_denominator,
        client_buffer_scale,
        scale_mode,
        layout_generation,
        flags,
    };
    let unique = v
        .output_ids
        .iter()
        .enumerate()
        .all(|(i, id)| *id != 0 && !v.output_ids[..i].contains(id));
    let valid = surface_id != 0
        && primary_output_id != 0
        && unique
        && (v.output_ids.is_empty() || v.output_ids.contains(&primary_output_id))
        && valid_scale(preferred_scale_numerator, preferred_scale_denominator, 120)
        && (1..=4).contains(&client_buffer_scale)
        && !(scale_mode == SurfaceScaleMode::Legacy && client_buffer_scale != 1)
        && layout_generation != 0
        && flags == 0
        && reserved == 0;
    finish(r, valid)?;
    Ok(v)
}

#[must_use]
pub fn encode_policy_output_upsert(v: &PolicyOutputUpsert) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.output_id);
    w.write_i32(v.logical_x);
    w.write_i32(v.logical_y);
    w.write_u32(v.logical_width);
    w.write_u32(v.logical_height);
    w.write_i32(v.work_x);
    w.write_i32(v.work_y);
    w.write_u32(v.work_width);
    w.write_u32(v.work_height);
    w.write_u32(v.scale_numerator);
    w.write_u32(v.scale_denominator);
    w.write_u16(v.transform as u16);
    w.write_u8(u8::from(v.enabled));
    w.write_u8(u8::from(v.primary));
    w.write_u32(v.flags);
    w.into_bytes()
}
pub fn decode_policy_output_upsert(
    bytes: &[u8],
) -> Result<PolicyOutputUpsert, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = PolicyOutputUpsert {
        output_id: r.read_u64()?,
        logical_x: r.read_i32()?,
        logical_y: r.read_i32()?,
        logical_width: r.read_u32()?,
        logical_height: r.read_u32()?,
        work_x: r.read_i32()?,
        work_y: r.read_i32()?,
        work_width: r.read_u32()?,
        work_height: r.read_u32()?,
        scale_numerator: r.read_u32()?,
        scale_denominator: r.read_u32()?,
        transform: r.read_u16()?.try_into()?,
        enabled: read_bool(&mut r)?,
        primary: read_bool(&mut r)?,
        flags: r.read_u32()?,
    };
    let disabled = !v.enabled
        && !v.primary
        && v.logical_x == 0
        && v.logical_y == 0
        && v.logical_width == 0
        && v.logical_height == 0
        && v.work_x == 0
        && v.work_y == 0
        && v.work_width == 0
        && v.work_height == 0;
    let enabled = v.enabled;
    let extents = valid_extent(v.logical_x, v.logical_width)
        && valid_extent(v.logical_y, v.logical_height)
        && valid_extent(v.work_x, v.work_width)
        && valid_extent(v.work_y, v.work_height)
        && v.work_x >= v.logical_x
        && v.work_y >= v.logical_y
        && (v.work_x as u64 + u64::from(v.work_width))
            <= v.logical_x as u64 + u64::from(v.logical_width)
        && (v.work_y as u64 + u64::from(v.work_height))
            <= v.logical_y as u64 + u64::from(v.logical_height);
    finish(
        r,
        v.output_id != 0
            && v.flags == 0
            && valid_scale(v.scale_numerator, v.scale_denominator, 120)
            && (disabled || (enabled && extents)),
    )?;
    Ok(v)
}

#[must_use]
pub fn encode_policy_window_output_hint(v: &PolicyWindowOutputHint) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u32(v.window_id);
    w.write_u32(v.flags);
    w.write_u64(v.previous_output_id);
    w.write_u64(v.preferred_output_id);
    w.into_bytes()
}
pub fn decode_policy_window_output_hint(
    bytes: &[u8],
) -> Result<PolicyWindowOutputHint, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = PolicyWindowOutputHint {
        window_id: r.read_u32()?,
        flags: r.read_u32()?,
        previous_output_id: r.read_u64()?,
        preferred_output_id: r.read_u64()?,
    };
    finish(r, v.window_id != 0 && v.flags == 0)?;
    Ok(v)
}
#[must_use]
pub fn encode_output_state_query(v: &OutputStateQuery) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.query_id);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_output_state_query(bytes: &[u8]) -> Result<OutputStateQuery, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = OutputStateQuery {
        query_id: r.read_u64()?,
        flags: r.read_u32()?,
    };
    let z = r.read_u32()?;
    finish(
        r,
        v.query_id != 0 && v.flags != 0 && (v.flags & !KNOWN_OUTPUT_QUERY_FLAGS) == 0 && z == 0,
    )?;
    Ok(v)
}
#[must_use]
pub fn encode_output_configuration_commit(v: &OutputConfigurationCommit) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.configuration_id);
    w.write_u64(v.base_generation);
    w.write_u64(v.primary_output_id);
    w.write_u32(v.flags);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_output_configuration_commit(
    bytes: &[u8],
) -> Result<OutputConfigurationCommit, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = OutputConfigurationCommit {
        configuration_id: r.read_u64()?,
        base_generation: r.read_u64()?,
        primary_output_id: r.read_u64()?,
        flags: r.read_u32()?,
    };
    let z = r.read_u32()?;
    finish(
        r,
        v.configuration_id != 0
            && v.base_generation != 0
            && v.primary_output_id != 0
            && v.flags == 0
            && z == 0,
    )?;
    Ok(v)
}
#[must_use]
pub fn encode_output_configuration_acknowledged(v: &OutputConfigurationAcknowledged) -> Vec<u8> {
    let mut w = ByteWriter::new();
    w.write_u64(v.request_id);
    w.write_u64(v.applied_generation);
    w.write_u16(v.result as u16);
    w.write_u16(0);
    w.write_u32(v.flags);
    w.write_u64(v.primary_output_id);
    w.write_u32(v.root_logical_width);
    w.write_u32(v.root_logical_height);
    w.write_u32(v.enabled_output_count);
    w.write_u32(0);
    w.into_bytes()
}
pub fn decode_output_configuration_acknowledged(
    bytes: &[u8],
) -> Result<OutputConfigurationAcknowledged, ContractDecodeError> {
    let mut r = ByteReader::new(bytes);
    let v = OutputConfigurationAcknowledged {
        request_id: r.read_u64()?,
        applied_generation: r.read_u64()?,
        result: r.read_u16()?.try_into()?,
        flags: {
            if r.read_u16()? != 0 {
                return Err(ContractDecodeError::InvalidValue);
            }
            r.read_u32()?
        },
        primary_output_id: r.read_u64()?,
        root_logical_width: r.read_u32()?,
        root_logical_height: r.read_u32()?,
        enabled_output_count: r.read_u32()?,
    };
    let z = r.read_u32()?;
    finish(
        r,
        v.request_id != 0
            && v.applied_generation != 0
            && v.flags == 0
            && v.primary_output_id != 0
            && (1..=32767).contains(&v.root_logical_width)
            && (1..=32767).contains(&v.root_logical_height)
            && (1..=MAXIMUM_MANAGED_OUTPUTS as u32).contains(&v.enabled_output_count)
            && z == 0,
    )?;
    Ok(v)
}
