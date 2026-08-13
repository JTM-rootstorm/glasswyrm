use std::collections::BTreeMap;

use gw_types::{Generation, OutputId, WindowId};
use gwm_core::{
    Context, DecorationPreference, OutputContext, OutputSelection, RawState, RawWindow, Rectangle,
    StackMode, VrrReason, VrrWindowInput, VrrWindowPreference, WindowOutputHint,
    classify_vrr_window, evaluate, select_output,
};

fn window(id: u32, serial: u64) -> RawWindow {
    RawWindow::mapped(WindowId::new(id), WindowId::new(1), serial)
}

fn single_state() -> RawState {
    RawState {
        complete: true,
        context: Context {
            root_window_id: WindowId::new(1),
            workspace_id: 1,
            primary_output_id: OutputId::new(7),
            work: Rectangle {
                x: 100,
                y: 50,
                width: 640,
                height: 480,
            },
        },
        ..RawState::default()
    }
}

fn output(id: u64, x: i32, work_height: u32, primary: bool) -> OutputContext {
    OutputContext {
        output_id: OutputId::new(id),
        logical: Rectangle {
            x,
            y: 0,
            width: 800,
            height: 600,
        },
        work: Rectangle {
            x,
            y: 0,
            width: 800,
            height: work_height,
        },
        enabled: true,
        primary,
    }
}

fn multi_state() -> RawState {
    RawState {
        complete: true,
        context: Context {
            root_window_id: WindowId::new(1),
            workspace_id: 1,
            primary_output_id: OutputId::new(10),
            work: Rectangle {
                width: 1_600,
                height: 600,
                ..Rectangle::default()
            },
        },
        outputs: BTreeMap::from([
            (OutputId::new(10), output(10, 0, 560, true)),
            (OutputId::new(20), output(20, 800, 600, false)),
        ]),
        ..RawState::default()
    }
}

#[test]
fn accepted_legacy_placement_focus_and_transient_fixtures_match() {
    struct ExpectedWindow {
        id: u32,
        output_id: u64,
        x: i32,
        y: i32,
        focused: bool,
        stacking: Option<u32>,
    }
    struct Fixture {
        name: &'static str,
        state: RawState,
        expected: &'static [ExpectedWindow],
    }

    let mut cascade = single_state();
    cascade.windows.insert(WindowId::new(10), window(10, 1));
    cascade.windows.insert(WindowId::new(20), window(20, 2));

    let mut transient = single_state();
    transient.windows.insert(WindowId::new(10), window(10, 1));
    let mut dialog = window(30, 3);
    dialog.transient_for = Some(WindowId::new(10));
    dialog.requested.width = 100;
    dialog.requested.height = 60;
    dialog.focus_serial = 4;
    transient.windows.insert(WindowId::new(30), dialog);

    let mut multi = multi_state();
    for (id, serial) in [(1_001, 1), (1_002, 2), (1_003, 3), (1_004, 4)] {
        multi.windows.insert(WindowId::new(id), window(id, serial));
    }
    for id in [1_003, 1_004] {
        multi.output_hints.insert(
            WindowId::new(id),
            WindowOutputHint {
                preferred_output_id: OutputId::new(20),
                ..WindowOutputHint::default()
            },
        );
    }

    let fixtures = [
        Fixture {
            name: "legacy single-output 32px cascade and fallback focus",
            state: cascade,
            expected: &[
                ExpectedWindow {
                    id: 10,
                    output_id: 7,
                    x: 100,
                    y: 50,
                    focused: false,
                    stacking: Some(0),
                },
                ExpectedWindow {
                    id: 20,
                    output_id: 7,
                    x: 132,
                    y: 82,
                    focused: true,
                    stacking: Some(1),
                },
            ],
        },
        Fixture {
            name: "legacy transient centering and focus serial",
            state: transient,
            expected: &[
                ExpectedWindow {
                    id: 10,
                    output_id: 7,
                    x: 100,
                    y: 50,
                    focused: false,
                    stacking: Some(0),
                },
                ExpectedWindow {
                    id: 30,
                    output_id: 7,
                    x: 150,
                    y: 70,
                    focused: true,
                    stacking: Some(1),
                },
            ],
        },
        Fixture {
            name: "legacy per-output independent cascade",
            state: multi,
            expected: &[
                ExpectedWindow {
                    id: 1_001,
                    output_id: 10,
                    x: 0,
                    y: 0,
                    focused: false,
                    stacking: Some(0),
                },
                ExpectedWindow {
                    id: 1_002,
                    output_id: 10,
                    x: 32,
                    y: 32,
                    focused: false,
                    stacking: Some(1),
                },
                ExpectedWindow {
                    id: 1_003,
                    output_id: 20,
                    x: 800,
                    y: 0,
                    focused: false,
                    stacking: Some(2),
                },
                ExpectedWindow {
                    id: 1_004,
                    output_id: 20,
                    x: 832,
                    y: 32,
                    focused: true,
                    stacking: Some(3),
                },
            ],
        },
    ];

    for fixture in fixtures {
        let policy = evaluate(&fixture.state, Generation::new(1)).expect(fixture.name);
        for expected in fixture.expected {
            let state = &policy.windows[&WindowId::new(expected.id)];
            assert_eq!(
                state.output_id,
                OutputId::new(expected.output_id),
                "{}",
                fixture.name
            );
            assert_eq!(
                (state.geometry.x, state.geometry.y),
                (expected.x, expected.y),
                "{}",
                fixture.name
            );
            assert_eq!(state.focused, expected.focused, "{}", fixture.name);
            assert_eq!(state.stacking, expected.stacking, "{}", fixture.name);
        }
    }
}

