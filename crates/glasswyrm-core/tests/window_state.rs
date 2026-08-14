use glasswyrm_core::property::{AtomId, Property, PropertyData, PropertyLimits, PropertyMode};
use glasswyrm_core::resource_id::{ClientResourceRange, ResourceBase, ResourceMask};
use glasswyrm_core::window::{
    ClientId, ConfigureWindow, CreateWindowStatus, DestroyWindowStatus, LifecycleStatus, MapState,
    StackMode, WindowAttributes, WindowClass, WindowCreateSpec, WindowGeometry, WindowId,
    WindowStore,
};

const BASE_A: u32 = 0x0040_0000;
const BASE_B: u32 = 0x0080_0000;
const MASK: u32 = 0x001f_ffff;

fn range(base: u32) -> ClientResourceRange {
    ClientResourceRange::new(ResourceBase::new(base), ResourceMask::new(MASK))
}

fn spec(xid: u32, parent: u32) -> WindowCreateSpec {
    WindowCreateSpec {
        xid: WindowId::new(xid),
        parent: WindowId::new(parent),
        geometry: WindowGeometry {
            x: -4,
            y: 7,
            width: 80,
            height: 60,
            border_width: 0,
        },
        depth: 0,
        window_class: WindowClass::CopyFromParent,
        visual: 0,
        attribute_mask: 0,
        attributes: WindowAttributes::default(),
    }
}

fn create(store: &mut WindowStore, owner: u64, xid: u32, parent: u32) {
    let base = if owner == 2 { BASE_B } else { BASE_A };
    assert_eq!(
        store.create_window(ClientId::new(owner), range(base), spec(xid, parent)),
        CreateWindowStatus::Success
    );
}

#[test]
fn creation_validates_resource_ownership_and_window_shape() {
    let mut store = WindowStore::default();
    let owner = ClientId::new(1);
    let cases = [
        (
            range(BASE_A),
            spec(BASE_B + 1, 1),
            CreateWindowStatus::BadIdChoice,
        ),
        (
            range(BASE_A),
            spec(BASE_A + 1, 99),
            CreateWindowStatus::BadWindow,
        ),
        (
            range(BASE_A),
            WindowCreateSpec {
                geometry: WindowGeometry {
                    width: 0,
                    ..spec(BASE_A + 1, 1).geometry
                },
                ..spec(BASE_A + 1, 1)
            },
            CreateWindowStatus::BadValue,
        ),
        (
            range(BASE_A),
            WindowCreateSpec {
                window_class: WindowClass::InputOnly,
                depth: 24,
                ..spec(BASE_A + 1, 1)
            },
            CreateWindowStatus::BadMatch,
        ),
    ];
    for (range, spec, expected) in cases {
        assert_eq!(store.create_window(owner, range, spec), expected);
        assert_eq!(store.window_count(), 1);
    }

    assert_eq!(
        store.create_window(owner, range(BASE_A), spec(BASE_A + 1, 1)),
        CreateWindowStatus::Success
    );
    assert_eq!(
        store.create_window(owner, range(BASE_A), spec(BASE_A + 1, 1)),
        CreateWindowStatus::BadIdChoice
    );
    assert_eq!(store.window_count_by_owner(owner), 1);
    assert_eq!(
        store.window(WindowId::new(BASE_A + 1)).unwrap().owner(),
        Some(owner)
    );
    assert!(store.invariants_hold());
}

#[test]
fn children_preserve_creation_order_and_recursive_destroy_is_postorder() {
    let mut store = WindowStore::default();
    create(&mut store, 1, BASE_A + 1, 1);
    create(&mut store, 2, BASE_B + 1, BASE_A + 1);
    create(&mut store, 1, BASE_A + 2, BASE_A + 1);
    create(&mut store, 2, BASE_B + 2, BASE_B + 1);
    assert_eq!(
        store.window(WindowId::new(BASE_A + 1)).unwrap().children(),
        &[WindowId::new(BASE_B + 1), WindowId::new(BASE_A + 2)]
    );

    assert_eq!(
        store.change_property(
            WindowId::new(BASE_B + 1),
            AtomId::new(39),
            Property {
                property_type: AtomId::new(31),
                data: PropertyData::U8(vec![1, 2, 3]),
            },
            PropertyMode::Replace,
        ),
        glasswyrm_core::property::PropertyMutationStatus::Success
    );
    let result = store.destroy_window(WindowId::new(BASE_A + 1));
    assert_eq!(result.status, DestroyWindowStatus::Success);
    assert_eq!(
        result.destroyed,
        [
            WindowId::new(BASE_A + 2),
            WindowId::new(BASE_B + 2),
            WindowId::new(BASE_B + 1),
            WindowId::new(BASE_A + 1),
        ]
    );
    assert_eq!(result.property_bytes_released, 3);
    assert_eq!(store.window_count_by_owner(ClientId::new(1)), 0);
    assert_eq!(store.window_count_by_owner(ClientId::new(2)), 0);
    assert_eq!(store.window(WindowId::new(1)).unwrap().children(), &[]);
    assert_eq!(
        store.destroy_window(WindowId::new(1)).status,
        DestroyWindowStatus::RootPreserved
    );
    assert!(store.invariants_hold());
}

