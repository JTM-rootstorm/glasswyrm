use std::collections::BTreeMap;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct AtomId(u32);

impl AtomId {
    pub const fn new(value: u32) -> Self {
        Self(value)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PropertyData {
    U8(Vec<u8>),
    U16(Vec<u16>),
    U32(Vec<u32>),
}

impl PropertyData {
    pub const fn format(&self) -> u8 {
        match self {
            Self::U8(_) => 8,
            Self::U16(_) => 16,
            Self::U32(_) => 32,
        }
    }

    pub fn item_count(&self) -> usize {
        match self {
            Self::U8(values) => values.len(),
            Self::U16(values) => values.len(),
            Self::U32(values) => values.len(),
        }
    }

    pub fn byte_size(&self) -> usize {
        self.item_count() * usize::from(self.format() / 8)
    }

    fn empty_like(&self) -> Self {
        match self {
            Self::U8(_) => Self::U8(Vec::new()),
            Self::U16(_) => Self::U16(Vec::new()),
            Self::U32(_) => Self::U32(Vec::new()),
        }
    }

    fn slice_bytes(&self, byte_offset: usize, byte_length: usize) -> Self {
        macro_rules! slice {
            ($values:expr, $variant:ident, $width:expr) => {{
                let first = byte_offset / $width;
                let count = byte_length / $width;
                let last = first.saturating_add(count).min($values.len());
                if first >= $values.len() {
                    Self::$variant(Vec::new())
                } else {
                    Self::$variant($values[first..last].to_vec())
                }
            }};
        }

        match self {
            Self::U8(values) => slice!(values, U8, 1),
            Self::U16(values) => slice!(values, U16, 2),
            Self::U32(values) => slice!(values, U32, 4),
        }
    }

    fn concatenate(first: &Self, second: &Self) -> Option<Self> {
        macro_rules! concatenate {
            ($left:expr, $right:expr, $variant:ident) => {{
                let mut values = Vec::new();
                values
                    .try_reserve_exact($left.len().checked_add($right.len())?)
                    .ok()?;
                values.extend_from_slice($left);
                values.extend_from_slice($right);
                Some(Self::$variant(values))
            }};
        }

        match (first, second) {
            (Self::U8(left), Self::U8(right)) => concatenate!(left, right, U8),
            (Self::U16(left), Self::U16(right)) => concatenate!(left, right, U16),
            (Self::U32(left), Self::U32(right)) => concatenate!(left, right, U32),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Property {
    pub property_type: AtomId,
    pub data: PropertyData,
}

impl Property {
    pub fn format(&self) -> u8 {
        self.data.format()
    }

    pub fn item_count(&self) -> usize {
        self.data.item_count()
    }

    pub fn byte_size(&self) -> usize {
        self.data.byte_size()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyMode {
    Replace,
    Prepend,
    Append,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PropertySlice {
    pub property_type: AtomId,
    pub format: u8,
    pub bytes_after: u32,
    pub data: PropertyData,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyMutationStatus {
    Success,
    BadWindow,
    BadMatch,
    BadAlloc,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PropertyReadStatus {
    Success,
    BadWindow,
    BadValue,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PropertyReadResult {
    pub status: PropertyReadStatus,
    pub present: bool,
    pub type_matched: bool,
    pub deleted: bool,
    pub value: Option<PropertySlice>,
}

impl PropertyReadResult {
    pub(crate) const fn absent() -> Self {
        Self {
            status: PropertyReadStatus::Success,
            present: false,
            type_matched: false,
            deleted: false,
            value: None,
        }
    }

    pub(crate) const fn error(status: PropertyReadStatus) -> Self {
        Self {
            status,
            present: false,
            type_matched: false,
            deleted: false,
            value: None,
        }
    }
}

pub const MAXIMUM_BYTES_PER_PROPERTY: usize = 4 * 1024 * 1024;
pub const MAXIMUM_TOTAL_PROPERTY_BYTES: usize = 64 * 1024 * 1024;
pub const MAXIMUM_PROPERTIES_PER_WINDOW: usize = 4096;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PropertyLimits {
    pub maximum_bytes_per_property: usize,
    pub maximum_total_property_bytes: usize,
    pub maximum_properties_per_window: usize,
}

impl Default for PropertyLimits {
    fn default() -> Self {
        Self {
            maximum_bytes_per_property: MAXIMUM_BYTES_PER_PROPERTY,
            maximum_total_property_bytes: MAXIMUM_TOTAL_PROPERTY_BYTES,
            maximum_properties_per_window: MAXIMUM_PROPERTIES_PER_WINDOW,
        }
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub(crate) struct PropertyStore {
    values: BTreeMap<AtomId, Property>,
}

impl PropertyStore {
    pub(crate) fn get(&self, atom: AtomId) -> Option<&Property> {
        self.values.get(&atom)
    }

    pub(crate) fn len(&self) -> usize {
        self.values.len()
    }

    pub(crate) fn atoms(&self) -> Vec<AtomId> {
        self.values.keys().copied().collect()
    }

    pub(crate) fn byte_size(&self) -> usize {
        self.values.values().map(Property::byte_size).sum()
    }

    pub(crate) fn change(
        &mut self,
        atom: AtomId,
        value: Property,
        mode: PropertyMode,
        limits: PropertyLimits,
        current_total: usize,
    ) -> PropertyMutationStatus {
        let current = self.values.get(&atom);
        if let Some(current) = current
            && mode != PropertyMode::Replace
            && (current.property_type != value.property_type || current.format() != value.format())
        {
            return PropertyMutationStatus::BadMatch;
        }

        let old_size = current.map_or(0, Property::byte_size);
        let prospective_size = match (current, mode) {
            (Some(current), PropertyMode::Append | PropertyMode::Prepend) => {
                match current.byte_size().checked_add(value.byte_size()) {
                    Some(size) => size,
                    None => return PropertyMutationStatus::BadAlloc,
                }
            }
            _ => value.byte_size(),
        };
        let retained_total = match current_total.checked_sub(old_size) {
            Some(size) => size,
            None => return PropertyMutationStatus::BadAlloc,
        };
        let prospective_total = match retained_total.checked_add(prospective_size) {
            Some(size) => size,
            None => return PropertyMutationStatus::BadAlloc,
        };
        if prospective_size > limits.maximum_bytes_per_property
            || (current.is_none() && self.len() >= limits.maximum_properties_per_window)
            || prospective_total > limits.maximum_total_property_bytes
        {
            return PropertyMutationStatus::BadAlloc;
        }

        let replacement = match (current, mode) {
            (Some(current), PropertyMode::Append) => Property {
                property_type: value.property_type,
                data: match PropertyData::concatenate(&current.data, &value.data) {
                    Some(data) => data,
                    None => return PropertyMutationStatus::BadAlloc,
                },
            },
            (Some(current), PropertyMode::Prepend) => Property {
                property_type: value.property_type,
                data: match PropertyData::concatenate(&value.data, &current.data) {
                    Some(data) => data,
                    None => return PropertyMutationStatus::BadAlloc,
                },
            },
            _ => value,
        };
        self.values.insert(atom, replacement);
        PropertyMutationStatus::Success
    }

    pub(crate) fn delete(&mut self, atom: AtomId) -> usize {
        self.values
            .remove(&atom)
            .map_or(0, |value| value.byte_size())
    }

    pub(crate) fn read(
        &mut self,
        atom: AtomId,
        requested_type: Option<AtomId>,
        delete_after_read: bool,
        long_offset: u32,
        long_length: u32,
    ) -> PropertyReadResult {
        let Some(property) = self.values.get(&atom) else {
            return PropertyReadResult::absent();
        };
        let byte_size = property.byte_size();
        let offset64 = u64::from(long_offset) * 4;
        if offset64 > byte_size as u64 {
            return PropertyReadResult::error(PropertyReadStatus::BadValue);
        }

        let type_matched = requested_type.is_none_or(|value| value == property.property_type);
        let mut result = PropertyReadResult {
            status: PropertyReadStatus::Success,
            present: true,
            type_matched,
            deleted: false,
            value: Some(PropertySlice {
                property_type: property.property_type,
                format: property.format(),
                bytes_after: 0,
                data: property.data.empty_like(),
            }),
        };
        if !type_matched {
            result.value.as_mut().expect("slice exists").bytes_after = byte_size as u32;
            return result;
        }

        let offset = offset64 as usize;
        let requested = u64::from(long_length) * 4;
        let available = byte_size - offset;
        let returned = available.min(requested as usize);
        let slice = result.value.as_mut().expect("slice exists");
        slice.bytes_after = (available - returned) as u32;
        slice.data = property.data.slice_bytes(offset, returned);
        if delete_after_read && slice.bytes_after == 0 {
            self.values.remove(&atom);
            result.deleted = true;
        }
        result
    }
}
