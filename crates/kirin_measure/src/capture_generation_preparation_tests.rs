use super::*;
use crate::capture_generation::CaptureGeneration;
use crate::plugin_data::Role;

/// The PRE has attached the second Keep's lane, but each finite Watch chunk takes two seconds.
/// Inject scheduling delay through the barrier's clock; no real worker or sleep is needed.
#[test]
fn a_second_keep_arms_while_its_watch_tail_keeps_advancing() {
    assert_eq!(
        slow_watch_preparation(false),
        (true, Duration::from_secs(13))
    );
}

#[test]
fn repeated_preparation_heartbeats_do_not_hide_a_watch_stall() {
    assert_eq!(
        slow_watch_preparation(true),
        (false, Duration::from_secs(5))
    );
}

fn slow_watch_preparation(stall: bool) -> (bool, Duration) {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let member = &generation.members[0];
    crate::record_entry_lock::claim_record_entry(
        temp.path(),
        "project",
        &member.record_session_id,
        Role::Pre,
        "pre",
    )
    .unwrap();
    let path = temp
        .path()
        .join("project/record_entry_lock")
        .join(&member.record_session_id)
        .join("pre__pre.json");
    let original: serde_json::Value =
        serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let mut writers = Vec::new();
    let ready = wait_for_producers(
        TIMEOUT,
        || {
            let seconds = elapsed.get().as_secs();
            let mut claim = original.clone();
            claim["measure_preparation"] = serde_json::json!({
                "capture_generation_id": generation.capture_generation_id,
                "generation_started_at_ms": generation.started_at_ms,
                "record_generation": 2,
                "samples": (seconds / 2).min(if stall { 1 } else { 6 }) * 9_600,
            });
            crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&claim).unwrap())
                .unwrap();
            if !stall && seconds >= 13 && writers.is_empty() {
                for (role, iid) in [(Role::Pre, "pre"), (Role::Post, "post")] {
                    let mut claim = crate::record_writer_claim::claim_writer_for_generation(
                        temp.path(),
                        "project",
                        &member.record_session_id,
                        role,
                        iid,
                        &generation.capture_generation_id,
                        generation.started_at_ms,
                    )
                    .unwrap();
                    claim.mark_ready().unwrap();
                    writers.push(claim);
                }
            }
            crate::capture_generation_tx::ready::producer_readiness_observation(
                temp.path(),
                &generation,
            )
        },
        2,
        || origin + elapsed.get(),
        || elapsed.set(elapsed.get() + Duration::from_secs(1)),
    );
    (ready, elapsed.get())
}
