//! Control-plane Stop resolution.
use super::*;

/// Stop resolution shared by the direct control and All Stop receiver.
///
/// Record lifecycle, reservation ownership and pair selection are deliberately separate. Stop
/// closes the current session and releases its reservation, but the exact PRE selection remains
/// available to the stopped-session Drop reconciler. Only an explicit selector transition/unpair
/// clears `paired_pre_target` through `PairBinding`.
pub(super) fn resolve_and_exit_stop(
    record_sm: &RecordStateMachine,
    record_ingress: &RecordIngress,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
    release_reason: Option<ReleaseReason>,
) {
    // Capture the selected PRE without consuming it. The reservation is Record-scoped, while this
    // identity is also the deterministic address for a WAV dropped after Stop.
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    // 2026-07-10 構造修正（ACK re-entry race）: shared signal を Released にしてから
    // record_sm.exit_record() する。逆順だと、record_sm が既に Watch なのに on-disk signal は
    // まだ Acknowledged のままという間隙が生まれ、その間に ACK poller（io_thread_pre.rs /
    // io_thread_post.rs）が stale な Acknowledged を読んで同じ session_id へ再入場して
    // しまう。record_sm 側の `closed_session_id` ガード（record.rs）が構造的な本丸だが、
    // この reorder はその隙間自体を縮める defense-in-depth。exit_record は
    // StoragePaths 解決の成否・早期 return 経路に関わらず必ず1回だけ呼ぶ（元の
    // 無条件呼び出しと同じ保証）。
    if project_hash.is_empty() || post_iid.is_empty() {
        kirin_measure::cleanup::exit_record_releasing_unattached_lane(record_sm, record_ingress);
        return;
    }
    if let Ok(p) = StoragePaths::default_platform() {
        let base = p.plugin_data_dir();
        if let Some(pre) = released_pre.as_deref() {
            reservation::release_pairing(&base, project_hash, pre, post_iid);
        }
        let owned_session = record_sm
            .record_session_id()
            .or_else(|| record_sm.last_closed_session_id());
        if let Some(current) = read_signal(&base, project_hash, post_iid) {
            // Explicit user/broadcast Stop owns the currently addressed member, including the
            // Pending-before-ACK interval. Teardown without a Stop reason owns only the exact
            // session this engine entered or closed; a recreated engine may already have replaced
            // the canonical path with its next Keep. An ownership mismatch must be a no-op, never
            // an unconditional fallback release of the newer incarnation.
            let _ = match release_reason {
                Some(reason) => mark_released_with_reason_if_current(
                    &base,
                    project_hash,
                    post_iid,
                    &current,
                    reason,
                ),
                None if owned_session.as_deref() == Some(current.session_id.as_str()) => {
                    mark_released_if_current(&base, project_hash, post_iid, &current)
                }
                None => Ok(false),
            };
        }
    }
    kirin_measure::cleanup::exit_record_releasing_unattached_lane(record_sm, record_ingress);
}

#[cfg(test)]
#[path = "record_stop_ffi_tests.rs"]
mod tests;
