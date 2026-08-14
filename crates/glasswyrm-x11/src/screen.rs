#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ScreenModel {
    pub root_window: u32,
    pub default_colormap: u32,
    pub root_visual: u32,
    pub root_depth: u8,
    pub width_pixels: u16,
    pub height_pixels: u16,
    pub width_millimeters: u16,
    pub height_millimeters: u16,
    pub red_mask: u32,
    pub green_mask: u32,
    pub blue_mask: u32,
    pub maximum_request_length: u16,
    pub resource_id_mask: u32,
    pub refresh_millihertz: u32,
}

pub const SCREEN_MODEL: ScreenModel = ScreenModel {
    root_window: 1,
    default_colormap: 2,
    root_visual: 3,
    root_depth: 24,
    width_pixels: 1024,
    height_pixels: 768,
    width_millimeters: 270,
    height_millimeters: 203,
    red_mask: 0x00ff_0000,
    green_mask: 0x0000_ff00,
    blue_mask: 0x0000_00ff,
    maximum_request_length: 65_535,
    resource_id_mask: 0x001f_ffff,
    refresh_millihertz: 60_000,
};

impl Default for ScreenModel {
    fn default() -> Self {
        SCREEN_MODEL
    }
}
