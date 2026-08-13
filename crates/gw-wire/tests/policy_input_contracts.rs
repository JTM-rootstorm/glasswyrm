use gw_wire::*;

fn hex(text: &str) -> Vec<u8> {
    text.as_bytes()
        .chunks_exact(2)
        .map(|digits| {
            let high = (digits[0] as char).to_digit(16).unwrap() as u8;
            let low = (digits[1] as char).to_digit(16).unwrap() as u8;
            (high << 4) | low
        })
        .collect()
}

fn window() -> PolicyWindowUpsert {
    PolicyWindowUpsert {
        window_id: 10,
        parent_window_id: 1,
        transient_for: 0,
        workspace_id: 2,
        requested_x: -3,
        requested_y: 4,
        requested_width: 100,
        requested_height: 80,
        border_width: 1,
        window_type: PolicyWindowType::Dialog,
        map_intent: PolicyMapIntent::WantsMap,
        override_redirect: false,
        decoration_preference: 2,
        fullscreen_requested: true,
        maximized_requested: false,
        minimized_requested: false,
        attention_requested: true,
        creation_serial: 11,
        map_serial: 12,
        focus_serial: 13,
        flags: 0,
    }
}

#[test]
fn policy_payloads_match_legacy_goldens() {
    let context = PolicyContextUpsert {
        root_window_id: 1,
        workspace_id: 2,
        output_id: 3,
        work_x: -4,
        work_y: 5,
        work_width: 640,
        work_height: 480,
        flags: 0,
    };
    assert_eq!(
        encode_policy_context_upsert(&context),
        hex("01000000020000000300000000000000fcffffff0500000080020000e00100000000000000000000")
    );
    assert_eq!(
        encode_policy_window_upsert(&window()),
        hex(
            "0a000000010000000000000002000000fdffffff040000006400000050000000010000000200010000020100000100000b000000000000000c000000000000000d000000000000000000000000000000"
        )
    );
    assert_eq!(
        encode_policy_window_remove(&PolicyWindowRemove { window_id: 10 }),
        hex("0a00000000000000")
    );
    assert_eq!(
        encode_policy_commit(&PolicyCommit {
            commit_id: 20,
            producer_generation: 2,
            flags: 0,
        }),
        hex("140000000000000002000000000000000000000000000000")
    );

    let state = PolicyWindowState {
        window_id: 10,
        transient_for: 0,
        workspace_id: 2,
        output_id: 3,
        final_x: -4,
        final_y: 5,
        final_width: 100,
        final_height: 80,
        stacking: 0,
        window_type: PolicyWindowType::Normal,
        applied_state: PolicyAppliedState::Normal,
        visible: true,
        focused: true,
        managed: true,
        decoration_eligible: true,
        override_redirect: false,
        attention_requested: true,
        fullscreen_eligible: 2,
        direct_scanout_eligible: 0,
        flags: 0,
    };
    assert_eq!(
        encode_policy_window_state(&state),
        hex(
            "0a0000000000000002000000000000000300000000000000fcffffff050000006400000050000000000000000100010001010101000102000000000000000000"
        )
    );

    let acknowledged = PolicyAcknowledged {
        commit_id: 20,
        producer_generation: 2,
        applied_generation: 3,
        policy_hash: 0x0102_0304_0506_0708,
        window_count: 1,
        result: PolicyResult::Accepted,
    };
    assert_eq!(
        encode_policy_acknowledged(&acknowledged),
        hex("14000000000000000200000000000000030000000000000008070605040302010100000001000000")
    );

    let bindings = PolicyBindingsUpsert {
        move_modifiers: 8,
        resize_modifiers: 8,
        close_modifiers: 8,
        move_button: 1,
        resize_button: 3,
        close_keysym: 0xffc1,
        minimum_width: 96,
        minimum_height: 64,
        raise_on_focus: true,
        consume_wm_bindings: true,
    };
    assert_eq!(
        encode_policy_bindings_upsert(&bindings),
        hex("080008000800000001030000c1ff000060000000400000000101000000000000")
    );
}

#[test]
fn policy_decoders_reject_every_truncated_prefix_and_trailing_data() {
    let bytes = encode_policy_window_upsert(&window());
    for length in 0..bytes.len() {
        assert_eq!(
            decode_policy_window_upsert(&bytes[..length]),
            Err(PolicyDecodeError::Truncated),
            "length {length}"
        );
    }
    let mut trailing = bytes;
    trailing.push(0);
    assert_eq!(
        decode_policy_window_upsert(&trailing),
        Err(PolicyDecodeError::TrailingData)
    );
}

#[test]
fn policy_decoders_reject_noncanonical_and_invalid_fields() {
    let mut bytes = encode_policy_window_upsert(&window());
    bytes[44] = 2;
    assert_eq!(
        decode_policy_window_upsert(&bytes),
        Err(PolicyDecodeError::InvalidValue)
    );
    bytes = encode_policy_window_upsert(&window());
    bytes[36] = 0xff;
    assert_eq!(
        decode_policy_window_upsert(&bytes),
        Err(PolicyDecodeError::InvalidValue)
    );
    bytes = encode_policy_window_upsert(&window());
    bytes[76] = 8;
    assert_eq!(
        decode_policy_window_upsert(&bytes),
        Err(PolicyDecodeError::InvalidValue)
    );

    let bindings = PolicyBindingsUpsert {
        move_modifiers: 0x100,
        resize_modifiers: 8,
        close_modifiers: 8,
        move_button: 1,
        resize_button: 3,
        close_keysym: 0xffc1,
        minimum_width: 96,
        minimum_height: 64,
        raise_on_focus: true,
        consume_wm_bindings: true,
    };
    assert_eq!(
        decode_policy_bindings_upsert(&encode_policy_bindings_upsert(&bindings)),
        Err(PolicyDecodeError::InvalidValue)
    );
}

