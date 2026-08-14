#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ResourceId(u32);

impl ResourceId {
    pub const fn new(value: u32) -> Self {
        Self(value)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ResourceBase(u32);

impl ResourceBase {
    pub const fn new(value: u32) -> Self {
        Self(value)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ResourceMask(u32);

impl ResourceMask {
    pub const fn new(value: u32) -> Self {
        Self(value)
    }

    pub const fn get(self) -> u32 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ClientResourceRange {
    pub base: ResourceBase,
    pub mask: ResourceMask,
}

impl ClientResourceRange {
    pub const fn new(base: ResourceBase, mask: ResourceMask) -> Self {
        Self { base, mask }
    }

    pub const fn contains(self, id: ResourceId) -> bool {
        let xid = id.get();
        let base = self.base.get();
        let mask = self.mask.get();
        xid != 0 && (base & mask) == 0 && (xid & !mask) == base
    }

    pub fn permits_new(
        self,
        id: ResourceId,
        server_ids: ServerOwnedIds,
        already_exists: bool,
    ) -> bool {
        self.contains(id) && !server_ids.contains(id) && !already_exists
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ServerOwnedIds {
    pub root_window: ResourceId,
    pub default_colormap: ResourceId,
    pub root_visual: ResourceId,
}

impl ServerOwnedIds {
    pub const DEFAULT: Self = Self {
        root_window: ResourceId::new(1),
        default_colormap: ResourceId::new(2),
        root_visual: ResourceId::new(3),
    };

    pub const fn contains(self, id: ResourceId) -> bool {
        let xid = id.get();
        id.get() == self.root_window.get()
            || id.get() == self.default_colormap.get()
            || id.get() == self.root_visual.get()
            || (xid >= 0x1fff_f100 && xid <= 0x1fff_f1ff)
    }
}

impl Default for ServerOwnedIds {
    fn default() -> Self {
        Self::DEFAULT
    }
}

pub const FIRST_CLIENT_RESOURCE_BASE: ResourceBase = ResourceBase::new(0x0020_0000);
pub const LAST_CLIENT_RESOURCE_BASE: ResourceBase = ResourceBase::new(0xffe0_0000);
pub const CLIENT_RESOURCE_BASE_STRIDE: u32 = 0x0020_0000;

pub fn first_available_resource_base(
    mut in_use: impl FnMut(ResourceBase) -> bool,
) -> Option<ResourceBase> {
    let mut candidate = u64::from(FIRST_CLIENT_RESOURCE_BASE.get());
    let last = u64::from(LAST_CLIENT_RESOURCE_BASE.get());
    let stride = u64::from(CLIENT_RESOURCE_BASE_STRIDE);
    while candidate <= last {
        let base = ResourceBase::new(candidate as u32);
        if !in_use(base) {
            return Some(base);
        }
        candidate += stride;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn client_range_matches_the_legacy_predicate() {
        let range = ClientResourceRange::new(
            ResourceBase::new(0x0040_0000),
            ResourceMask::new(0x001f_ffff),
        );
        assert!(range.contains(ResourceId::new(0x0040_0000)));
        assert!(range.contains(ResourceId::new(0x005f_ffff)));
        assert!(!range.contains(ResourceId::new(0)));
        assert!(!range.contains(ResourceId::new(0x0080_0001)));

        let invalid = ClientResourceRange::new(
            ResourceBase::new(0x0040_0001),
            ResourceMask::new(0x001f_ffff),
        );
        assert!(!invalid.contains(ResourceId::new(0x0040_0001)));
    }

    #[test]
    fn new_ids_exclude_server_and_existing_resources() {
        let range = ClientResourceRange::new(ResourceBase::new(0), ResourceMask::new(u32::MAX));
        let server = ServerOwnedIds::DEFAULT;
        assert!(!range.permits_new(ResourceId::new(1), server, false));
        assert!(!range.permits_new(ResourceId::new(2), server, false));
        assert!(!range.permits_new(ResourceId::new(3), server, false));
        assert!(!range.permits_new(ResourceId::new(0x1fff_f100), server, false));
        assert!(!range.permits_new(ResourceId::new(0x1fff_f1ff), server, false));
        assert!(range.permits_new(ResourceId::new(0x1fff_f200), server, false));
        assert!(!range.permits_new(ResourceId::new(0x1fff_f200), server, true));
    }

    #[test]
    fn base_allocation_uses_the_legacy_range_and_stride() {
        assert_eq!(
            first_available_resource_base(|_| false),
            Some(FIRST_CLIENT_RESOURCE_BASE)
        );
        let used = HashSet::from([
            FIRST_CLIENT_RESOURCE_BASE,
            ResourceBase::new(FIRST_CLIENT_RESOURCE_BASE.get() + CLIENT_RESOURCE_BASE_STRIDE),
        ]);
        assert_eq!(
            first_available_resource_base(|base| used.contains(&base)),
            Some(ResourceBase::new(0x0060_0000))
        );
        assert_eq!(
            first_available_resource_base(|base| base != LAST_CLIENT_RESOURCE_BASE),
            Some(LAST_CLIENT_RESOURCE_BASE)
        );
        assert_eq!(first_available_resource_base(|_| true), None);
    }
}
