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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExtensionCapability(u32);

impl ExtensionCapability {
    pub const NONE: Self = Self(0);
    pub const GAME_COMPAT: Self = Self(1 << 0);
    pub const SCALE_PROTOCOL: Self = Self(1 << 1);
    pub const VRR_PROTOCOL: Self = Self(1 << 2);

    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    pub const fn contains_any(self, required: Self) -> bool {
        self.0 & required.0 != 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ExtensionDescriptor {
    pub name: &'static str,
    pub kind: ExtensionKind,
    pub capability: ExtensionCapability,
    pub major_opcode: u8,
    pub first_event: u8,
    pub event_count: u8,
    pub first_error: u8,
    pub error_count: u8,
    pub maximum_major_version: u16,
    pub maximum_minor_version: u16,
}

pub const EXTENSIONS: [ExtensionDescriptor; 9] = [
    ExtensionDescriptor {
        name: "BIG-REQUESTS",
        kind: ExtensionKind::BigRequests,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 128,
        first_event: 0,
        event_count: 0,
        first_error: 0,
        error_count: 0,
        maximum_major_version: 1,
        maximum_minor_version: 0,
    },
    ExtensionDescriptor {
        name: "MIT-SHM",
        kind: ExtensionKind::MitShm,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 129,
        first_event: 64,
        event_count: 1,
        first_error: 128,
        error_count: 1,
        maximum_major_version: 1,
        maximum_minor_version: 1,
    },
    ExtensionDescriptor {
        name: "XFIXES",
        kind: ExtensionKind::XFixes,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 130,
        first_event: 65,
        event_count: 1,
        first_error: 129,
        error_count: 1,
        maximum_major_version: 2,
        maximum_minor_version: 0,
    },
    ExtensionDescriptor {
        name: "DAMAGE",
        kind: ExtensionKind::Damage,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 131,
        first_event: 66,
        event_count: 1,
        first_error: 130,
        error_count: 1,
        maximum_major_version: 1,
        maximum_minor_version: 1,
    },
    ExtensionDescriptor {
        name: "RENDER",
        kind: ExtensionKind::Render,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 132,
        first_event: 0,
        event_count: 0,
        first_error: 131,
        error_count: 5,
        maximum_major_version: 0,
        maximum_minor_version: 11,
    },
    ExtensionDescriptor {
        name: "Composite",
        kind: ExtensionKind::Composite,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 133,
        first_event: 0,
        event_count: 0,
        first_error: 0,
        error_count: 0,
        maximum_major_version: 0,
        maximum_minor_version: 4,
    },
    ExtensionDescriptor {
        name: "RANDR",
        kind: ExtensionKind::RandR,
        capability: ExtensionCapability::GAME_COMPAT,
        major_opcode: 134,
        first_event: 67,
        event_count: 2,
        first_error: 136,
        error_count: 3,
        maximum_major_version: 1,
        maximum_minor_version: 3,
    },
    ExtensionDescriptor {
        name: "GW_SCALE",
        kind: ExtensionKind::GwScale,
        capability: ExtensionCapability::SCALE_PROTOCOL,
        major_opcode: 135,
        first_event: 69,
        event_count: 1,
        first_error: 139,
        error_count: 2,
        maximum_major_version: 0,
        maximum_minor_version: 1,
    },
    ExtensionDescriptor {
        name: "GW_VRR",
        kind: ExtensionKind::GwVrr,
        capability: ExtensionCapability::VRR_PROTOCOL,
        major_opcode: 136,
        first_event: 70,
        event_count: 1,
        first_error: 141,
        error_count: 2,
        maximum_major_version: 0,
        maximum_minor_version: 1,
    },
];

pub const fn extension_ranges_are_valid() -> bool {
    let mut left = 0;
    while left < EXTENSIONS.len() {
        let a = &EXTENSIONS[left];
        if left != 0 && a.major_opcode <= EXTENSIONS[left - 1].major_opcode {
            return false;
        }
        let mut right = left + 1;
        while right < EXTENSIONS.len() {
            let b = &EXTENSIONS[right];
            let events_overlap = a.event_count != 0
                && b.event_count != 0
                && (a.first_event as u16) < b.first_event as u16 + b.event_count as u16
                && (b.first_event as u16) < a.first_event as u16 + a.event_count as u16;
            let errors_overlap = a.error_count != 0
                && b.error_count != 0
                && (a.first_error as u16) < b.first_error as u16 + b.error_count as u16
                && (b.first_error as u16) < a.first_error as u16 + a.error_count as u16;
            if events_overlap || errors_overlap {
                return false;
            }
            right += 1;
        }
        left += 1;
    }
    true
}

const _: () = assert!(extension_ranges_are_valid());

pub fn find_by_name(name: &str) -> Option<&'static ExtensionDescriptor> {
    EXTENSIONS.iter().find(|extension| extension.name == name)
}

pub fn find_by_opcode(major_opcode: u8) -> Option<&'static ExtensionDescriptor> {
    EXTENSIONS
        .iter()
        .find(|extension| extension.major_opcode == major_opcode)
}

