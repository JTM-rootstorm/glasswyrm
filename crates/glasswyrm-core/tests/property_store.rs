use glasswyrm_core::property::{
    AtomId, Property, PropertyData, PropertyLimits, PropertyMode, PropertyMutationStatus,
    PropertyReadStatus,
};
use glasswyrm_core::resource_id::{ClientResourceRange, ResourceBase, ResourceMask};
use glasswyrm_core::window::{
    ClientId, CreateWindowStatus, WindowAttributes, WindowClass, WindowCreateSpec, WindowGeometry,
    WindowId, WindowStore,
};

const BASE: u32 = 0x0040_0000;
const WINDOW: WindowId = WindowId::new(BASE + 1);
const PROPERTY: AtomId = AtomId::new(39);
const STRING: AtomId = AtomId::new(31);

fn property(property_type: AtomId, data: PropertyData) -> Property {
    Property {
        property_type,
        data,
    }
}

fn store(limits: PropertyLimits) -> WindowStore {
    let mut store = WindowStore::new(Default::default(), limits);
    let status = store.create_window(
        ClientId::new(1),
        ClientResourceRange::new(ResourceBase::new(BASE), ResourceMask::new(0x001f_ffff)),
        WindowCreateSpec {
            xid: WINDOW,
            parent: WindowId::new(1),
            geometry: WindowGeometry {
                x: 0,
                y: 0,
                width: 10,
                height: 10,
                border_width: 0,
            },
            depth: 0,
            window_class: WindowClass::CopyFromParent,
            visual: 0,
            attribute_mask: 0,
            attributes: WindowAttributes::default(),
        },
    );
    assert_eq!(status, CreateWindowStatus::Success);
    store
}

#[test]
fn replace_append_and_prepend_preserve_type_format_and_order() {
    let mut store = store(PropertyLimits::default());
    let mutations = [
        (
            PropertyMode::Replace,
            PropertyData::U8(b"abc".to_vec()),
            b"abc".as_slice(),
        ),
        (
            PropertyMode::Append,
            PropertyData::U8(b"d".to_vec()),
            b"abcd".as_slice(),
        ),
        (
            PropertyMode::Prepend,
            PropertyData::U8(b"0".to_vec()),
            b"0abcd".as_slice(),
        ),
    ];
    for (mode, data, expected) in mutations {
        assert_eq!(
            store.change_property(WINDOW, PROPERTY, property(STRING, data), mode),
            PropertyMutationStatus::Success
        );
        assert_eq!(
            store
                .window(WINDOW)
                .unwrap()
                .property(PROPERTY)
                .unwrap()
                .data,
            PropertyData::U8(expected.to_vec())
        );
    }
    assert_eq!(store.total_property_bytes(), 5);

    let mismatches = [
        property(AtomId::new(19), PropertyData::U8(vec![1])),
        property(STRING, PropertyData::U16(vec![1])),
    ];
    for value in mismatches {
        assert_eq!(
            store.change_property(WINDOW, PROPERTY, value, PropertyMode::Append),
            PropertyMutationStatus::BadMatch
        );
    }
    assert_eq!(store.total_property_bytes(), 5);
    assert!(store.invariants_hold());
}

#[test]
fn reads_use_four_byte_units_and_delete_only_the_final_matching_slice() {
    let mut store = store(PropertyLimits::default());
    assert_eq!(
        store.change_property(
            WINDOW,
            PROPERTY,
            property(STRING, PropertyData::U8(b"0abcd".to_vec())),
            PropertyMode::Replace,
        ),
        PropertyMutationStatus::Success
    );

    let mismatch = store.get_property(WINDOW, PROPERTY, Some(AtomId::new(6)), true, 0, 10);
    assert_eq!(mismatch.status, PropertyReadStatus::Success);
    assert!(mismatch.present);
    assert!(!mismatch.type_matched);
    assert!(!mismatch.deleted);
    assert_eq!(mismatch.value.unwrap().bytes_after, 5);
    assert_eq!(store.total_property_bytes(), 5);

    let prefix = store.get_property(WINDOW, PROPERTY, Some(STRING), true, 0, 1);
    assert_eq!(
        prefix.value.unwrap().data,
        PropertyData::U8(b"0abc".to_vec())
    );
    assert!(!prefix.deleted);
    assert_eq!(store.total_property_bytes(), 5);

    let tail = store.get_property(WINDOW, PROPERTY, Some(STRING), true, 1, 10);
    let tail_value = tail.value.unwrap();
    assert_eq!(tail_value.property_type, STRING);
    assert_eq!(tail_value.format, 8);
    assert_eq!(tail_value.bytes_after, 0);
    assert_eq!(tail_value.data, PropertyData::U8(b"d".to_vec()));
    assert!(tail.deleted);
    assert_eq!(store.total_property_bytes(), 0);
    assert!(store.window(WINDOW).unwrap().property(PROPERTY).is_none());
}

