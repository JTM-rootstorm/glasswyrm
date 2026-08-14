use std::collections::{BTreeMap, BTreeSet};

use crate::property::{
    AtomId, Property, PropertyLimits, PropertyMode, PropertyMutationStatus, PropertyReadResult,
    PropertyReadStatus, PropertyStore,
};
use crate::resource_id::{ClientResourceRange, ResourceId, ServerOwnedIds};

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ClientId(u64);

impl ClientId {
    pub const fn new(value: u64) -> Self {
        Self(value)
    }

    pub const fn get(self) -> u64 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct WindowId(ResourceId);

impl WindowId {
    pub const fn new(value: u32) -> Self {
        Self(ResourceId::new(value))
    }

    pub const fn get(self) -> u32 {
        self.0.get()
    }

    pub const fn resource_id(self) -> ResourceId {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum WindowClass {
    CopyFromParent = 0,
    InputOutput = 1,
    InputOnly = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MapState {
    Unmapped = 0,
    Unviewable = 1,
    Viewable = 2,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WindowAttributes {
    pub override_redirect: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WindowGeometry {
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub border_width: u16,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WindowCreateSpec {
    pub xid: WindowId,
    pub parent: WindowId,
    pub geometry: WindowGeometry,
    pub depth: u8,
    pub window_class: WindowClass,
    pub visual: u32,
    pub attribute_mask: u32,
    pub attributes: WindowAttributes,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ScreenModel {
    pub root_window: WindowId,
    pub root_width: u16,
    pub root_height: u16,
    pub root_depth: u8,
    pub root_visual: u32,
}

impl Default for ScreenModel {
    fn default() -> Self {
        Self {
            root_window: WindowId::new(1),
            root_width: 1280,
            root_height: 720,
            root_depth: 24,
            root_visual: 3,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Window {
    owner: Option<ClientId>,
    parent: Option<WindowId>,
    children: Vec<WindowId>,
    geometry: WindowGeometry,
    requested_geometry: WindowGeometry,
    depth: u8,
    window_class: WindowClass,
    visual: u32,
    attributes: WindowAttributes,
    map_state: MapState,
    map_requested: bool,
    policy_visible: bool,
    properties: PropertyStore,
}

impl Window {
    pub const fn owner(&self) -> Option<ClientId> {
        self.owner
    }

    pub const fn parent(&self) -> Option<WindowId> {
        self.parent
    }

    pub fn children(&self) -> &[WindowId] {
        &self.children
    }

    pub const fn geometry(&self) -> WindowGeometry {
        self.geometry
    }

    pub const fn requested_geometry(&self) -> WindowGeometry {
        self.requested_geometry
    }

    pub const fn depth(&self) -> u8 {
        self.depth
    }

    pub const fn window_class(&self) -> WindowClass {
        self.window_class
    }

    pub const fn visual(&self) -> u32 {
        self.visual
    }

    pub const fn attributes(&self) -> WindowAttributes {
        self.attributes
    }

    pub const fn map_state(&self) -> MapState {
        self.map_state
    }

    pub const fn map_requested(&self) -> bool {
        self.map_requested
    }

    pub const fn policy_visible(&self) -> bool {
        self.policy_visible
    }

    pub fn property(&self, atom: AtomId) -> Option<&Property> {
        self.properties.get(atom)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CreateWindowStatus {
    Success,
    BadIdChoice,
    BadWindow,
    BadValue,
    BadMatch,
    BadAlloc,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DestroyWindowStatus {
    Success,
    BadWindow,
    RootPreserved,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DestroyWindowResult {
    pub status: DestroyWindowStatus,
    pub destroyed: Vec<WindowId>,
    pub property_bytes_released: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LifecycleStatus {
    Success,
    BadWindow,
    BadMatch,
    BadValue,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StackMode {
    None,
    Above,
    Below,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ConfigureWindow {
    pub x: Option<i32>,
    pub y: Option<i32>,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub border_width: Option<u32>,
    pub sibling: Option<WindowId>,
    pub stack_mode: StackMode,
}

impl Default for ConfigureWindow {
    fn default() -> Self {
        Self {
            x: None,
            y: None,
            width: None,
            height: None,
            border_width: None,
            sibling: None,
            stack_mode: StackMode::None,
        }
    }
}

#[derive(Clone, Debug)]
pub struct WindowStore {
    screen: ScreenModel,
    server_ids: ServerOwnedIds,
    property_limits: PropertyLimits,
    windows: BTreeMap<WindowId, Window>,
    ids_by_owner: BTreeMap<ClientId, BTreeSet<WindowId>>,
    total_property_bytes: usize,
}

impl WindowStore {
    pub fn new(screen: ScreenModel, property_limits: PropertyLimits) -> Self {
        let root = Window {
            owner: None,
            parent: None,
            children: Vec::new(),
            geometry: WindowGeometry {
                x: 0,
                y: 0,
                width: screen.root_width,
                height: screen.root_height,
                border_width: 0,
            },
            requested_geometry: WindowGeometry {
                x: 0,
                y: 0,
                width: screen.root_width,
                height: screen.root_height,
                border_width: 0,
            },
            depth: screen.root_depth,
            window_class: WindowClass::InputOutput,
            visual: screen.root_visual,
            attributes: WindowAttributes::default(),
            map_state: MapState::Viewable,
            map_requested: false,
            policy_visible: false,
            properties: PropertyStore::default(),
        };
        Self {
            screen,
            server_ids: ServerOwnedIds {
                root_window: screen.root_window.resource_id(),
                default_colormap: ResourceId::new(2),
                root_visual: ResourceId::new(screen.root_visual),
            },
            property_limits,
            windows: BTreeMap::from([(screen.root_window, root)]),
            ids_by_owner: BTreeMap::new(),
            total_property_bytes: 0,
        }
    }

    pub fn with_default_screen() -> Self {
        Self::new(ScreenModel::default(), PropertyLimits::default())
    }

    pub const fn screen(&self) -> ScreenModel {
        self.screen
    }

    pub fn window(&self, xid: WindowId) -> Option<&Window> {
        self.windows.get(&xid)
    }

    pub fn window_count(&self) -> usize {
        self.windows.len()
    }

    pub fn window_count_by_owner(&self, owner: ClientId) -> usize {
        self.ids_by_owner.get(&owner).map_or(0, BTreeSet::len)
    }

    pub const fn total_property_bytes(&self) -> usize {
        self.total_property_bytes
    }

    pub fn is_policy_candidate(&self, xid: WindowId) -> bool {
        xid.get() != 4
            && self.window(xid).is_some_and(|window| {
                window.parent == Some(self.screen.root_window)
                    && window.window_class == WindowClass::InputOutput
            })
    }

    pub fn create_window(
        &mut self,
        owner: ClientId,
        range: ClientResourceRange,
        spec: WindowCreateSpec,
    ) -> CreateWindowStatus {
        if !range.permits_new(
            spec.xid.resource_id(),
            self.server_ids,
            self.windows.contains_key(&spec.xid),
        ) {
            return CreateWindowStatus::BadIdChoice;
        }
        let Some(parent) = self.window(spec.parent) else {
            return CreateWindowStatus::BadWindow;
        };
        if spec.geometry.width == 0 || spec.geometry.height == 0 {
            return CreateWindowStatus::BadValue;
        }

        let window_class = match spec.window_class {
            WindowClass::CopyFromParent => parent.window_class,
            WindowClass::InputOutput | WindowClass::InputOnly => spec.window_class,
        };
        let (depth, visual) = if window_class == WindowClass::InputOnly {
            const INPUT_ONLY_ATTRIBUTES: u32 =
                (1 << 5) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 14);
            if spec.depth != 0
                || spec.visual != 0
                || spec.geometry.border_width != 0
                || spec.attribute_mask & !INPUT_ONLY_ATTRIBUTES != 0
            {
                return CreateWindowStatus::BadMatch;
            }
            (0, 0)
        } else {
            if parent.window_class == WindowClass::InputOnly {
                return CreateWindowStatus::BadMatch;
            }
            let depth = if spec.depth == 0 {
                parent.depth
            } else {
                spec.depth
            };
            let visual = if spec.visual == 0 {
                parent.visual
            } else {
                spec.visual
            };
            if depth != self.screen.root_depth || visual != self.screen.root_visual {
                return CreateWindowStatus::BadMatch;
            }
            (depth, visual)
        };

        let window = Window {
            owner: Some(owner),
            parent: Some(spec.parent),
            children: Vec::new(),
            geometry: spec.geometry,
            requested_geometry: spec.geometry,
            depth,
            window_class,
            visual,
            attributes: spec.attributes,
            map_state: MapState::Unmapped,
            map_requested: false,
            policy_visible: false,
            properties: PropertyStore::default(),
        };
        self.windows.insert(spec.xid, window);
        self.windows
            .get_mut(&spec.parent)
            .expect("validated parent")
            .children
            .push(spec.xid);
        self.ids_by_owner.entry(owner).or_default().insert(spec.xid);
        CreateWindowStatus::Success
    }

    pub fn destroy_window(&mut self, xid: WindowId) -> DestroyWindowResult {
        if xid == self.screen.root_window {
            return DestroyWindowResult {
                status: DestroyWindowStatus::RootPreserved,
                destroyed: Vec::new(),
                property_bytes_released: 0,
            };
        }
        let Some(root) = self.window(xid) else {
            return DestroyWindowResult {
                status: DestroyWindowStatus::BadWindow,
                destroyed: Vec::new(),
                property_bytes_released: 0,
            };
        };
        let parent = root.parent.expect("non-root window has parent");
        let mut pending = vec![(xid, false)];
        let mut postorder = Vec::new();
        while let Some((current, visited)) = pending.pop() {
            if visited {
                postorder.push(current);
                continue;
            }
            pending.push((current, true));
            if let Some(window) = self.window(current) {
                pending.extend(window.children.iter().map(|child| (*child, false)));
            }
        }

        let mut released = 0usize;
        for current in &postorder {
            let window = self
                .windows
                .remove(current)
                .expect("captured window exists");
            released += window.properties.byte_size();
            if let Some(owner) = window.owner
                && let Some(ids) = self.ids_by_owner.get_mut(&owner)
            {
                ids.remove(current);
                if ids.is_empty() {
                    self.ids_by_owner.remove(&owner);
                }
            }
        }
        self.windows
            .get_mut(&parent)
            .expect("captured parent exists")
            .children
            .retain(|child| *child != xid);
        self.total_property_bytes -= released;
        DestroyWindowResult {
            status: DestroyWindowStatus::Success,
            destroyed: postorder,
            property_bytes_released: released,
        }
    }

    pub fn set_map_requested(&mut self, xid: WindowId, mapped: bool) -> LifecycleStatus {
        let Some(window) = self.windows.get_mut(&xid) else {
            return LifecycleStatus::BadWindow;
        };
        if xid == self.screen.root_window {
            return LifecycleStatus::BadMatch;
        }
        window.map_requested = mapped;
        self.recompute_map_states(xid);
        LifecycleStatus::Success
    }

    pub fn set_local_map_intent(&mut self, xid: WindowId, mapped: bool) -> LifecycleStatus {
        if self.is_policy_candidate(xid) {
            return LifecycleStatus::BadMatch;
        }
        self.set_map_requested(xid, mapped)
    }

    pub fn set_policy_visible(&mut self, xid: WindowId, visible: bool) -> LifecycleStatus {
        if self.window(xid).is_none() {
            return LifecycleStatus::BadWindow;
        }
        if !self.is_policy_candidate(xid) {
            return LifecycleStatus::BadMatch;
        }
        self.windows
            .get_mut(&xid)
            .expect("validated window")
            .policy_visible = visible;
        self.recompute_map_states(xid);
        LifecycleStatus::Success
    }

    pub fn set_override_redirect(
        &mut self,
        xid: WindowId,
        override_redirect: bool,
    ) -> LifecycleStatus {
        let Some(window) = self.windows.get_mut(&xid) else {
            return LifecycleStatus::BadWindow;
        };
        if xid == self.screen.root_window {
            return LifecycleStatus::BadMatch;
        }
        window.attributes.override_redirect = override_redirect;
        LifecycleStatus::Success
    }

    pub fn configure_local(
        &mut self,
        xid: WindowId,
        configure: ConfigureWindow,
    ) -> LifecycleStatus {
        if self.window(xid).is_none() {
            return LifecycleStatus::BadWindow;
        }
        if xid == self.screen.root_window || self.is_policy_candidate(xid) {
            return LifecycleStatus::BadMatch;
        }
        self.configure(xid, configure)
    }

    pub fn configure(&mut self, xid: WindowId, configure: ConfigureWindow) -> LifecycleStatus {
        let Some(window) = self.window(xid) else {
            return LifecycleStatus::BadWindow;
        };
        if xid == self.screen.root_window {
            return LifecycleStatus::BadMatch;
        }
        if configure
            .width
            .is_some_and(|value| value == 0 || value > u16::MAX.into())
            || configure
                .height
                .is_some_and(|value| value == 0 || value > u16::MAX.into())
            || configure
                .border_width
                .is_some_and(|value| value > u16::MAX.into())
            || configure
                .x
                .is_some_and(|value| value < i16::MIN.into() || value > i16::MAX.into())
            || configure
                .y
                .is_some_and(|value| value < i16::MIN.into() || value > i16::MAX.into())
        {
            return LifecycleStatus::BadValue;
        }
        let parent_id = window.parent.expect("non-root window has parent");
        if let Some(sibling_id) = configure.sibling
            && (sibling_id == xid
                || configure.stack_mode == StackMode::None
                || self
                    .window(sibling_id)
                    .is_none_or(|sibling| sibling.parent != Some(parent_id)))
        {
            return LifecycleStatus::BadMatch;
        }

        let reordered = if configure.stack_mode == StackMode::None {
            None
        } else {
            let mut children = self
                .window(parent_id)
                .expect("validated parent")
                .children
                .clone();
            children.retain(|child| *child != xid);
            match configure.sibling {
                None if configure.stack_mode == StackMode::Above => children.push(xid),
                None => children.insert(0, xid),
                Some(sibling) => {
                    let mut position = children
                        .iter()
                        .position(|child| *child == sibling)
                        .expect("validated sibling");
                    if configure.stack_mode == StackMode::Above {
                        position += 1;
                    }
                    children.insert(position, xid);
                }
            }
            Some(children)
        };

        let window = self.windows.get_mut(&xid).expect("validated window");
        if let Some(x) = configure.x {
            window.geometry.x = x as i16;
        }
        if let Some(y) = configure.y {
            window.geometry.y = y as i16;
        }
        if let Some(width) = configure.width {
            window.geometry.width = width as u16;
        }
        if let Some(height) = configure.height {
            window.geometry.height = height as u16;
        }
        if let Some(border_width) = configure.border_width {
            window.geometry.border_width = border_width as u16;
        }
        window.requested_geometry = window.geometry;
        if let Some(children) = reordered {
            self.windows
                .get_mut(&parent_id)
                .expect("validated parent")
                .children = children;
        }
        LifecycleStatus::Success
    }

    pub fn reorder_root_children(&mut self, visible_bottom_to_top: &[WindowId]) -> bool {
        let root = self.screen.root_window;
        let mut result: Vec<_> = self
            .window(root)
            .expect("root exists")
            .children
            .iter()
            .copied()
            .filter(|child| !visible_bottom_to_top.contains(child))
            .collect();
        for child in visible_bottom_to_top {
            if !self.is_policy_candidate(*child) || result.contains(child) {
                return false;
            }
            result.push(*child);
        }
        self.windows.get_mut(&root).expect("root exists").children = result;
        true
    }

    pub fn change_property(
        &mut self,
        window: WindowId,
        atom: AtomId,
        value: Property,
        mode: PropertyMode,
    ) -> PropertyMutationStatus {
        let Some(window) = self.windows.get_mut(&window) else {
            return PropertyMutationStatus::BadWindow;
        };
        let old_size = window.properties.get(atom).map_or(0, Property::byte_size);
        let status = window.properties.change(
            atom,
            value,
            mode,
            self.property_limits,
            self.total_property_bytes,
        );
        if status == PropertyMutationStatus::Success {
            let new_size = window.properties.get(atom).map_or(0, Property::byte_size);
            self.total_property_bytes = self.total_property_bytes - old_size + new_size;
        }
        status
    }

    pub fn delete_property(&mut self, window: WindowId, atom: AtomId) -> bool {
        let Some(window) = self.windows.get_mut(&window) else {
            return false;
        };
        self.total_property_bytes -= window.properties.delete(atom);
        true
    }

    pub fn get_property(
        &mut self,
        window: WindowId,
        atom: AtomId,
        requested_type: Option<AtomId>,
        delete_after_read: bool,
        long_offset: u32,
        long_length: u32,
    ) -> PropertyReadResult {
        let Some(window) = self.windows.get_mut(&window) else {
            return PropertyReadResult::error(PropertyReadStatus::BadWindow);
        };
        let old_size = window.properties.get(atom).map_or(0, Property::byte_size);
        let result = window.properties.read(
            atom,
            requested_type,
            delete_after_read,
            long_offset,
            long_length,
        );
        if result.deleted {
            self.total_property_bytes -= old_size;
        }
        result
    }

    pub fn list_properties(&self, window: WindowId) -> Vec<AtomId> {
        self.window(window)
            .map_or_else(Vec::new, |window| window.properties.atoms())
    }

    pub fn invariants_hold(&self) -> bool {
        let Some(root) = self.window(self.screen.root_window) else {
            return false;
        };
        if root.owner.is_some()
            || root.parent.is_some()
            || root.map_state != MapState::Viewable
            || root.geometry.width != self.screen.root_width
            || root.geometry.height != self.screen.root_height
        {
            return false;
        }
        let mut property_bytes = 0usize;
        for (xid, window) in &self.windows {
            property_bytes += window.properties.byte_size();
            if *xid != self.screen.root_window {
                let Some(owner) = window.owner else {
                    return false;
                };
                let Some(parent) = window.parent.and_then(|parent| self.window(parent)) else {
                    return false;
                };
                if parent
                    .children
                    .iter()
                    .filter(|child| **child == *xid)
                    .count()
                    != 1
                    || self
                        .ids_by_owner
                        .get(&owner)
                        .is_none_or(|ids| !ids.contains(xid))
                {
                    return false;
                }
            }
            if window.children.iter().any(|child| {
                self.window(*child)
                    .is_none_or(|child_window| child_window.parent != Some(*xid))
            }) {
                return false;
            }
        }
        if property_bytes != self.total_property_bytes {
            return false;
        }
        self.ids_by_owner.iter().all(|(owner, ids)| {
            ids.iter().all(|xid| {
                self.window(*xid)
                    .is_some_and(|window| window.owner == Some(*owner))
            })
        })
    }

    fn recompute_map_states(&mut self, xid: WindowId) {
        let parent_viewable = self
            .window(xid)
            .and_then(Window::parent)
            .and_then(|parent| self.window(parent))
            .is_some_and(|parent| parent.map_state == MapState::Viewable);
        let mut pending = vec![(xid, parent_viewable)];
        while let Some((current, parent_viewable)) = pending.pop() {
            let candidate = self.is_policy_candidate(current);
            let Some(window) = self.windows.get_mut(&current) else {
                continue;
            };
            window.map_state = if !window.map_requested || (candidate && !window.policy_visible) {
                MapState::Unmapped
            } else if parent_viewable {
                MapState::Viewable
            } else {
                MapState::Unviewable
            };
            let viewable = window.map_state == MapState::Viewable;
            pending.extend(window.children.iter().rev().map(|child| (*child, viewable)));
        }
    }
}

impl Default for WindowStore {
    fn default() -> Self {
        Self::with_default_screen()
    }
}
