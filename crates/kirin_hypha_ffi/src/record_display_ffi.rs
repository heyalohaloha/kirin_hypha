//! Keep/Record display snapshot to its C ABI form (UI thread only).
use super::*;

pub(crate) fn to_c_record_display(
    snapshot: RecordDisplaySnapshot,
    current_pair_pre_instance_id: Option<&str>,
) -> KirinRecordDisplay {
    let has_measure = snapshot.measure.is_some() as u8;
    let has_session = snapshot.summary.is_some() as u8;
    let has_delta = snapshot.delta.is_some() as u8;
    let pair_matches_current = matches!(
        (
            snapshot.pair_pre_instance_id.as_deref(),
            current_pair_pre_instance_id
        ),
        (Some(recorded), Some(current)) if recorded == current
    ) as u8;
    KirinRecordDisplay {
        phase: record_display_phase_to_abi(snapshot.status),
        has_measure,
        has_session,
        has_delta,
        generation: snapshot.generation,
        measure: snapshot
            .measure
            .as_ref()
            .map_or_else(|| to_c_result(&MeasureResult::default()), to_c_result),
        session: snapshot
            .summary
            .as_ref()
            .map_or_else(|| to_c_session(&SessionSummary::default()), to_c_session),
        delta: snapshot
            .delta
            .as_ref()
            .map_or_else(|| to_c_delta(&DeltaResult::default()), to_c_delta),
        pair_matches_current,
    }
}

/// Read the last closed Record's audio-binding notice without waiting for the IO worker.
/// -1 means the snapshot is temporarily unavailable, 0 means no notice, 1 means waiting.
///
/// # Safety
/// `handle` must be null or a live engine pointer.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_record_wav_binding_status(
    handle: *const KirinHyphaEngine,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return -1;
        }
        unsafe { (*handle).record_sm.try_record_wav_binding_pending() }.map_or(-1, i32::from)
    }))
    .unwrap_or(-1)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wav_binding_status_is_scoped_to_the_closed_generation() {
        assert_eq!(
            unsafe { kirin_hypha_record_wav_binding_status(std::ptr::null()) },
            -1
        );
        let engine = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
        assert_eq!(unsafe { kirin_hypha_record_wav_binding_status(&engine) }, 0);
        engine.record_sm.set_record_wav_binding_pending(0, true);
        assert_eq!(unsafe { kirin_hypha_record_wav_binding_status(&engine) }, 1);
        engine
            .record_sm
            .try_enter_record_started_at_clock_transaction(License::Os, 1, None, "session-a")
            .unwrap();
        let generation = engine.record_sm.generation();
        engine.record_sm.exit_record();
        engine
            .record_sm
            .set_record_wav_binding_pending(generation, true);
        assert_eq!(unsafe { kirin_hypha_record_wav_binding_status(&engine) }, 1);
        engine
            .record_sm
            .try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-b")
            .unwrap();
        assert_eq!(unsafe { kirin_hypha_record_wav_binding_status(&engine) }, 0);
    }
}