pub fn find_by_kind(kind: ExtensionKind) -> Option<&'static ExtensionDescriptor> {
    EXTENSIONS.iter().find(|extension| extension.kind == kind)
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExtensionRegistry {
    capabilities: ExtensionCapability,
    disabled: [bool; EXTENSIONS.len()],
}

impl ExtensionRegistry {
    pub fn new(capabilities: ExtensionCapability, disabled_names: &[&str]) -> Self {
        let mut disabled = [false; EXTENSIONS.len()];
        for name in disabled_names {
            if let Some(index) = EXTENSIONS
                .iter()
                .position(|extension| extension.name == *name)
            {
                disabled[index] = true;
            }
        }
        Self {
            capabilities,
            disabled,
        }
    }

    pub fn game_compat(enabled: bool, disabled_names: &[&str]) -> Self {
        Self::new(
            if enabled {
                ExtensionCapability::GAME_COMPAT
            } else {
                ExtensionCapability::NONE
            },
            disabled_names,
        )
    }

    pub const fn profile_enabled(&self, capability: ExtensionCapability) -> bool {
        self.capabilities.contains_any(capability)
    }

    pub fn query_name(&self, name: &str) -> Option<&'static ExtensionDescriptor> {
        self.query(find_by_name(name)?)
    }

    pub fn query_opcode(&self, major_opcode: u8) -> Option<&'static ExtensionDescriptor> {
        self.query(find_by_opcode(major_opcode)?)
    }

    pub fn enabled_names(&self) -> Vec<&'static str> {
        EXTENSIONS
            .iter()
            .enumerate()
            .filter(|(index, extension)| {
                !self.disabled[*index] && self.capabilities.contains_any(extension.capability)
            })
            .map(|(_, extension)| extension.name)
            .collect()
    }

    fn query(
        &self,
        extension: &'static ExtensionDescriptor,
    ) -> Option<&'static ExtensionDescriptor> {
        let index = EXTENSIONS
            .iter()
            .position(|candidate| candidate.kind == extension.kind)?;
        (!self.disabled[index] && self.capabilities.contains_any(extension.capability))
            .then_some(extension)
    }
}

impl Default for ExtensionRegistry {
    fn default() -> Self {
        Self::new(ExtensionCapability::NONE, &[])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn descriptor_table_matches_the_legacy_assignments() {
        assert!(extension_ranges_are_valid());
        assert_eq!(EXTENSIONS.len(), 9);
        assert_eq!(find_by_name("BIG-REQUESTS").unwrap().major_opcode, 128);
        assert_eq!(find_by_opcode(136).unwrap().kind, ExtensionKind::GwVrr);
        assert_eq!(find_by_kind(ExtensionKind::RandR).unwrap().first_event, 67);
        assert!(find_by_name("render").is_none());
        assert!(find_by_name("COMPOSITE").is_none());
        assert_eq!(find_by_name("Composite").unwrap().major_opcode, 133);
    }

    #[test]
    fn registry_applies_profiles_disables_and_order() {
        let game = ExtensionRegistry::game_compat(true, &["MIT-SHM", "unknown"]);
        assert!(game.query_name("BIG-REQUESTS").is_some());
        assert!(game.query_name("MIT-SHM").is_none());
        assert!(game.query_name("GW_SCALE").is_none());
        assert_eq!(
            game.enabled_names(),
            vec![
                "BIG-REQUESTS",
                "XFIXES",
                "DAMAGE",
                "RENDER",
                "Composite",
                "RANDR"
            ]
        );

        let modern = ExtensionRegistry::new(
            ExtensionCapability::SCALE_PROTOCOL.union(ExtensionCapability::VRR_PROTOCOL),
            &[],
        );
        assert!(modern.query_name("BIG-REQUESTS").is_none());
        assert_eq!(modern.enabled_names(), vec!["GW_SCALE", "GW_VRR"]);
        assert!(modern.profile_enabled(ExtensionCapability::SCALE_PROTOCOL));
    }
}