#[test]
fn accepted_legacy_output_tie_break_fixtures_match() {
    let outputs = multi_state().outputs;
    let geometry = Rectangle {
        x: 700,
        width: 200,
        height: 100,
        ..Rectangle::default()
    };
    let fixtures = [
        ("previous wins intersection tie", 10, 20, 10),
        ("preferred wins after stale previous", 99, 20, 20),
        ("primary wins after stale hints", 99, 99, 10),
    ];
    for (name, previous, preferred, expected) in fixtures {
        assert_eq!(
            select_output(
                &outputs,
                OutputId::new(10),
                OutputSelection {
                    geometry,
                    previous_output_id: OutputId::new(previous),
                    preferred_output_id: OutputId::new(preferred),
                    ..OutputSelection::default()
                }
            ),
            OutputId::new(expected),
            "{name}"
        );
    }
}

#[test]
fn accepted_legacy_restack_fixtures_match() {
    struct Fixture {
        name: &'static str,
        target: u32,
        serial: u64,
        sibling: Option<u32>,
        mode: StackMode,
        expected: &'static [u32],
    }
    let fixtures = [
        Fixture {
            name: "Above without sibling moves to band top",
            target: 10,
            serial: 1,
            sibling: None,
            mode: StackMode::Above,
            expected: &[20, 30, 10],
        },
        Fixture {
            name: "Below without sibling moves to band bottom",
            target: 30,
            serial: 1,
            sibling: None,
            mode: StackMode::Below,
            expected: &[30, 10, 20],
        },
        Fixture {
            name: "Above sibling inserts immediately above",
            target: 10,
            serial: 1,
            sibling: Some(20),
            mode: StackMode::Above,
            expected: &[20, 10, 30],
        },
    ];
    for fixture in fixtures {
        let mut raw = single_state();
        for (id, serial) in [(10, 1), (20, 2), (30, 3)] {
            raw.windows.insert(WindowId::new(id), window(id, serial));
        }
        let target = raw
            .windows
            .get_mut(&WindowId::new(fixture.target))
            .expect("fixture target exists");
        target.stack_serial = fixture.serial;
        target.stack_sibling = fixture.sibling.map(WindowId::new);
        target.stack_mode = fixture.mode;
        let policy = evaluate(&raw, Generation::new(1)).expect(fixture.name);
        assert_eq!(
            policy
                .output_order
                .iter()
                .map(|id| id.get())
                .collect::<Vec<_>>(),
            fixture.expected,
            "{}",
            fixture.name
        );
    }
}

#[test]
fn vrr_classification_preserves_legacy_facts_without_claiming_output_authority() {
    let mut raw = multi_state();
    let mut candidate = window(1_001, 1);
    candidate.geometry_serial = 1;
    candidate.requested = raw.outputs[&OutputId::new(10)].logical;
    candidate.decoration_preference = DecorationPreference::False;
    raw.windows.insert(candidate.window_id, candidate);
    let policy = evaluate(&raw, Generation::new(42)).expect("valid VRR fixture");

    struct Fixture {
        name: &'static str,
        membership: &'static [u64],
        preference: VrrWindowPreference,
        expected_candidate: bool,
        expected_reason: VrrReason,
        expected_borderless: bool,
    }
    let fixtures = [
        Fixture {
            name: "exact one-output membership is a common candidate",
            membership: &[10],
            preference: VrrWindowPreference::Default,
            expected_candidate: true,
            expected_reason: VrrReason::default(),
            expected_borderless: true,
        },
        Fixture {
            name: "spanning membership has the frozen legacy reason",
            membership: &[20, 10],
            preference: VrrWindowPreference::Default,
            expected_candidate: false,
            expected_reason: VrrReason::WINDOW_SPANS_OUTPUTS,
            expected_borderless: false,
        },
        Fixture {
            name: "application disable remains a fact, not final policy",
            membership: &[10],
            preference: VrrWindowPreference::Disable,
            expected_candidate: true,
            expected_reason: VrrReason::WINDOW_PREFERENCE_DISABLED,
            expected_borderless: true,
        },
    ];
    for fixture in fixtures {
        let input = VrrWindowInput {
            window_id: WindowId::new(1_001),
            preference: fixture.preference,
            output_membership: fixture
                .membership
                .iter()
                .copied()
                .map(OutputId::new)
                .collect(),
        };
        let classified = classify_vrr_window(&raw, &policy, &input).expect(fixture.name);
        assert_eq!(
            classified.common_candidate, fixture.expected_candidate,
            "{}",
            fixture.name
        );
        assert_eq!(
            classified.reason, fixture.expected_reason,
            "{}",
            fixture.name
        );
        assert_eq!(
            classified.borderless_fullscreen, fixture.expected_borderless,
            "{}",
            fixture.name
        );
    }
}