#[test]
fn configure_geometry_and_sibling_stacking_match_the_legacy_order() {
    let mut store = WindowStore::default();
    create(&mut store, 1, BASE_A + 1, 1);
    for index in 2..=4 {
        create(&mut store, 1, BASE_A + index, BASE_A + 1);
    }
    let parent = WindowId::new(BASE_A + 1);
    let w2 = WindowId::new(BASE_A + 2);
    let w3 = WindowId::new(BASE_A + 3);
    let w4 = WindowId::new(BASE_A + 4);

    let cases = [
        (
            w2,
            ConfigureWindow {
                stack_mode: StackMode::Above,
                ..ConfigureWindow::default()
            },
            vec![w3, w4, w2],
        ),
        (
            w2,
            ConfigureWindow {
                sibling: Some(w3),
                stack_mode: StackMode::Below,
                ..ConfigureWindow::default()
            },
            vec![w2, w3, w4],
        ),
        (
            w4,
            ConfigureWindow {
                sibling: Some(w2),
                stack_mode: StackMode::Above,
                ..ConfigureWindow::default()
            },
            vec![w2, w4, w3],
        ),
    ];
    for (window, configure, expected) in cases {
        assert_eq!(
            store.configure_local(window, configure),
            LifecycleStatus::Success
        );
        assert_eq!(store.window(parent).unwrap().children(), expected);
    }

    let before = store.window(parent).unwrap().children().to_vec();
    assert_eq!(
        store.configure_local(
            w2,
            ConfigureWindow {
                sibling: Some(w2),
                stack_mode: StackMode::Above,
                ..ConfigureWindow::default()
            }
        ),
        LifecycleStatus::BadMatch
    );
    assert_eq!(store.window(parent).unwrap().children(), before);
    assert_eq!(
        store.configure_local(
            w2,
            ConfigureWindow {
                x: Some(-5),
                width: Some(320),
                height: Some(200),
                border_width: Some(2),
                ..ConfigureWindow::default()
            }
        ),
        LifecycleStatus::Success
    );
    let window = store.window(w2).unwrap();
    assert_eq!(
        window.geometry(),
        WindowGeometry {
            x: -5,
            y: 7,
            width: 320,
            height: 200,
            border_width: 2,
        }
    );
    assert_eq!(window.requested_geometry(), window.geometry());
    assert_eq!(
        store.configure_local(
            w2,
            ConfigureWindow {
                width: Some(0),
                ..ConfigureWindow::default()
            }
        ),
        LifecycleStatus::BadValue
    );
    assert!(store.invariants_hold());
}

#[test]
fn map_state_tracks_policy_visibility_and_parent_viewability() {
    let mut store = WindowStore::default();
    let mut top = spec(BASE_A + 1, 1);
    top.window_class = WindowClass::InputOutput;
    top.attributes.override_redirect = true;
    assert_eq!(
        store.create_window(ClientId::new(1), range(BASE_A), top),
        CreateWindowStatus::Success
    );
    let mut child = spec(BASE_A + 2, BASE_A + 1);
    child.window_class = WindowClass::InputOnly;
    assert_eq!(
        store.create_window(ClientId::new(1), range(BASE_A), child),
        CreateWindowStatus::Success
    );
    let top = WindowId::new(BASE_A + 1);
    let child = WindowId::new(BASE_A + 2);
    assert!(store.window(top).unwrap().attributes().override_redirect);
    assert_eq!(
        store.set_override_redirect(top, false),
        LifecycleStatus::Success
    );
    assert!(!store.window(top).unwrap().attributes().override_redirect);
    assert_eq!(
        store.set_override_redirect(WindowId::new(1), true),
        LifecycleStatus::BadMatch
    );

    assert_eq!(
        store.set_local_map_intent(child, true),
        LifecycleStatus::Success
    );
    assert_eq!(
        store.window(child).unwrap().map_state(),
        MapState::Unviewable
    );
    assert_eq!(
        store.set_local_map_intent(top, true),
        LifecycleStatus::BadMatch
    );
    assert_eq!(store.set_map_requested(top, true), LifecycleStatus::Success);
    assert_eq!(store.window(top).unwrap().map_state(), MapState::Unmapped);
    assert_eq!(
        store.set_policy_visible(top, true),
        LifecycleStatus::Success
    );
    assert_eq!(store.window(top).unwrap().map_state(), MapState::Viewable);
    assert_eq!(store.window(child).unwrap().map_state(), MapState::Viewable);
    assert_eq!(
        store.set_map_requested(top, false),
        LifecycleStatus::Success
    );
    assert_eq!(store.window(top).unwrap().map_state(), MapState::Unmapped);
    assert_eq!(
        store.window(child).unwrap().map_state(),
        MapState::Unviewable
    );
    assert!(store.invariants_hold());
}

#[test]
fn policy_reorder_moves_only_unique_root_candidates() {
    let mut store = WindowStore::default();
    for index in 1..=3 {
        create(&mut store, 1, BASE_A + index, 1);
    }
    let first = WindowId::new(BASE_A + 1);
    let second = WindowId::new(BASE_A + 2);
    let third = WindowId::new(BASE_A + 3);
    assert!(store.reorder_root_children(&[third, first]));
    assert_eq!(
        store.window(WindowId::new(1)).unwrap().children(),
        &[second, third, first]
    );
    assert!(!store.reorder_root_children(&[first, first]));
    assert_eq!(
        store.window(WindowId::new(1)).unwrap().children(),
        &[second, third, first]
    );
}

#[test]
fn property_limits_constructor_remains_usable_with_window_state() {
    let store = WindowStore::new(
        Default::default(),
        PropertyLimits {
            maximum_bytes_per_property: 4,
            maximum_total_property_bytes: 6,
            maximum_properties_per_window: 2,
        },
    );
    assert!(store.invariants_hold());
}
