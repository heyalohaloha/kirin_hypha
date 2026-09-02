use super::{stable_record_generation, PostObservation};
use crate::delta::DeltaResult;
use crate::pairing_scope::{LatchedPre, LatchedPreReadiness};
use crate::storage::PlatformPaths;
use crate::{PairOwnershipLease, RecordStateMachine, SignalState};
use std::fs;
use std::sync::atomic::{AtomicBool, AtomicU8};
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, Instant};

#[test]
fn record_display_requires_one_unchanged_record_generation() {
    assert_eq!(stable_record_generation(7, true, 7), Some(7));
    assert_eq!(stable_record_generation(7, true, 8), None);
    assert_eq!(stable_record_generation(7, false, 7), None);
}

#[test]
fn confirmed_self_check_release_is_published_in_the_same_observation_tick() {
    let suffix = uuid::Uuid::new_v4();
    let project = format!("observation-project-{suffix}");
    let post = format!("observation-post-{suffix}");
    let foreign_post = format!("observation-foreign-{suffix}");
    let pre = format!("observation-pre-{suffix}");
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let project_dir = kirin_root.join(&project);
    let _ = fs::remove_dir_all(&project_dir);

    let foreign = PairOwnershipLease::new();
    let foreign_dir = project_dir.join(&foreign_post);
    fs::create_dir_all(&foreign_dir).unwrap();
    assert_eq!(
        foreign
            .commit_claimed_binding_if(
                &kirin_root,
                Some(&foreign_dir),
                Some(&pre),
                &project,
                &foreign_post,
                2.0,
                || true,
                || Some(()),
            )
            .unwrap(),
        Some(())
    );

    let instance_id = Arc::new(RwLock::new(post.clone()));
    let project_hash = Arc::new(RwLock::new(project.clone()));
    let daw_session_id = Arc::new(RwLock::new(format!("observation-daw-{suffix}")));
    let record_sm = Arc::new(RecordStateMachine::new());
    let post_result = Arc::new(Mutex::new(crate::MeasureResult::default()));
    let delta_result = Arc::new(Mutex::new(DeltaResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Inactive as u8));
    let is_playing = Arc::new(AtomicBool::new(false));
    let paired_pre_target = Arc::new(Mutex::new(Some(pre.clone())));
    let pair_pre_name = Arc::new(RwLock::new("PRE-A".to_string()));
    let pair_claimed_at = Arc::new(RwLock::new(1.0));
    let pair_release_notice = Arc::new(RwLock::new(None));
    let latched_pre = Arc::new(Mutex::new(Some(LatchedPre {
        name: "PRE-A".to_string(),
        instance_id: pre,
        project_dir: project_dir.clone(),
        pre_json: project_dir.join("unused-pre.json"),
        daw_session_id: Some(daw_session_id.read().unwrap().clone()),
        host_process_id: Some(crate::current_host_process_id()),
        readiness: LatchedPreReadiness::Confirmed,
    })));
    let release_pair_name = Arc::clone(&pair_pre_name);
    let release_latch = Arc::clone(&latched_pre);
    let release_target = Arc::clone(&paired_pre_target);
    let release = Arc::new(move |expected_name: &str, expected_generation: u64| {
        if expected_name != "PRE-A" || expected_generation != 7 {
            return false;
        }
        *release_pair_name.write().unwrap() = String::new();
        *release_latch.lock().unwrap() = None;
        *release_target.lock().unwrap() = None;
        true
    });
    let start = Instant::now();
    let mut observation = PostObservation::new(
        instance_id,
        project_hash,
        daw_session_id,
        record_sm,
        post_result,
        delta_result,
        signal_state,
        is_playing,
        paired_pre_target,
        Arc::clone(&pair_pre_name),
        Arc::new(|| 7),
        release,
        Arc::clone(&pair_claimed_at),
        Arc::clone(&pair_release_notice),
        Arc::new(PairOwnershipLease::new()),
        Arc::clone(&latched_pre),
        None,
        None,
        start,
    );

    assert_eq!(observation.service_at(start).pair_pre_name, "PRE-A");
    assert_eq!(
        observation
            .service_at(start + Duration::from_secs(1))
            .pair_pre_name,
        "PRE-A"
    );
    let released = observation.service_at(start + Duration::from_secs(2));
    assert!(released.pair_pre_name.is_empty());
    assert!(pair_pre_name.read().unwrap().is_empty());
    assert!(latched_pre.lock().unwrap().is_none());
    assert_eq!(*pair_claimed_at.read().unwrap(), 0.0);
    assert_eq!(
        pair_release_notice.read().unwrap().as_deref(),
        Some("PRE already in use")
    );

    let post_json: serde_json::Value =
        serde_json::from_slice(&fs::read(project_dir.join(&post).join("post.json")).unwrap())
            .unwrap();
    assert_eq!(post_json["pair_pre_name"], "");
    assert_eq!(post_json["paired_pre_instance_id"], "");
    assert_eq!(post_json["pair_claimed_at"], 0.0);

    drop(observation);
    drop(foreign);
    fs::remove_dir_all(project_dir).unwrap();
}
