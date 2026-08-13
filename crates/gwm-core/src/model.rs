use std::collections::BTreeMap;

use gw_types::{Generation, OutputId, WindowId};

pub const MAXIMUM_WINDOWS: usize = 4_096;
pub const MAXIMUM_OUTPUTS: usize = 8;
pub const MAXIMUM_WORK_EXTENT: u32 = 16_384;
pub const MAXIMUM_ROOT_EXTENT: u32 = 32_767;
pub const MAXIMUM_WINDOW_EXTENT: u32 = 16_384;

pub const WINDOW_ABOVE: u32 = 1 << 0;
pub const WINDOW_BYPASS_COMPOSITOR: u32 = 1 << 1;
pub const WINDOW_INPUT_DISABLED: u32 = 1 << 2;
pub const KNOWN_WINDOW_FLAGS: u32 = WINDOW_ABOVE | WINDOW_BYPASS_COMPOSITOR | WINDOW_INPUT_DISABLED;

pub type WorkspaceId = u32;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Rectangle {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Context {
    pub root_window_id: WindowId,
    pub workspace_id: WorkspaceId,
    pub primary_output_id: OutputId,
    pub work: Rectangle,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct OutputContext {
    pub output_id: OutputId,
    pub logical: Rectangle,
    pub work: Rectangle,
    pub enabled: bool,
    pub primary: bool,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WindowOutputHint {
    pub previous_output_id: OutputId,
    pub preferred_output_id: OutputId,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum WindowType {
    #[default]
    Unknown,
    Normal,
    Dialog,
    Utility,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum DecorationPreference {
    #[default]
    Unknown,
    False,
    True,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum StackMode {
    #[default]
    None,
    Above,
    Below,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum AppliedState {
    #[default]
    Normal,
    Maximized,
    Fullscreen,
    Minimized,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum TriState {
    #[default]
    Unknown,
    False,
    True,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RawWindow {
    pub window_id: WindowId,
    pub parent_window_id: WindowId,
    pub transient_for: Option<WindowId>,
    pub workspace_id: Option<WorkspaceId>,
    pub requested: Rectangle,
    pub border_width: u32,
    pub window_type: WindowType,
    pub wants_map: bool,
    pub override_redirect: bool,
    pub decoration_preference: DecorationPreference,
    pub fullscreen_requested: bool,
    pub maximized_requested: bool,
    pub minimized_requested: bool,
    pub attention_requested: bool,
    pub creation_serial: u64,
    pub map_serial: u64,
    pub focus_serial: u64,
    pub geometry_serial: u64,
    pub stack_serial: u64,
    pub stack_sibling: Option<WindowId>,
    pub stack_mode: StackMode,
    pub flags: u32,
}

impl RawWindow {
    #[must_use]
    pub fn mapped(window_id: WindowId, parent_window_id: WindowId, creation_serial: u64) -> Self {
        Self {
            window_id,
            parent_window_id,
            transient_for: None,
            workspace_id: None,
            requested: Rectangle {
                width: 200,
                height: 100,
                ..Rectangle::default()
            },
            border_width: 0,
            window_type: WindowType::Normal,
            wants_map: true,
            override_redirect: false,
            decoration_preference: DecorationPreference::Unknown,
            fullscreen_requested: false,
            maximized_requested: false,
            minimized_requested: false,
            attention_requested: false,
            creation_serial,
            map_serial: creation_serial,
            focus_serial: 0,
            geometry_serial: 0,
            stack_serial: 0,
            stack_sibling: None,
            stack_mode: StackMode::None,
            flags: 0,
        }
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RawState {
    pub complete: bool,
    pub producer_generation: Generation,
    pub context: Context,
    pub outputs: BTreeMap<OutputId, OutputContext>,
    pub output_hints: BTreeMap<WindowId, WindowOutputHint>,
    pub windows: BTreeMap<WindowId, RawWindow>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WindowState {
    pub window_id: WindowId,
    pub transient_for: Option<WindowId>,
    pub workspace_id: WorkspaceId,
    pub output_id: OutputId,
    pub geometry: Rectangle,
    pub stacking: Option<u32>,
    pub window_type: WindowType,
    pub applied_state: AppliedState,
    pub visible: bool,
    pub focused: bool,
    pub managed: bool,
    pub decoration_eligible: bool,
    pub override_redirect: bool,
    pub attention_requested: bool,
    pub fullscreen_eligible: TriState,
    pub direct_scanout_eligible: TriState,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EvaluationError {
    IncompleteSnapshot,
    InvalidGeneration,
    InvalidContext,
    InvalidWindow,
    UnknownReference,
    UnsupportedMetadata,
    Limit,
}