#[test]
fn lifecycle_payloads_match_legacy_goldens_and_validate_stack_intent() {
    let lifecycle = PolicyLifecycleWindowUpsert {
        window: window(),
        geometry_serial: 14,
        stack_serial: 15,
        stack_sibling: 16,
        stack_mode: PolicyStackMode::Above,
        flags: 0,
    };
    let golden = hex(
        "0a000000010000000000000002000000fdffffff040000006400000050000000010000000200010000020100000100000b000000000000000c000000000000000d0000000000000000000000000000000e000000000000000f0000000000000010000000010000000000000000000000",
    );
    assert_eq!(encode_policy_lifecycle_window_upsert(&lifecycle), golden);
    assert_eq!(
        decode_policy_lifecycle_window_upsert(&golden),
        Ok(lifecycle)
    );
    for length in 0..golden.len() {
        assert_eq!(
            decode_policy_lifecycle_window_upsert(&golden[..length]),
            Err(LifecycleDecodeError::Truncated)
        );
    }

    let invalid = PolicyLifecycleWindowUpsert {
        stack_serial: 0,
        ..lifecycle
    };
    assert_eq!(
        decode_policy_lifecycle_window_upsert(&encode_policy_lifecycle_window_upsert(&invalid)),
        Err(LifecycleDecodeError::InvalidValue)
    );

    let surface = SurfacePolicyUpsert {
        surface_id: 17,
        x11_window_id: 10,
        workspace_id: 2,
        window_type: PolicyWindowType::Dialog,
        applied_state: PolicyAppliedState::Normal,
        focused: true,
        managed: true,
        decoration_eligible: true,
        override_redirect: false,
        attention_requested: true,
        fullscreen_eligible: 2,
        direct_scanout_eligible: 0,
        flags: 0,
    };
    let surface_golden =
        hex("11000000000000000a000000020000000200010001010100010200000000000000000000");
    assert_eq!(encode_surface_policy_upsert(&surface), surface_golden);
    assert_eq!(decode_surface_policy_upsert(&surface_golden), Ok(surface));
    let mut bad_boolean = surface_golden;
    bad_boolean[20] = 2;
    assert_eq!(
        decode_surface_policy_upsert(&bad_boolean),
        Err(LifecycleDecodeError::InvalidValue)
    );
}

#[test]
fn input_and_session_payloads_match_legacy_goldens() {
    let motion = SyntheticMotion {
        input_id: 7,
        time_ms: 11,
        root_x: -2,
        root_y: 300,
        flags: 0,
    };
    let motion_golden = vec![
        7, 0, 0, 0, 0, 0, 0, 0, 11, 0, 0, 0, 254, 255, 255, 255, 44, 1, 0, 0, 0, 0, 0, 0,
    ];
    assert_eq!(encode_synthetic_motion(&motion), motion_golden);
    assert_eq!(decode_synthetic_motion(&motion_golden), Ok(motion));

    let change = SessionStateChange {
        generation: 0x0102_0304_0506_0708,
        state: SessionState::Active,
        flags: 0,
    };
    let change_golden = vec![8, 7, 6, 5, 4, 3, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0];
    assert_eq!(encode_session_state_change(&change), change_golden);
    assert_eq!(decode_session_state_change(&change_golden), Ok(change));

    let acknowledged = SessionStateAcknowledged {
        generation: 9,
        state: SessionState::Inactive,
        result: SessionStateResult::InputUnavailable,
        flags: 0,
    };
    let ack_golden = vec![9, 0, 0, 0, 0, 0, 0, 0, 1, 0, 3, 0, 0, 0, 0, 0];
    assert_eq!(encode_session_state_acknowledged(&acknowledged), ack_golden);
    assert_eq!(
        decode_session_state_acknowledged(&ack_golden),
        Ok(acknowledged)
    );
}

#[test]
fn input_and_session_decoders_reject_malformed_values() {
    let button = SyntheticButton {
        input_id: 9,
        time_ms: 12,
        button: 5,
        pressed: true,
        flags: 0,
    };
    let bytes = encode_synthetic_button(&button);
    for length in 0..bytes.len() {
        assert_eq!(
            decode_synthetic_button(&bytes[..length]),
            Err(InputDecodeError::Truncated)
        );
    }
    let mut invalid_bool = bytes.clone();
    invalid_bool[13] = 2;
    assert_eq!(
        decode_synthetic_button(&invalid_bool),
        Err(InputDecodeError::InvalidValue)
    );
    let mut trailing = bytes;
    trailing.push(0);
    assert_eq!(
        decode_synthetic_button(&trailing),
        Err(InputDecodeError::TrailingData)
    );

    let barrier = SyntheticBarrier {
        input_id: 11,
        flags: 0,
    };
    let mut invalid_barrier = encode_synthetic_barrier(&barrier);
    invalid_barrier[0..8].fill(0);
    assert_eq!(
        decode_synthetic_barrier(&invalid_barrier),
        Err(InputDecodeError::InvalidValue)
    );

    let change = SessionStateChange {
        generation: 1,
        state: SessionState::Inactive,
        flags: 0,
    };
    let mut invalid_state = encode_session_state_change(&change);
    invalid_state[8] = 3;
    assert_eq!(
        decode_session_state_change(&invalid_state),
        Err(SessionDecodeError::InvalidValue)
    );
    let mut trailing_session = encode_session_state_change(&change);
    trailing_session.push(0);
    assert_eq!(
        decode_session_state_change(&trailing_session),
        Err(SessionDecodeError::TrailingData)
    );
}
