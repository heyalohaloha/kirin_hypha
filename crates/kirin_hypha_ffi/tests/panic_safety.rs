//! FFI 境界の panic-safety / 堅牢性テスト。
//!
//! 全 extern "C" 関数は `catch_unwind` で包まれており、panic が C ABI 境界を越えて UB を
//! 起こさないことを担保する（実装は src/lib.rs / コードレビューで全6関数の wrap を確認）。
//!
//! ZSA: 「どの入力が内部 panic を誘発するか」は実装依存であり、人工的な panic 経路は捏造しない。
//! 本テストは (1) 境界/異常引数で **プロセスが abort せず呼び出しが安全な失敗値で戻る**、
//! (2) 正常系が壊れていない、ことを確認する。

use kirin_hypha_ffi::{
    kirin_hypha_create, kirin_hypha_destroy, kirin_hypha_poll_result, kirin_hypha_poll_session,
    kirin_hypha_push_samples, kirin_hypha_set_signal_state, KirinMeasureResult, KirinSessionSummary,
};

#[test]
fn null_handle_calls_are_safe_noops() {
    // null handle に対する全関数呼び出しが UB を起こさず安全に戻ること。
    unsafe {
        kirin_hypha_set_signal_state(std::ptr::null_mut(), 1);
        kirin_hypha_push_samples(std::ptr::null_mut(), std::ptr::null(), 0, 2);

        let mut mr = std::mem::zeroed::<KirinMeasureResult>();
        assert!(
            !kirin_hypha_poll_result(std::ptr::null_mut(), &mut mr),
            "poll_result(null) must be false"
        );

        let mut ss = std::mem::zeroed::<KirinSessionSummary>();
        assert!(
            !kirin_hypha_poll_session(std::ptr::null_mut(), &mut ss),
            "poll_session(null) must be false"
        );

        kirin_hypha_destroy(std::ptr::null_mut()); // null destroy = no-op
    }
}

#[test]
fn edge_create_inputs_do_not_abort_process() {
    // 境界引数（sample_rate=0 / num_channels=0）。内部で panic すれば catch_unwind→null、
    // panic しなければ有効 handle。いずれにせよ呼び出しが戻り（= abort しない）、
    // 有効 handle なら正常に destroy できることを確認する。
    let h_edge = kirin_hypha_create(0, 0);
    if !h_edge.is_null() {
        unsafe { kirin_hypha_destroy(h_edge) };
    }
    // ここに到達した時点で「create が UB-abort しなかった」ことが示される。
}

#[test]
fn normal_lifecycle_intact_through_c_abi() {
    // 正常系（create→set_signal_state→push keepalive→poll→destroy）が catch_unwind 導入後も
    // 壊れていないこと。
    let h = kirin_hypha_create(48_000, 2);
    assert!(!h.is_null(), "normal create must return non-null");

    unsafe {
        kirin_hypha_set_signal_state(h, 1); // Active
        kirin_hypha_push_samples(h, std::ptr::null(), 0, 2); // 0-frame keepalive

        // 未計測でも poll_result は安全に戻る（true=default 値 / false=競合、どちらも UB なし）。
        let mut mr = std::mem::zeroed::<KirinMeasureResult>();
        let _ = kirin_hypha_poll_result(h, &mut mr);

        // poll_session は Phase 1 で常に false。
        let mut ss = std::mem::zeroed::<KirinSessionSummary>();
        assert!(!kirin_hypha_poll_session(h, &mut ss), "poll_session is false in Phase 1");

        kirin_hypha_destroy(h);
    }
}
