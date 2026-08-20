#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExtensionKind {
    BigRequests,
    MitShm,
    XFixes,
    Damage,
    Render,
    Composite,
    RandR,
    GwScale,
    GwVrr,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ExtensionProfiles(u32);

impl ExtensionProfiles {
    pub const NONE: Self = Self(0);
    pub const GAME_COMPAT: Self = Self(1 << 0);
    pub const SCALE_PROTOCOL: Self = Self(1 << 1);
    pub const VRR_PROTOCOL: Self = Self(1 << 2);

    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    #[must_use]
    pub const fn contains_any(self, required: Self) -> bool {
        self.0 & required.0 != 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExtensionAssignment {
    pub major_opcode: u8,
    pub first_event: u8,
    pub first_error: u8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExtensionDescriptor {
    pub name: &'static [u8],
    pub kind: ExtensionKind,
    pub required_profile: ExtensionProfiles,
    pub assignment: ExtensionAssignment,
    pub event_count: u8,
    pub error_count: u8,
    pub maximum_major_version: u16,
    pub maximum_minor_version: u16,
}

impl ExtensionDescriptor {
    #[allow(clippy::too_many_arguments)]
    pub const fn new(
        name: &'static [u8],
        kind: ExtensionKind,
        required_profile: ExtensionProfiles,
        major_opcode: u8,
        first_event: u8,
        event_count: u8,
        first_error: u8,
        error_count: u8,
        maximum_major_version: u16,
        maximum_minor_version: u16,
    ) -> Self {
        Self {
            name,
            kind,
            required_profile,
            assignment: ExtensionAssignment {
                major_opcode,
                first_event,
                first_error,
            },
            event_count,
            error_count,
            maximum_major_version,
            maximum_minor_version,
        }
    }
}

pub const EXTENSION_REGISTRY: [ExtensionDescriptor; 9] = [
    ExtensionDescriptor::new(
        b"BIG-REQUESTS",
        ExtensionKind::BigRequests,
        ExtensionProfiles::GAME_COMPAT,
        128,
        0,
        0,
        0,
        0,
        1,
        0,
    ),
    ExtensionDescriptor::new(
        b"MIT-SHM",
        ExtensionKind::MitShm,
        ExtensionProfiles::GAME_COMPAT,
        129,
        64,
        1,
        128,
        1,
        1,
        1,
    ),
    ExtensionDescriptor::new(
        b"XFIXES",
        ExtensionKind::XFixes,
        ExtensionProfiles::GAME_COMPAT,
        130,
        65,
        1,
        129,
        1,
        2,
        0,
    ),
    ExtensionDescriptor::new(
        b"DAMAGE",
        ExtensionKind::Damage,
        ExtensionProfiles::GAME_COMPAT,
        131,
        66,
        1,
        130,
        1,
        1,
        1,
    ),
    ExtensionDescriptor::new(
        b"RENDER",
        ExtensionKind::Render,
        ExtensionProfiles::GAME_COMPAT,
        132,
        0,
        0,
        131,
        5,
        0,
        11,
    ),
    ExtensionDescriptor::new(
        b"Composite",
        ExtensionKind::Composite,
        ExtensionProfiles::GAME_COMPAT,
        133,
        0,
        0,
        0,
        0,
        0,
        4,
    ),
    ExtensionDescriptor::new(
        b"RANDR",
        ExtensionKind::RandR,
        ExtensionProfiles::GAME_COMPAT,
        134,
        67,
        2,
        136,
        3,
        1,
        3,
    ),
    ExtensionDescriptor::new(
        b"GW_SCALE",
        ExtensionKind::GwScale,
        ExtensionProfiles::SCALE_PROTOCOL,
        135,
        69,
        1,
        139,
        2,
        0,
        1,
    ),
    ExtensionDescriptor::new(
        b"GW_VRR",
        ExtensionKind::GwVrr,
        ExtensionProfiles::VRR_PROTOCOL,
        136,
        70,
        1,
        141,
        2,
        0,
        1,
    ),
];

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExtensionRegistry {
    profiles: ExtensionProfiles,
    disabled: [bool; EXTENSION_REGISTRY.len()],
}

impl Default for ExtensionRegistry {
    fn default() -> Self {
        Self::new(ExtensionProfiles::NONE)
    }
}

impl ExtensionRegistry {
    #[must_use]
    pub const fn new(profiles: ExtensionProfiles) -> Self {
        Self {
            profiles,
            disabled: [false; EXTENSION_REGISTRY.len()],
        }
    }

    pub fn disable(&mut self, name: &[u8]) -> bool {
        let Some(index) = EXTENSION_REGISTRY
            .iter()
            .position(|descriptor| descriptor.name == name)
        else {
            return false;
        };
        self.disabled[index] = true;
        true
    }

    #[must_use]
    pub fn query(&self, name: &[u8]) -> Option<&'static ExtensionDescriptor> {
        EXTENSION_REGISTRY
            .iter()
            .enumerate()
            .find(|(index, descriptor)| {
                !self.disabled[*index]
                    && self.profiles.contains_any(descriptor.required_profile)
                    && descriptor.name == name
            })
            .map(|(_, descriptor)| descriptor)
    }

    #[must_use]
    pub fn query_opcode(&self, major_opcode: u8) -> Option<&'static ExtensionDescriptor> {
        EXTENSION_REGISTRY
            .iter()
            .enumerate()
            .find(|(index, descriptor)| {
                !self.disabled[*index]
                    && self.profiles.contains_any(descriptor.required_profile)
                    && descriptor.assignment.major_opcode == major_opcode
            })
            .map(|(_, descriptor)| descriptor)
    }

    #[must_use]
    pub fn enabled_names(&self) -> Vec<&'static [u8]> {
        EXTENSION_REGISTRY
            .iter()
            .enumerate()
            .filter(|(index, descriptor)| {
                !self.disabled[*index] && self.profiles.contains_any(descriptor.required_profile)
            })
            .map(|(_, descriptor)| descriptor.name)
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_registry_ranges_are_nonoverlapping_and_monotonic() {
        for pair in EXTENSION_REGISTRY.windows(2) {
            assert!(pair[0].assignment.major_opcode < pair[1].assignment.major_opcode);
        }
        for (left_index, left) in EXTENSION_REGISTRY.iter().enumerate() {
            for right in &EXTENSION_REGISTRY[left_index + 1..] {
                let events_overlap = left.event_count != 0
                    && right.event_count != 0
                    && left.assignment.first_event
                        < right
                            .assignment
                            .first_event
                            .saturating_add(right.event_count)
                    && right.assignment.first_event
                        < left.assignment.first_event.saturating_add(left.event_count);
                let errors_overlap = left.error_count != 0
                    && right.error_count != 0
                    && left.assignment.first_error
                        < right
                            .assignment
                            .first_error
                            .saturating_add(right.error_count)
                    && right.assignment.first_error
                        < left.assignment.first_error.saturating_add(left.error_count);
                assert!(
                    !events_overlap,
                    "event range overlap: {:?} {:?}",
                    left.kind, right.kind
                );
                assert!(
                    !errors_overlap,
                    "error range overlap: {:?} {:?}",
                    left.kind, right.kind
                );
            }
        }
    }

    #[test]
    fn profiles_and_disable_rules_match_legacy_visibility() {
        let disabled = ExtensionRegistry::default();
        assert!(disabled.enabled_names().is_empty());
        assert!(disabled.query(b"BIG-REQUESTS").is_none());

        let mut game = ExtensionRegistry::new(ExtensionProfiles::GAME_COMPAT);
        assert_eq!(game.enabled_names().len(), 7);
        assert_eq!(
            game.query(b"BIG-REQUESTS").unwrap().assignment.major_opcode,
            128
        );
        assert_eq!(game.query_opcode(134).unwrap().name, b"RANDR");
        assert!(game.query(b"GW_SCALE").is_none());
        assert!(game.disable(b"RENDER"));
        assert!(game.query(b"RENDER").is_none());
        assert!(!game.disable(b"not-an-extension"));

        let custom = ExtensionRegistry::new(
            ExtensionProfiles::SCALE_PROTOCOL.union(ExtensionProfiles::VRR_PROTOCOL),
        );
        assert_eq!(
            custom.enabled_names(),
            [b"GW_SCALE".as_slice(), b"GW_VRR".as_slice()]
        );
    }
}
