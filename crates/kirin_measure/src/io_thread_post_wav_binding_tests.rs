use super::wav_binding::{saved_pair_waits_for_audio, WavBindingObserver};
use crate::identity::License;
use crate::plugin_data::{compute_checksum, PluginDataFile, RecordQuality, Role, Status};
use crate::record::RecordStateMachine;
use crate::record_signal::{self, RecordSignal, SignalStatus};
use std::fs;
use std::path::{Path, PathBuf};

const PROJECT: &str = "project-a";
const SESSION: &str = "session-a";
const PRE: &str = "pre-a";
const POST: &str = "post-a";

fn member(role: Role) -> PluginDataFile {
    let mut data = PluginDataFile::new(
        "fixture".into(),
        PROJECT.into(),
        if role == Role::Pre { PRE } else { POST }.into(),
        role,
        None,
        48_000,
        (role == Role::Post).then(|| PRE.into()),
        (role == Role::Pre).then(|| POST.into()),
        None,
        None,
    );
    data.record_session_id = Some(SESSION.into());
    data.status = Status::Closed;
    data.commit_status = Some("pair_pending".into());
    data.record_quality = Some(RecordQuality {
        status: "complete".into(),
        complete: true,
        usable: true,
        expected_wav_ready: false,
        sample_count_ready: true,
        trace_slots_complete: true,
        expected_frame_count: 1,
        measured_frame_count: 1,
        missing_trace_slots: 0,
    });
    data
}

fn path(base: &Path, role: Role) -> PathBuf {
    let (id, name) = if role == Role::Pre {
        (PRE, "pre")
    } else {
        (POST, "post")
    };
    base.join(PROJECT)
        .join(id)
        .join(name)
        .join(".pair_pending")
        .join(format!("{SESSION}.json"))
}

fn write(base: &Path, data: &mut PluginDataFile) {
    data.checksum = compute_checksum(data).unwrap();
    let path = path(base, data.role);
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, serde_json::to_vec(data).unwrap()).unwrap();
}

#[test]
fn wav_binding_wait_requires_two_complete_exact_unbound_members() {
    let temp = tempfile::tempdir().unwrap();
    let base = temp.path();
    let (mut pre, mut post) = (member(Role::Pre), member(Role::Post));
    write(base, &mut pre);
    write(base, &mut post);
    assert!(saved_pair_waits_for_audio(base, PROJECT, POST, SESSION));
    post.record_quality.as_mut().unwrap().expected_wav_ready = true;
    write(base, &mut post);
    pre.record_quality.as_mut().unwrap().expected_wav_ready = true;
    write(base, &mut pre);
    assert!(!saved_pair_waits_for_audio(base, PROJECT, POST, SESSION));
    pre.record_quality.as_mut().unwrap().expected_wav_ready = false;
    pre.record_quality.as_mut().unwrap().complete = false;
    write(base, &mut pre);
    assert!(!saved_pair_waits_for_audio(base, PROJECT, POST, SESSION));
    pre.record_quality.as_mut().unwrap().complete = true;
    pre.paired_post_instance_id = Some("other".into());
    write(base, &mut pre);
    assert!(!saved_pair_waits_for_audio(base, PROJECT, POST, SESSION));
    pre.paired_post_instance_id = Some(POST.into());
    write(base, &mut pre);
    fs::write(path(base, Role::Pre), b"corrupt").unwrap();
    assert!(!saved_pair_waits_for_audio(base, PROJECT, POST, SESSION));
    assert!(!saved_pair_waits_for_audio(
        base,
        "../unsafe",
        POST,
        SESSION
    ));
}

#[test]
fn wav_binding_wait_tracks_the_closed_generation_and_ends_after_binding() {
    let temp = tempfile::tempdir().unwrap();
    let base = temp.path();
    let (mut pre, mut post) = (member(Role::Pre), member(Role::Post));
    write(base, &mut pre);
    write(base, &mut post);
    let state = RecordStateMachine::new();
    state
        .try_enter_record_started_at_clock_transaction(License::Os, 1, None, SESSION)
        .unwrap();
    let generation = state.generation();
    let mut observer = WavBindingObserver::default();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
    state.exit_record();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(true));
    let pre_bytes = fs::read(path(base, Role::Pre)).unwrap();
    let post_bytes = fs::read(path(base, Role::Post)).unwrap();
    for _ in 0..1000 {
        observer.poll(Some(base), PROJECT, POST, &state);
    }
    assert_eq!(state.try_record_wav_binding_pending(), Some(true));
    assert_eq!(fs::read(path(base, Role::Pre)).unwrap(), pre_bytes);
    assert_eq!(fs::read(path(base, Role::Post)).unwrap(), post_bytes);
    state.dismiss_record_display_on_watch_result();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(true));
    let manifest = base
        .join(PROJECT)
        .join("record_sessions")
        .join(format!("{SESSION}.json"));
    fs::create_dir_all(manifest.parent().unwrap()).unwrap();
    fs::write(&manifest, b"{}").unwrap();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
    fs::remove_file(manifest).unwrap();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(true));
    pre.record_quality.as_mut().unwrap().expected_wav_ready = true;
    post.record_quality.as_mut().unwrap().expected_wav_ready = true;
    write(base, &mut pre);
    write(base, &mut post);
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
    state
        .try_enter_record_started_at_clock_transaction(License::Os, 2, None, "session-b")
        .unwrap();
    state.set_record_wav_binding_pending(generation, true);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
    state.exit_record();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
}

#[test]
fn wav_binding_wait_restores_only_the_exact_released_pair() {
    let temp = tempfile::tempdir().unwrap();
    let base = temp.path();
    write(base, &mut member(Role::Pre));
    write(base, &mut member(Role::Post));
    let mut signal =
        RecordSignal::new_pending(POST.into(), PRE.into(), "fixture".into(), None, None);
    signal.session_id = SESSION.into();
    signal.status = SignalStatus::Released;
    record_signal::write_signal(base, PROJECT, POST, &signal).unwrap();
    let state = RecordStateMachine::new();
    let mut observer = WavBindingObserver::default();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(true));
    signal.target_pre_instance_id = "other".into();
    record_signal::write_signal(base, PROJECT, POST, &signal).unwrap();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
    signal.target_pre_instance_id = PRE.into();
    signal.status = SignalStatus::Acknowledged;
    record_signal::write_signal(base, PROJECT, POST, &signal).unwrap();
    observer.poll(Some(base), PROJECT, POST, &state);
    assert_eq!(state.try_record_wav_binding_pending(), Some(false));
}
