//! Prepare closed Record pairs without discarding their clock journal.
use super::*;

pub(super) fn prepare_late_expected_pair(
    left_data: &mut PluginDataFile,
    right_data: &mut PluginDataFile,
) -> bool {
    let (pre_data, post_data) = match (left_data.role, right_data.role) {
        (Role::Pre, Role::Post) => (left_data, right_data),
        (Role::Post, Role::Pre) => (right_data, left_data),
        _ => return false,
    };
    synchronize_pair_annotations(pre_data, post_data);
    let content_alignment = crate::trace_alignment::canonical_wav_alignment(pre_data, post_data);
    let timing_reasons = pair_publish_failure_reasons(pre_data, post_data);
    let alignment_exact = content_alignment.is_some();
    pre_data.trace_content_alignment = alignment_exact.then(|| {
        content_alignment
            .clone()
            .expect("exact timing has alignment")
    });
    post_data.trace_content_alignment =
        alignment_exact.then(|| content_alignment.expect("exact timing has alignment"));
    pre_data.trace_pair_offset_samples = None;
    post_data.trace_pair_offset_samples = None;
    pre_data.trace_pair_alignment_score = None;
    post_data.trace_pair_alignment_score = None;
    pre_data.trace_context_frames.clear();
    post_data.trace_context_frames.clear();
    pre_data.trace_context_slot_positions.clear();
    post_data.trace_context_slot_positions.clear();
    pre_data.trace_pair_wav_start_basis = None;
    post_data.trace_pair_wav_start_basis = None;
    pre_data.trace_context_psb_snapshots.clear();
    post_data.trace_context_psb_snapshots.clear();
    let publication_reasons = pair_visibility_failure_reasons(pre_data, post_data);
    if !publication_reasons.is_empty() {
        add_integrity_reasons(pre_data, &publication_reasons);
        add_integrity_reasons(post_data, &publication_reasons);
        log::warn!(
            "[RecordPair] late publish rejected for session {:?}: {}",
            pre_data.record_session_id,
            publication_reasons.join(",")
        );
        return false;
    }
    if !timing_reasons.is_empty() {
        add_integrity_reasons(pre_data, &timing_reasons);
        add_integrity_reasons(post_data, &timing_reasons);
    }
    // Keep the measured journal, just as normal writer-close publication does. Canonical WAV
    // placement does not erase the facts used to qualify that placement or a later recovery.
    pre_data.commit_status = Some("committed".to_string());
    post_data.commit_status = Some("committed".to_string());
    refresh_record_quality(pre_data);
    refresh_record_quality(post_data);
    true
}
