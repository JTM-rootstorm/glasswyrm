use std::collections::HashMap;

pub const HIGHEST_PREDEFINED_ATOM: u32 = 68;
pub const MAXIMUM_ATOMS: usize = 65_536;
pub const MAXIMUM_ATOM_NAME_BYTES: usize = 4 * 1024 * 1024;

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
    ids_by_name: HashMap<Vec<u8>, u32>,
    names_by_id: HashMap<u32, Vec<u8>>,
    next_dynamic_atom: u32,
    limits: AtomLimits,
    name_bytes: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AtomLimits {
    pub maximum_atom_id: u32,
    pub maximum_atoms: usize,
    pub maximum_name_bytes: usize,
}

impl Default for AtomLimits {
    fn default() -> Self {
        Self {
            maximum_atom_id: u32::MAX,
            maximum_atoms: MAXIMUM_ATOMS,
            maximum_name_bytes: MAXIMUM_ATOM_NAME_BYTES,
        }
    }
}

impl AtomTable {
    pub fn new(maximum_atom: u32) -> Self {
        Self::with_limits(AtomLimits {
            maximum_atom_id: maximum_atom,
            ..AtomLimits::default()
        })
    }

    pub fn with_limits(limits: AtomLimits) -> Self {
        let mut ids_by_name = HashMap::with_capacity(HIGHEST_PREDEFINED_ATOM as usize);
        let mut names_by_id = HashMap::with_capacity(HIGHEST_PREDEFINED_ATOM as usize);
        let mut name_bytes = 0;
        for atom in 1..=HIGHEST_PREDEFINED_ATOM {
            let name = PREDEFINED_ATOMS[atom as usize].as_bytes().to_vec();
            name_bytes += name.len();
            ids_by_name.insert(name.clone(), atom);
            names_by_id.insert(atom, name);
        }
        Self {
            ids_by_name,
            names_by_id,
            next_dynamic_atom: HIGHEST_PREDEFINED_ATOM + 1,
            limits,
            name_bytes,
        }
    }

    pub fn intern(&mut self, name: &[u8], only_if_exists: bool) -> InternAtomResult {
        if let Some(atom) = self.find(name) {
            return InternAtomResult::success(atom);
        }
        if only_if_exists {
            return InternAtomResult::success(0);
        }
        if self.next_dynamic_atom == 0
            || self.next_dynamic_atom > self.limits.maximum_atom_id
            || self.names_by_id.len() >= self.limits.maximum_atoms
            || self.name_bytes > self.limits.maximum_name_bytes
            || name.len() > self.limits.maximum_name_bytes - self.name_bytes
        {
            return InternAtomResult::exhausted();
        }

        let atom = self.next_dynamic_atom;
        if self.ids_by_name.try_reserve(1).is_err() || self.names_by_id.try_reserve(1).is_err() {
            return InternAtomResult::exhausted();
        }
        let owned = name.to_vec();
        self.ids_by_name.insert(owned.clone(), atom);
        self.names_by_id.insert(atom, owned);
        self.name_bytes += name.len();
        self.next_dynamic_atom = atom.checked_add(1).unwrap_or(0);
        InternAtomResult::success(atom)
    }

    pub fn find(&self, name: &[u8]) -> Option<u32> {
        self.ids_by_name.get(name).copied()
    }

    pub fn name(&self, atom: u32) -> Option<&[u8]> {
        self.names_by_id.get(&atom).map(Vec::as_slice)
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

    pub fn name_bytes(&self) -> usize {
        self.name_bytes
    }
}

impl Default for AtomTable {
    fn default() -> Self {
        Self::with_limits(AtomLimits::default())
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
            assert_eq!(
                atoms.name(atom),
                Some(PREDEFINED_ATOMS[atom as usize].as_bytes())
            );
            assert_eq!(
                atoms.find(PREDEFINED_ATOMS[atom as usize].as_bytes()),
                Some(atom)
            );
        }
    }

    #[test]
    fn dynamic_atoms_are_case_sensitive_stable_and_bounded() {
        let mut atoms = AtomTable::default();
        assert_eq!(atoms.intern(b"case-sensitive", true).atom, 0);
        let first = atoms.intern(b"case-sensitive", false);
        let second = atoms.intern(b"case-sensitive", false);
        let different = atoms.intern(b"CASE-SENSITIVE", false);
        assert_eq!(first, InternAtomResult::success(69));
        assert_eq!(second, first);
        assert_eq!(different, InternAtomResult::success(70));
        assert_eq!(atoms.name(first.atom), Some(b"case-sensitive".as_slice()));

        let mut limited = AtomTable::new(69);
        assert_eq!(limited.intern(b"last", false).atom, 69);
        assert_eq!(
            limited.intern(b"exhausted", false).status,
            InternAtomStatus::Exhausted
        );
    }

    #[test]
    fn dynamic_atom_count_and_name_bytes_match_server_limits() {
        let predefined_bytes = AtomTable::default().name_bytes();
        let mut count_limited = AtomTable::with_limits(AtomLimits {
            maximum_atoms: HIGHEST_PREDEFINED_ATOM as usize + 1,
            maximum_name_bytes: predefined_bytes + 1024,
            ..AtomLimits::default()
        });
        assert_eq!(count_limited.intern(b"one", false).atom, 69);
        assert_eq!(
            count_limited.intern(b"two", false).status,
            InternAtomStatus::Exhausted
        );
        assert_eq!(count_limited.intern(b"one", false).atom, 69);

        let mut bytes_limited = AtomTable::with_limits(AtomLimits {
            maximum_atoms: HIGHEST_PREDEFINED_ATOM as usize + 10,
            maximum_name_bytes: predefined_bytes + 3,
            ..AtomLimits::default()
        });
        assert_eq!(bytes_limited.intern(b"abc", false).atom, 69);
        assert_eq!(bytes_limited.name_bytes(), predefined_bytes + 3);
        assert_eq!(
            bytes_limited.intern(b"d", false).status,
            InternAtomStatus::Exhausted
        );
        assert_eq!(bytes_limited.intern(b"abc", false).atom, 69);
    }

    #[test]
    fn atom_names_preserve_non_utf8_protocol_bytes() {
        let mut atoms = AtomTable::default();
        let name = b"GW_\xff_ATOM";
        let result = atoms.intern(name, false);
        assert_eq!(result.atom, 69);
        assert_eq!(atoms.find(name), Some(69));
        assert_eq!(atoms.name(69), Some(name.as_slice()));
    }
}
