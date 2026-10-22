use super::*;

#[test]
fn preparation_counts_only_completed_input_of_its_own_generation() {
    let state = RecordStateMachine::new();
    state.begin_measure_preparation(1);
    state.note_measure_preparation_samples(9_600);
    assert_eq!(state.measure_preparation_samples(1), 9_600);
    assert_eq!(state.measure_preparation_samples(2), 0);
    assert_eq!(state.measure_preparation_samples(0), 0);
    state.begin_measure_preparation(2);
    assert_eq!(state.measure_preparation_samples(1), 0);
    assert_eq!(state.measure_preparation_samples(2), 0);
    state.note_measure_preparation_samples(4_800);
    state.begin_measure_preparation(2);
    assert_eq!(state.measure_preparation_samples(2), 4_800);
    assert_eq!(
        state.measure_progress(),
        0,
        "the Stop seal has a separate progress source"
    );
    assert_eq!(
        state.measure_ready_generation(),
        0,
        "progress is not readiness"
    );
}
