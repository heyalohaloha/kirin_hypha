use super::*;

#[test]
fn stopping_during_arm_releases_the_lane_for_the_next_keep() {
    for (role, reselection) in [
        (Some(PluginDataRole::Post), false),
        (Some(PluginDataRole::Pre), false),
        (None, false),
        (Some(PluginDataRole::Post), true),
    ] {
        let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
        // Give the control path an unclaimed lane deterministically: the live worker retains
        // its original Watch state, representing a Measure worker not scheduled during arming.
        engine.record_sm = Arc::new(RecordStateMachine::new());
        engine.record_ingress = Arc::new(RecordIngress::new(32));
        *engine.write_role.lock().unwrap() = role;
        let generation = engine.record_sm.generation() + 1;
        assert!(engine.record_ingress.prepare_for_generation(generation));
        engine.record_sm.try_enter_record(License::Os).unwrap();
        if reselection {
            engine.begin_pair_reselection();
        } else {
            engine.exit_record();
        }
        assert!(!engine.is_recording());
        engine.set_license(LICENSE_OS);
        assert!(
            engine.enter_record(),
            "Stop during arm must permit the next Keep: {role:?}, reselection {reselection}"
        );
        assert_eq!(engine.record_sm.generation(), generation + 1);
        engine.exit_record();
    }
}