#[test]
fn property_formats_slice_at_element_boundaries() {
    let cases = [
        (
            AtomId::new(40),
            property(AtomId::new(19), PropertyData::U16(vec![1, 2, 3])),
            16,
            PropertyData::U16(vec![3]),
        ),
        (
            AtomId::new(41),
            property(AtomId::new(6), PropertyData::U32(vec![1, 2, 3])),
            32,
            PropertyData::U32(vec![2]),
        ),
    ];
    for (atom, value, format, expected) in cases {
        let mut store = store(PropertyLimits::default());
        assert_eq!(
            store.change_property(WINDOW, atom, value, PropertyMode::Replace),
            PropertyMutationStatus::Success
        );
        let read = store.get_property(WINDOW, atom, None, false, 1, 1);
        let slice = read.value.unwrap();
        assert_eq!(slice.format, format);
        assert_eq!(slice.data, expected);
    }
}

#[test]
fn absent_invalid_offset_delete_and_sorted_listing_match_legacy_behavior() {
    let mut store = store(PropertyLimits::default());
    let absent = store.get_property(WINDOW, PROPERTY, None, false, 0, 1);
    assert_eq!(absent.status, PropertyReadStatus::Success);
    assert!(!absent.present);
    assert_eq!(
        store
            .get_property(WindowId::new(99), PROPERTY, None, false, 0, 1)
            .status,
        PropertyReadStatus::BadWindow
    );

    for atom in [AtomId::new(41), AtomId::new(39), AtomId::new(40)] {
        assert_eq!(
            store.change_property(
                WINDOW,
                atom,
                property(STRING, PropertyData::U8(vec![1, 2, 3])),
                PropertyMode::Replace,
            ),
            PropertyMutationStatus::Success
        );
    }
    assert_eq!(
        store.list_properties(WINDOW),
        [AtomId::new(39), AtomId::new(40), AtomId::new(41)]
    );
    assert!(store.list_properties(WindowId::new(99)).is_empty());
    let invalid = store.get_property(WINDOW, PROPERTY, None, true, 1, 1);
    assert_eq!(invalid.status, PropertyReadStatus::BadValue);
    assert!(store.window(WINDOW).unwrap().property(PROPERTY).is_some());
    assert!(store.delete_property(WINDOW, AtomId::new(40)));
    assert!(store.delete_property(WINDOW, AtomId::new(40)));
    assert!(!store.delete_property(WindowId::new(99), AtomId::new(40)));
    assert_eq!(store.total_property_bytes(), 6);
    assert!(store.invariants_hold());
}

#[test]
fn property_limits_reject_without_partial_mutation() {
    let mut store = store(PropertyLimits {
        maximum_bytes_per_property: 4,
        maximum_total_property_bytes: 6,
        maximum_properties_per_window: 2,
    });
    let cases = [
        (
            PROPERTY,
            PropertyData::U8(vec![1, 2, 3]),
            PropertyMode::Replace,
            PropertyMutationStatus::Success,
        ),
        (
            PROPERTY,
            PropertyData::U8(vec![4, 5]),
            PropertyMode::Append,
            PropertyMutationStatus::BadAlloc,
        ),
        (
            AtomId::new(40),
            PropertyData::U8(vec![4, 5, 6]),
            PropertyMode::Replace,
            PropertyMutationStatus::Success,
        ),
        (
            AtomId::new(41),
            PropertyData::U8(Vec::new()),
            PropertyMode::Replace,
            PropertyMutationStatus::BadAlloc,
        ),
        (
            AtomId::new(40),
            PropertyData::U8(vec![4, 5, 6, 7]),
            PropertyMode::Replace,
            PropertyMutationStatus::BadAlloc,
        ),
    ];
    for (atom, data, mode, expected) in cases {
        assert_eq!(
            store.change_property(WINDOW, atom, property(STRING, data), mode),
            expected
        );
    }
    assert_eq!(store.total_property_bytes(), 6);
    assert_eq!(
        store
            .window(WINDOW)
            .unwrap()
            .property(PROPERTY)
            .unwrap()
            .data,
        PropertyData::U8(vec![1, 2, 3])
    );
    assert_eq!(
        store
            .window(WINDOW)
            .unwrap()
            .property(AtomId::new(40))
            .unwrap()
            .data,
        PropertyData::U8(vec![4, 5, 6])
    );
    assert!(store.invariants_hold());
}
