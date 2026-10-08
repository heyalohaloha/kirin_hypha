use super::*;
use crate::KirinHyphaEngine;

#[test]
fn u32_aligned_future_prefix_precedes_v2_alignment_and_preserves_token() {
    #[repr(C, align(8))]
    struct Storage([u32; 32]);
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let mut storage = Storage([0; 32]);
    storage.0[1] = 3;
    storage.0[2] = 128;
    let request = unsafe { storage.0.as_ptr().add(1) }.cast::<KirinAttackSingleV2Request>();
    assert_eq!(request as usize % 8, 4);
    let mut token = 0xa5a5a5a5a5a5a5a5;
    for size in [4, 8, 95, 96, 128] {
        assert_eq!(
            unsafe { kirin_hypha_request_attack_single_v2(&engine, size, request, &mut token) },
            KIRIN_SNAPSHOT_UNSUPPORTED
        );
        assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
    }
    storage.0[1] = 2;
    let request = unsafe { storage.0.as_ptr().add(1) }.cast::<KirinAttackSingleV2Request>();
    assert_eq!(
        unsafe { kirin_hypha_request_attack_single_v2(&engine, 96, request, &mut token) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(token, 0xa5a5a5a5a5a5a5a5);
}

#[test]
fn poisoned_single_control_retires_once_and_allows_recovery_instead_of_endless_busy() {
    for first_token in [31, 32] {
        let (engine, _) = tests::selected_fixture();
        let runtime = engine.attack_runtime.as_ref().unwrap();
        runtime.fixture_poison_single();
        let mut storage = vec![0xa5a5a5a5a5a5a5a5_u64; 4560 / 8 + 2];
        let before = storage.clone();
        let out = storage.as_mut_ptr().cast::<KirinAttackSingleSnapshotV2>();
        let status = unsafe { kirin_hypha_poll_attack_single_v2(&engine, first_token, 4560, out) };
        assert_eq!(
            status,
            if first_token == 31 {
                KIRIN_SNAPSHOT_SUCCESS
            } else {
                KIRIN_SNAPSHOT_RETIRED
            }
        );
        if first_token == 32 {
            assert_eq!(storage, before);
        }
        let retired = engine.attack_single_v2(31).unwrap();
        assert_eq!(retired.request_token, 31);
        assert_eq!(
            (retired.finish, retired.reason),
            (KIRIN_FINISH_RETIRED, KIRIN_REASON_WORKER_UNAVAILABLE)
        );
        assert_eq!(retired.has_all_post, 0);
        assert_eq!(engine.attack_single_v2(31).unwrap(), retired);
        assert_eq!(&storage[4560 / 8..], &before[4560 / 8..]);
        assert_eq!(
            unsafe { kirin_hypha_cancel_attack_single_v2(&engine, 31) },
            KIRIN_SNAPSHOT_SUCCESS
        );
        assert_eq!(engine.attack_single_v2(31), Err(KIRIN_SNAPSHOT_RETIRED));
    }
}
