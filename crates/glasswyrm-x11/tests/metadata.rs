use glasswyrm_x11::{
    CoreErrorCode, CoreOpcode, LAST_PREDEFINED_ATOM, NONE_ATOM, PREDEFINED_ATOMS, SCREEN_MODEL,
    wire_sequence,
};

#[test]
fn screen_model_matches_the_legacy_synthetic_screen() {
    assert_eq!(SCREEN_MODEL.root_window, 1);
    assert_eq!(SCREEN_MODEL.default_colormap, 2);
    assert_eq!(SCREEN_MODEL.root_visual, 3);
    assert_eq!(SCREEN_MODEL.root_depth, 24);
    assert_eq!(
        (SCREEN_MODEL.width_pixels, SCREEN_MODEL.height_pixels),
        (1024, 768)
    );
    assert_eq!(
        (
            SCREEN_MODEL.width_millimeters,
            SCREEN_MODEL.height_millimeters
        ),
        (270, 203)
    );
    assert_eq!(SCREEN_MODEL.red_mask, 0x00ff_0000);
    assert_eq!(SCREEN_MODEL.green_mask, 0x0000_ff00);
    assert_eq!(SCREEN_MODEL.blue_mask, 0x0000_00ff);
    assert_eq!(SCREEN_MODEL.maximum_request_length, 65_535);
    assert_eq!(SCREEN_MODEL.resource_id_mask, 0x001f_ffff);
    assert_eq!(SCREEN_MODEL.refresh_millihertz, 60_000);
}

#[test]
fn predefined_atom_table_matches_the_core_protocol_range() {
    assert_eq!(NONE_ATOM, 0);
    assert_eq!(LAST_PREDEFINED_ATOM, 68);
    assert_eq!(PREDEFINED_ATOMS.len(), 68);
    for (index, atom) in PREDEFINED_ATOMS.iter().enumerate() {
        assert_eq!(atom.id, index as u32 + 1);
    }
    assert_eq!(PREDEFINED_ATOMS[0].name, "PRIMARY");
    assert_eq!(PREDEFINED_ATOMS[30].name, "STRING");
    assert_eq!(PREDEFINED_ATOMS[32].name, "WINDOW");
    assert_eq!(PREDEFINED_ATOMS[66].name, "WM_CLASS");
    assert_eq!(PREDEFINED_ATOMS[67].name, "WM_TRANSIENT_FOR");
}

#[test]
fn supported_opcode_and_error_metadata_retains_wire_numbers() {
    assert_eq!(CoreOpcode::CreateWindow as u8, 1);
    assert_eq!(CoreOpcode::InternAtom as u8, 16);
    assert_eq!(CoreOpcode::ChangeProperty as u8, 18);
    assert_eq!(CoreOpcode::GetInputFocus as u8, 43);
    assert_eq!(CoreOpcode::PutImage as u8, 72);
    assert_eq!(CoreOpcode::QueryExtension as u8, 98);
    assert_eq!(CoreOpcode::NoOperation as u8, 127);
    assert_eq!(CoreErrorCode::BadRequest as u8, 1);
    assert_eq!(CoreErrorCode::BadAtom as u8, 5);
    assert_eq!(CoreErrorCode::BadColormap as u8, 12);
    assert_eq!(CoreErrorCode::BadLength as u8, 16);
    assert_eq!(CoreErrorCode::BadImplementation as u8, 17);
    assert_eq!(wire_sequence(0xffff), 0xffff);
    assert_eq!(wire_sequence(0x1_0000), 0);
    assert_eq!(wire_sequence(0x1_0001), 1);
}
