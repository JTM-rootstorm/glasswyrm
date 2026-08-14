use std::collections::HashMap;

pub const HIGHEST_PREDEFINED_ATOM: u32 = 68;

pub const PREDEFINED_ATOMS: [&str; HIGHEST_PREDEFINED_ATOM as usize + 1] = [
    "",
    "PRIMARY",
    "SECONDARY",
    "ARC",
    "ATOM",
    "BITMAP",
    "CARDINAL",
    "COLORMAP",
    "CURSOR",
    "CUT_BUFFER0",
    "CUT_BUFFER1",
    "CUT_BUFFER2",
    "CUT_BUFFER3",
    "CUT_BUFFER4",
    "CUT_BUFFER5",
    "CUT_BUFFER6",
    "CUT_BUFFER7",
    "DRAWABLE",
    "FONT",
    "INTEGER",
    "PIXMAP",
    "POINT",
    "RECTANGLE",
    "RESOURCE_MANAGER",
    "RGB_COLOR_MAP",
    "RGB_BEST_MAP",
    "RGB_BLUE_MAP",
    "RGB_DEFAULT_MAP",
    "RGB_GRAY_MAP",
    "RGB_GREEN_MAP",
    "RGB_RED_MAP",
    "STRING",
    "VISUALID",
    "WINDOW",
    "WM_COMMAND",
    "WM_HINTS",
    "WM_CLIENT_MACHINE",
    "WM_ICON_NAME",
    "WM_ICON_SIZE",
    "WM_NAME",
    "WM_NORMAL_HINTS",
    "WM_SIZE_HINTS",
    "WM_ZOOM_HINTS",
    "MIN_SPACE",
    "NORM_SPACE",
    "MAX_SPACE",
    "END_SPACE",
    "SUPERSCRIPT_X",
    "SUPERSCRIPT_Y",
    "SUBSCRIPT_X",
    "SUBSCRIPT_Y",
    "UNDERLINE_POSITION",
    "UNDERLINE_THICKNESS",
    "STRIKEOUT_ASCENT",
    "STRIKEOUT_DESCENT",
    "ITALIC_ANGLE",
    "X_HEIGHT",
    "QUAD_WIDTH",
    "WEIGHT",
    "POINT_SIZE",
    "RESOLUTION",
    "COPYRIGHT",
    "NOTICE",
    "FONT_NAME",
    "FAMILY_NAME",
    "FULL_NAME",
    "CAP_HEIGHT",
    "WM_CLASS",
    "WM_TRANSIENT_FOR",
];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum InternAtomStatus {
    Success,
    Exhausted,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct InternAtomResult {
    pub status: InternAtomStatus,
    pub atom: u32,
}

impl InternAtomResult {
    const fn success(atom: u32) -> Self {
        Self {
            status: InternAtomStatus::Success,
            atom,
        }
    }

    const fn exhausted() -> Self {
        Self {
            status: InternAtomStatus::Exhausted,
            atom: 0,
        }
    }
}

#[derive(Clone, Debug)]
pub struct AtomTable {
    ids_by_name: HashMap<String, u32>,
    names_by_id: HashMap<u32, String>,
    next_dynamic_atom: u32,
    maximum_atom: u32,
}

impl AtomTable {
    pub fn new(maximum_atom: u32) -> Self {
        let mut ids_by_name = HashMap::with_capacity(HIGHEST_PREDEFINED_ATOM as usize);
        let mut names_by_id = HashMap::with_capacity(HIGHEST_PREDEFINED_ATOM as usize);
        for atom in 1..=HIGHEST_PREDEFINED_ATOM {
            let name = PREDEFINED_ATOMS[atom as usize].to_owned();
            ids_by_name.insert(name.clone(), atom);
            names_by_id.insert(atom, name);
        }
        Self {
            ids_by_name,
            names_by_id,
            next_dynamic_atom: HIGHEST_PREDEFINED_ATOM + 1,
            maximum_atom,
        }
    }

    pub fn intern(&mut self, name: &str, only_if_exists: bool) -> InternAtomResult {
        if let Some(atom) = self.find(name) {
            return InternAtomResult::success(atom);
        }
        if only_if_exists {
            return InternAtomResult::success(0);
        }
        if self.next_dynamic_atom == 0 || self.next_dynamic_atom > self.maximum_atom {
            return InternAtomResult::exhausted();
        }

        let atom = self.next_dynamic_atom;
        if self.ids_by_name.try_reserve(1).is_err() || self.names_by_id.try_reserve(1).is_err() {
            return InternAtomResult::exhausted();
        }
        let owned = name.to_owned();
        self.ids_by_name.insert(owned.clone(), atom);
        self.names_by_id.insert(atom, owned);
        self.next_dynamic_atom = atom.checked_add(1).unwrap_or(0);
        InternAtomResult::success(atom)
    }

    pub fn find(&self, name: &str) -> Option<u32> {
        self.ids_by_name.get(name).copied()
    }

    pub fn name(&self, atom: u32) -> Option<&str> {
        self.names_by_id.get(&atom).map(String::as_str)
    }

    pub fn valid(&self, atom: u32, allow_none: bool) -> bool {
        (allow_none && atom == 0) || self.names_by_id.contains_key(&atom)
    }

    pub fn len(&self) -> usize {
        self.names_by_id.len()
    }

    pub fn is_empty(&self) -> bool {
        self.names_by_id.is_empty()
    }
}

impl Default for AtomTable {
    fn default() -> Self {
        Self::new(u32::MAX)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn predefined_atoms_are_exact_and_none_is_not_an_atom() {
        let atoms = AtomTable::default();
        assert!(!atoms.valid(0, false));
        assert!(atoms.valid(0, true));
        assert_eq!(atoms.name(0), None);
        assert_eq!(atoms.len(), HIGHEST_PREDEFINED_ATOM as usize);
        for atom in 1..=HIGHEST_PREDEFINED_ATOM {
            assert_eq!(atoms.name(atom), Some(PREDEFINED_ATOMS[atom as usize]));
            assert_eq!(atoms.find(PREDEFINED_ATOMS[atom as usize]), Some(atom));
        }
    }

    #[test]
    fn dynamic_atoms_are_case_sensitive_stable_and_bounded() {
        let mut atoms = AtomTable::default();
        assert_eq!(atoms.intern("case-sensitive", true).atom, 0);
        let first = atoms.intern("case-sensitive", false);
        let second = atoms.intern("case-sensitive", false);
        let different = atoms.intern("CASE-SENSITIVE", false);
        assert_eq!(first, InternAtomResult::success(69));
        assert_eq!(second, first);
        assert_eq!(different, InternAtomResult::success(70));
        assert_eq!(atoms.name(first.atom), Some("case-sensitive"));

        let mut limited = AtomTable::new(69);
        assert_eq!(limited.intern("last", false).atom, 69);
        assert_eq!(
            limited.intern("exhausted", false).status,
            InternAtomStatus::Exhausted
        );
    }
}
