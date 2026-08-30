#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum CoreOpcode {
    CreateWindow = 1,
    ChangeWindowAttributes = 2,
    GetWindowAttributes = 3,
    DestroyWindow = 4,
    MapWindow = 8,
    MapSubwindows = 9,
    UnmapWindow = 10,
    UnmapSubwindows = 11,
    ConfigureWindow = 12,
    GetGeometry = 14,
    QueryTree = 15,
    InternAtom = 16,
    GetAtomName = 17,
    ChangeProperty = 18,
    DeleteProperty = 19,
    GetProperty = 20,
    ListProperties = 21,
    SetSelectionOwner = 22,
    GetSelectionOwner = 23,
    ConvertSelection = 24,
    SendEvent = 25,
    GrabPointer = 26,
    UngrabPointer = 27,
    GrabButton = 28,
    ChangeActivePointerGrab = 30,
    GrabKeyboard = 31,
    UngrabKeyboard = 32,
    AllowEvents = 35,
    QueryPointer = 38,
    TranslateCoordinates = 40,
    WarpPointer = 41,
    SetInputFocus = 42,
    GetInputFocus = 43,
    QueryKeymap = 44,
    OpenFont = 45,
    CloseFont = 46,
    QueryFont = 47,
    QueryTextExtents = 48,
    ListFonts = 49,
    CreatePixmap = 53,
    FreePixmap = 54,
    CreateGc = 55,
    ChangeGc = 56,
    FreeGc = 60,
    ClearArea = 61,
    CopyArea = 62,
    PolyLine = 65,
    PolySegment = 66,
    FillPoly = 69,
    PolyFillRectangle = 70,
    PolyFillArc = 71,
    PutImage = 72,
    PolyText8 = 74,
    ImageText8 = 76,
    CreateColormap = 78,
    FreeColormap = 79,
    InstallColormap = 81,
    UninstallColormap = 82,
    ListInstalledColormaps = 83,
    AllocColor = 84,
    AllocNamedColor = 85,
    FreeColors = 88,
    QueryColors = 91,
    LookupColor = 92,
    CreateCursor = 93,
    CreateGlyphCursor = 94,
    FreeCursor = 95,
    RecolorCursor = 96,
    QueryBestSize = 97,
    QueryExtension = 98,
    ListExtensions = 99,
    GetKeyboardMapping = 101,
    ChangeKeyboardControl = 102,
    GetKeyboardControl = 103,
    Bell = 104,
    ForceScreenSaver = 115,
    GetPointerMapping = 117,
    GetModifierMapping = 119,
    NoOperation = 127,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CoreRequestCoverage {
    Decoded,
    Dispatched,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CoreRequestMetadata {
    pub opcode: CoreOpcode,
    pub name: &'static str,
    pub coverage: CoreRequestCoverage,
}

macro_rules! core_request {
    ($coverage:ident, $opcode:ident) => {
        CoreRequestMetadata {
            opcode: CoreOpcode::$opcode,
            name: stringify!($opcode),
            coverage: CoreRequestCoverage::$coverage,
        }
    };
}

/// Canonical WP1 coverage for every core request implemented by the retained
/// Glasswyrm server. `Decoded` means the wire shape is available without
/// claiming stateful server behavior; `Dispatched` means the Rust session also
/// implements the request's current state/reply behavior.
pub const CORE_REQUEST_METADATA: [CoreRequestMetadata; 79] = [
    core_request!(Dispatched, CreateWindow),
    core_request!(Decoded, ChangeWindowAttributes),
    core_request!(Decoded, GetWindowAttributes),
    core_request!(Dispatched, DestroyWindow),
    core_request!(Decoded, MapWindow),
    core_request!(Decoded, MapSubwindows),
    core_request!(Decoded, UnmapWindow),
    core_request!(Decoded, UnmapSubwindows),
    core_request!(Decoded, ConfigureWindow),
    core_request!(Dispatched, GetGeometry),
    core_request!(Dispatched, QueryTree),
    core_request!(Dispatched, InternAtom),
    core_request!(Dispatched, GetAtomName),
    core_request!(Dispatched, ChangeProperty),
    core_request!(Dispatched, DeleteProperty),
    core_request!(Dispatched, GetProperty),
    core_request!(Dispatched, ListProperties),
    core_request!(Decoded, SetSelectionOwner),
    core_request!(Decoded, GetSelectionOwner),
    core_request!(Decoded, ConvertSelection),
    core_request!(Decoded, SendEvent),
    core_request!(Decoded, GrabPointer),
    core_request!(Decoded, UngrabPointer),
    core_request!(Decoded, GrabButton),
    core_request!(Decoded, ChangeActivePointerGrab),
    core_request!(Decoded, GrabKeyboard),
    core_request!(Decoded, UngrabKeyboard),
    core_request!(Decoded, AllowEvents),
    core_request!(Decoded, QueryPointer),
    core_request!(Decoded, TranslateCoordinates),
    core_request!(Decoded, WarpPointer),
    core_request!(Decoded, SetInputFocus),
    core_request!(Dispatched, GetInputFocus),
    core_request!(Decoded, QueryKeymap),
    core_request!(Decoded, OpenFont),
    core_request!(Decoded, CloseFont),
    core_request!(Decoded, QueryFont),
    core_request!(Decoded, QueryTextExtents),
    core_request!(Decoded, ListFonts),
    core_request!(Decoded, CreatePixmap),
    core_request!(Decoded, FreePixmap),
    core_request!(Decoded, CreateGc),
    core_request!(Decoded, ChangeGc),
    core_request!(Decoded, FreeGc),
    core_request!(Decoded, ClearArea),
    core_request!(Decoded, CopyArea),
    core_request!(Decoded, PolyLine),
    core_request!(Decoded, PolySegment),
    core_request!(Decoded, FillPoly),
    core_request!(Decoded, PolyFillRectangle),
    core_request!(Decoded, PolyFillArc),
    core_request!(Decoded, PutImage),
    core_request!(Decoded, PolyText8),
    core_request!(Decoded, ImageText8),
    core_request!(Decoded, CreateColormap),
    core_request!(Decoded, FreeColormap),
    core_request!(Decoded, InstallColormap),
    core_request!(Decoded, UninstallColormap),
    core_request!(Decoded, ListInstalledColormaps),
    core_request!(Decoded, AllocColor),
    core_request!(Decoded, AllocNamedColor),
    core_request!(Decoded, FreeColors),
    core_request!(Decoded, QueryColors),
    core_request!(Decoded, LookupColor),
    core_request!(Decoded, CreateCursor),
    core_request!(Decoded, CreateGlyphCursor),
    core_request!(Decoded, FreeCursor),
    core_request!(Decoded, RecolorCursor),
    core_request!(Decoded, QueryBestSize),
    core_request!(Dispatched, QueryExtension),
    core_request!(Dispatched, ListExtensions),
    core_request!(Decoded, GetKeyboardMapping),
    core_request!(Decoded, ChangeKeyboardControl),
    core_request!(Decoded, GetKeyboardControl),
    core_request!(Decoded, Bell),
    core_request!(Decoded, ForceScreenSaver),
    core_request!(Decoded, GetPointerMapping),
    core_request!(Decoded, GetModifierMapping),
    core_request!(Dispatched, NoOperation),
];

#[must_use]
pub fn core_request_metadata(opcode: u8) -> Option<&'static CoreRequestMetadata> {
    CORE_REQUEST_METADATA
        .iter()
        .find(|metadata| metadata.opcode as u8 == opcode)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum CoreEventType {
    KeyPress = 2,
    KeyRelease = 3,
    ButtonPress = 4,
    ButtonRelease = 5,
    MotionNotify = 6,
    EnterNotify = 7,
    LeaveNotify = 8,
    FocusIn = 9,
    FocusOut = 10,
    Expose = 12,
    GraphicsExpose = 13,
    NoExpose = 14,
    DestroyNotify = 17,
    UnmapNotify = 18,
    MapNotify = 19,
    ConfigureNotify = 22,
    PropertyNotify = 28,
    SelectionClear = 29,
    SelectionRequest = 30,
    SelectionNotify = 31,
    ClientMessage = 33,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum CoreErrorCode {
    BadRequest = 1,
    BadValue = 2,
    BadWindow = 3,
    BadPixmap = 4,
    BadAtom = 5,
    BadCursor = 6,
    BadFont = 7,
    BadMatch = 8,
    BadDrawable = 9,
    BadAccess = 10,
    BadAlloc = 11,
    BadColormap = 12,
    BadGContext = 13,
    BadIdChoice = 14,
    BadName = 15,
    BadLength = 16,
    BadImplementation = 17,
}

#[must_use]
pub const fn wire_sequence(sequence: u64) -> u16 {
    (sequence & 0xffff) as u16
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn core_request_metadata_is_complete_ordered_and_unique() {
        assert_eq!(CORE_REQUEST_METADATA.len(), 79);
        assert!(
            CORE_REQUEST_METADATA
                .windows(2)
                .all(|pair| (pair[0].opcode as u8) < pair[1].opcode as u8)
        );
        for metadata in CORE_REQUEST_METADATA {
            assert_eq!(
                core_request_metadata(metadata.opcode as u8),
                Some(&metadata)
            );
        }
        assert_eq!(core_request_metadata(0), None);
        assert_eq!(core_request_metadata(128), None);
    }

    #[test]
    fn core_request_metadata_tracks_the_exact_wp1_dispatch_subset() {
        let dispatched: Vec<_> = CORE_REQUEST_METADATA
            .iter()
            .filter(|metadata| metadata.coverage == CoreRequestCoverage::Dispatched)
            .map(|metadata| metadata.name)
            .collect();
        assert_eq!(
            dispatched,
            [
                "CreateWindow",
                "DestroyWindow",
                "GetGeometry",
                "QueryTree",
                "InternAtom",
                "GetAtomName",
                "ChangeProperty",
                "DeleteProperty",
                "GetProperty",
                "ListProperties",
                "GetInputFocus",
                "QueryExtension",
                "ListExtensions",
                "NoOperation",
            ]
        );
    }
}
