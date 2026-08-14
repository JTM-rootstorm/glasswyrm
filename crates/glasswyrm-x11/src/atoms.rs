#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PredefinedAtom {
    pub id: u32,
    pub name: &'static str,
}

pub const NONE_ATOM: u32 = 0;

pub const PREDEFINED_ATOMS: [PredefinedAtom; 68] = [
    PredefinedAtom {
        id: 1,
        name: "PRIMARY",
    },
    PredefinedAtom {
        id: 2,
        name: "SECONDARY",
    },
    PredefinedAtom { id: 3, name: "ARC" },
    PredefinedAtom {
        id: 4,
        name: "ATOM",
    },
    PredefinedAtom {
        id: 5,
        name: "BITMAP",
    },
    PredefinedAtom {
        id: 6,
        name: "CARDINAL",
    },
    PredefinedAtom {
        id: 7,
        name: "COLORMAP",
    },
    PredefinedAtom {
        id: 8,
        name: "CURSOR",
    },
    PredefinedAtom {
        id: 9,
        name: "CUT_BUFFER0",
    },
    PredefinedAtom {
        id: 10,
        name: "CUT_BUFFER1",
    },
    PredefinedAtom {
        id: 11,
        name: "CUT_BUFFER2",
    },
    PredefinedAtom {
        id: 12,
        name: "CUT_BUFFER3",
    },
    PredefinedAtom {
        id: 13,
        name: "CUT_BUFFER4",
    },
    PredefinedAtom {
        id: 14,
        name: "CUT_BUFFER5",
    },
    PredefinedAtom {
        id: 15,
        name: "CUT_BUFFER6",
    },
    PredefinedAtom {
        id: 16,
        name: "CUT_BUFFER7",
    },
    PredefinedAtom {
        id: 17,
        name: "DRAWABLE",
    },
    PredefinedAtom {
        id: 18,
        name: "FONT",
    },
    PredefinedAtom {
        id: 19,
        name: "INTEGER",
    },
    PredefinedAtom {
        id: 20,
        name: "PIXMAP",
    },
    PredefinedAtom {
        id: 21,
        name: "POINT",
    },
    PredefinedAtom {
        id: 22,
        name: "RECTANGLE",
    },
    PredefinedAtom {
        id: 23,
        name: "RESOURCE_MANAGER",
    },
    PredefinedAtom {
        id: 24,
        name: "RGB_COLOR_MAP",
    },
    PredefinedAtom {
        id: 25,
        name: "RGB_BEST_MAP",
    },
    PredefinedAtom {
        id: 26,
        name: "RGB_BLUE_MAP",
    },
    PredefinedAtom {
        id: 27,
        name: "RGB_DEFAULT_MAP",
    },
    PredefinedAtom {
        id: 28,
        name: "RGB_GRAY_MAP",
    },
    PredefinedAtom {
        id: 29,
        name: "RGB_GREEN_MAP",
    },
    PredefinedAtom {
        id: 30,
        name: "RGB_RED_MAP",
    },
    PredefinedAtom {
        id: 31,
        name: "STRING",
    },
    PredefinedAtom {
        id: 32,
        name: "VISUALID",
    },
    PredefinedAtom {
        id: 33,
        name: "WINDOW",
    },
    PredefinedAtom {
        id: 34,
        name: "WM_COMMAND",
    },
    PredefinedAtom {
        id: 35,
        name: "WM_HINTS",
    },
    PredefinedAtom {
        id: 36,
        name: "WM_CLIENT_MACHINE",
    },
    PredefinedAtom {
        id: 37,
        name: "WM_ICON_NAME",
    },
    PredefinedAtom {
        id: 38,
        name: "WM_ICON_SIZE",
    },
    PredefinedAtom {
        id: 39,
        name: "WM_NAME",
    },
    PredefinedAtom {
        id: 40,
        name: "WM_NORMAL_HINTS",
    },
    PredefinedAtom {
        id: 41,
        name: "WM_SIZE_HINTS",
    },
    PredefinedAtom {
        id: 42,
        name: "WM_ZOOM_HINTS",
    },
    PredefinedAtom {
        id: 43,
        name: "MIN_SPACE",
    },
    PredefinedAtom {
        id: 44,
        name: "NORM_SPACE",
    },
    PredefinedAtom {
        id: 45,
        name: "MAX_SPACE",
    },
    PredefinedAtom {
        id: 46,
        name: "END_SPACE",
    },
    PredefinedAtom {
        id: 47,
        name: "SUPERSCRIPT_X",
    },
    PredefinedAtom {
        id: 48,
        name: "SUPERSCRIPT_Y",
    },
    PredefinedAtom {
        id: 49,
        name: "SUBSCRIPT_X",
    },
    PredefinedAtom {
        id: 50,
        name: "SUBSCRIPT_Y",
    },
    PredefinedAtom {
        id: 51,
        name: "UNDERLINE_POSITION",
    },
    PredefinedAtom {
        id: 52,
        name: "UNDERLINE_THICKNESS",
    },
    PredefinedAtom {
        id: 53,
        name: "STRIKEOUT_ASCENT",
    },
    PredefinedAtom {
        id: 54,
        name: "STRIKEOUT_DESCENT",
    },
    PredefinedAtom {
        id: 55,
        name: "ITALIC_ANGLE",
    },
    PredefinedAtom {
        id: 56,
        name: "X_HEIGHT",
    },
    PredefinedAtom {
        id: 57,
        name: "QUAD_WIDTH",
    },
    PredefinedAtom {
        id: 58,
        name: "WEIGHT",
    },
    PredefinedAtom {
        id: 59,
        name: "POINT_SIZE",
    },
    PredefinedAtom {
        id: 60,
        name: "RESOLUTION",
    },
    PredefinedAtom {
        id: 61,
        name: "COPYRIGHT",
    },
    PredefinedAtom {
        id: 62,
        name: "NOTICE",
    },
    PredefinedAtom {
        id: 63,
        name: "FONT_NAME",
    },
    PredefinedAtom {
        id: 64,
        name: "FAMILY_NAME",
    },
    PredefinedAtom {
        id: 65,
        name: "FULL_NAME",
    },
    PredefinedAtom {
        id: 66,
        name: "CAP_HEIGHT",
    },
    PredefinedAtom {
        id: 67,
        name: "WM_CLASS",
    },
    PredefinedAtom {
        id: 68,
        name: "WM_TRANSIENT_FOR",
    },
];

pub const LAST_PREDEFINED_ATOM: u32 = 68;
