//! Shell-facing pairing candidate scenarios.
//!
//! These tests exercise the C ABI that the JUCE shell calls for the POST dropdown.

use std::ffi::CStr;
use std::path::Path;
use std::thread::sleep;
use std::time::Duration;

use kirin_hypha_ffi::{
    kirin_hypha_count_keep_ready, kirin_hypha_enumerate_post_pair_claims,
    kirin_hypha_enumerate_pre_candidates, KirinHyphaEngine, KirinPostPairClaim, KirinPreCandidate,
};

const SR: u32 = 48_000;

fn wait_until_stopped(engine: &KirinHyphaEngine, label: &str) {
    for _ in 0..30 {
        if !engine.is_recording() {
            return;
        }
        engine.push_samples(&[], 2);
        sleep(Duration::from_millis(100));
    }
    panic!("engine did not leave Record after All Stop: {label}");
}

fn isolate_env(label: &str) -> (std::path::PathBuf, std::path::PathBuf) {
    let root = std::env::temp_dir().join(format!(
        "kirin_pairing_candidates_{label}_pid{}",
        std::process::id()
    ));
    let home = root.join("home");
    let tmp = root.join("tmp");
    let _ = std::fs::remove_dir_all(&root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    kirin_hypha_ffi::__reset_shared_ids_for_tests();

    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"pairing-candidate-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-26T00:00:00Z","last_verified_at":"2026-06-26T00:00:00Z"}"#,
    )
    .unwrap();

    (home, tmp)
}

fn spawn_pre(
    instance_id: &str,
    project_uuid: &str,
    daw_session_id: &str,
    name: &str,
) -> KirinHyphaEngine {
    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity(
        instance_id.to_string(),
        project_uuid.to_string(),
        daw_session_id.to_string(),
        name.to_string(),
    );
    pre.enable_pre_writes();
    pre.set_signal_state(0);
    pre
}

fn spawn_post(
    instance_id: &str,
    project_uuid: &str,
    daw_session_id: &str,
    name: &str,
) -> KirinHyphaEngine {
    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity(
        instance_id.to_string(),
        project_uuid.to_string(),
        daw_session_id.to_string(),
        name.to_string(),
    );
    post.enable_post_writes();
    post.set_signal_state(0);
    post
}

fn read_candidate_names(post: &KirinHyphaEngine) -> Vec<String> {
    let mut buf: [KirinPreCandidate; 8] = std::array::from_fn(|_| unsafe { std::mem::zeroed() });
    let handle = (post as *const KirinHyphaEngine).cast_mut();
    let n = unsafe { kirin_hypha_enumerate_pre_candidates(handle, buf.as_mut_ptr(), buf.len()) };
    buf.iter()
        .take(n)
        .filter(|c| c.has_name != 0)
        .map(|c| unsafe { CStr::from_ptr(c.name.as_ptr()) })
        .map(|s| s.to_string_lossy().to_string())
        .collect()
}

fn read_pair_claims(post: &KirinHyphaEngine) -> Vec<(String, String, String)> {
    let mut buf: [KirinPostPairClaim; 8] = std::array::from_fn(|_| unsafe { std::mem::zeroed() });
    let handle = (post as *const KirinHyphaEngine).cast_mut();
    let n = unsafe { kirin_hypha_enumerate_post_pair_claims(handle, buf.as_mut_ptr(), buf.len()) };
    buf.iter()
        .take(n)
        .filter(|c| c.has_pair_pre_name != 0)
        .map(|c| {
            let iid = unsafe { CStr::from_ptr(c.instance_id.as_ptr()) }
                .to_string_lossy()
                .to_string();
            let pair = unsafe { CStr::from_ptr(c.pair_pre_name.as_ptr()) }
                .to_string_lossy()
                .to_string();
            let exact = unsafe { CStr::from_ptr(c.paired_pre_instance_id.as_ptr()) }
                .to_string_lossy()
                .to_string();
            (iid, pair, exact)
        })
        .collect()
}

fn wait_for_candidate_names(post: &KirinHyphaEngine, expected: &[&str]) -> Vec<String> {
    for _ in 0..60 {
        let names = read_candidate_names(post);
        if expected.iter().all(|name| names.iter().any(|n| n == name)) {
            return names;
        }
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
    }
    read_candidate_names(post)
}

fn read_watch_field(
    tmp: &Path,
    project_uuid: &str,
    instance_id: &str,
    file_name: &str,
    field: &str,
) -> Option<String> {
    let path = tmp
        .join("kirin")
        .join(project_uuid)
        .join(instance_id)
        .join(file_name);
    let text = std::fs::read_to_string(path).ok()?;
    let json: serde_json::Value = serde_json::from_str(&text).ok()?;
    json.get(field)?.as_str().map(ToOwned::to_owned)
}

fn wait_for_watch_field(
    engine: &KirinHyphaEngine,
    tmp: &Path,
    project_uuid: &str,
    instance_id: &str,
    file_name: &str,
    field: &str,
    expected: &str,
) -> String {
    let mut last = None;
    for _ in 0..80 {
        if let Some(value) = read_watch_field(tmp, project_uuid, instance_id, file_name, field) {
            if value == expected {
                return value;
            }
            last = Some(value);
        }
        engine.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
    }
    panic!("{file_name}.{field} did not become {expected:?}; last={last:?}");
}

/// AU/JUCE POST dropdown parity.
///
/// `2Mix` / `Drum` / `Music` are fixture labels only. AU and VST3 may publish different project and
/// DAW IDs inside one host process. The shell contract still requires all three exact claims to be
/// visible, counted as ready, reached by one All Keep action, and stopped by one All Stop action.
#[test]
#[ignore = "slow: C ABI candidate enumeration with PRE/POST io threads (sets HOME/TMPDIR)"]
fn juce_candidate_abi_bridges_split_shell_claims_and_all_keep() {
    let (home, tmp) = isolate_env("split_shell_all_keep");

    let pre_2mix = spawn_pre("iid-pre-2mix", "puid-pre-au", "daw-pre-au", "2Mix");
    let pre_drum = spawn_pre("iid-pre-drum", "puid-pre-vst", "daw-pre-vst", "Drum");
    let pre_music = spawn_pre("iid-pre-music", "puid-pre-vst", "daw-pre-vst", "Music");
    let post_2mix = spawn_post("iid-post-2mix", "puid-post-au", "daw-post-au", "");
    let post_drum = spawn_post("iid-post-drum", "puid-post-vst", "daw-post-vst", "");
    let post_music = spawn_post("iid-post-music", "puid-post-vst", "daw-post-vst", "");

    for _ in 0..12 {
        pre_2mix.push_samples(&[], 2);
        pre_drum.push_samples(&[], 2);
        pre_music.push_samples(&[], 2);
        post_2mix.push_samples(&[], 2);
        post_drum.push_samples(&[], 2);
        post_music.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
    }

    let names = wait_for_candidate_names(&post_2mix, &["2Mix", "Drum", "Music"]);
    assert_eq!(names.len(), 3, "split-shell PRE choices: {names:?}");
    assert!(post_2mix.set_pair_candidate("iid-pre-2mix"));
    assert!(post_drum.set_pair_candidate("iid-pre-drum"));
    assert!(post_music.set_pair_candidate("iid-pre-music"));

    wait_for_watch_field(
        &post_2mix,
        &tmp,
        "puid-post-au",
        "iid-post-2mix",
        "post.json",
        "paired_pre_instance_id",
        "iid-pre-2mix",
    );
    wait_for_watch_field(
        &post_drum,
        &tmp,
        "puid-post-vst",
        "iid-post-drum",
        "post.json",
        "paired_pre_instance_id",
        "iid-pre-drum",
    );
    wait_for_watch_field(
        &post_music,
        &tmp,
        "puid-post-vst",
        "iid-post-music",
        "post.json",
        "paired_pre_instance_id",
        "iid-pre-music",
    );

    let ready = unsafe {
        let handle = (&post_2mix as *const KirinHyphaEngine).cast_mut();
        kirin_hypha_count_keep_ready(handle)
    };
    assert_eq!(ready, 3, "All Keep must count all split-shell POSTs");
    let claims = read_pair_claims(&post_2mix);
    assert_eq!(
        claims,
        vec![
            (
                "iid-post-2mix".to_string(),
                "2Mix".to_string(),
                "iid-pre-2mix".to_string(),
            ),
            (
                "iid-post-drum".to_string(),
                "Drum".to_string(),
                "iid-pre-drum".to_string(),
            ),
            (
                "iid-post-music".to_string(),
                "Music".to_string(),
                "iid-pre-music".to_string(),
            ),
        ],
        "pair ownership must cross AU/VST shelves: {claims:?}"
    );

    assert!(
        post_2mix.keep_all(),
        "All Keep may return success only after every exact producer is armed"
    );
    assert!(
        post_2mix.is_recording() && post_drum.is_recording() && post_music.is_recording(),
        "successful All Keep is the bounce-safe barrier; callers must not need a later wait"
    );

    post_drum.set_signal_state(2); // C ABI 2=Bypassed; owners remain part of All Stop
    sleep(Duration::from_millis(150));
    post_2mix.stop_all();
    wait_until_stopped(&post_2mix, "split-shell-2mix");
    wait_until_stopped(&post_drum, "split-shell-drum-bypassed");
    wait_until_stopped(&post_music, "split-shell-music");

    drop(post_music);
    drop(post_drum);
    drop(post_2mix);
    drop(pre_music);
    drop(pre_drum);
    drop(pre_2mix);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
    let _ = std::fs::remove_dir_all(tmp.parent().unwrap_or(&tmp));
}

/// AU/JUCE state chunk ordering parity.
///
/// `Mix` is a fixture label only. The invariant is that a pair target restored before
/// `enable_post_writes` survives the enable boundary and appears in the POST watch JSON.
#[test]
#[ignore = "slow: C ABI restore order with POST io thread (sets HOME/TMPDIR)"]
fn restored_pair_target_before_enable_is_written_to_post_watch_json() {
    let (home, tmp) = isolate_env("restore_pair_before_enable");
    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity(
        "post-restore-a".to_string(),
        "proj-restore-a".to_string(),
        "daw-post-a".to_string(),
        "IdentitySeed".to_string(),
    );
    post.set_pair_target("Mix".to_string());
    post.enable_post_writes();
    post.set_signal_state(0);

    let pair = wait_for_watch_field(
        &post,
        &tmp,
        "proj-restore-a",
        "post-restore-a",
        "post.json",
        "pair_pre_name",
        "Mix",
    );
    assert_eq!(pair, "Mix");

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
}

/// AU/JUCE state chunk ordering parity.
///
/// `Mix` is a fixture label only. The invariant is that a pair target restored after
/// `enable_post_writes` is pushed live into the already-running POST io thread.
#[test]
#[ignore = "slow: C ABI live restore with POST io thread (sets HOME/TMPDIR)"]
fn restored_pair_target_after_enable_updates_post_watch_json() {
    let (home, tmp) = isolate_env("restore_pair_after_enable");
    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity(
        "post-restore-b".to_string(),
        "proj-restore-b".to_string(),
        "daw-post-b".to_string(),
        "IdentitySeed".to_string(),
    );
    post.enable_post_writes();
    post.set_signal_state(0);
    post.set_pair_target("Mix".to_string());

    let pair = wait_for_watch_field(
        &post,
        &tmp,
        "proj-restore-b",
        "post-restore-b",
        "post.json",
        "pair_pre_name",
        "Mix",
    );
    assert_eq!(pair, "Mix");

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
}

/// AU/JUCE state chunk ordering parity.
///
/// `PRE A` is a fixture label only. The invariant is that a PRE name restored before
/// `enable_pre_writes` seeds the PRE watch JSON.
#[test]
#[ignore = "slow: C ABI restore order with PRE io thread (sets HOME/TMPDIR)"]
fn restored_pre_name_before_enable_is_written_to_pre_watch_json() {
    let (home, tmp) = isolate_env("restore_pre_before_enable");
    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity(
        "pre-restore-a".to_string(),
        "proj-pre-a".to_string(),
        "daw-pre-a".to_string(),
        "PRE A".to_string(),
    );
    pre.enable_pre_writes();
    pre.set_signal_state(0);

    let name = wait_for_watch_field(
        &pre,
        &tmp,
        "proj-pre-a",
        "pre-restore-a",
        "pre.json",
        "name",
        "PRE A",
    );
    assert_eq!(name, "PRE A");

    drop(pre);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
}

/// AU/JUCE state chunk ordering parity.
///
/// `PRE B` is a fixture label only. The invariant is that a PRE name restored after
/// `enable_pre_writes` is pushed live into the already-running PRE io thread.
#[test]
#[ignore = "slow: C ABI live restore with PRE io thread (sets HOME/TMPDIR)"]
fn restored_pre_name_after_enable_updates_pre_watch_json() {
    let (home, tmp) = isolate_env("restore_pre_after_enable");
    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity(
        "pre-restore-b".to_string(),
        "proj-pre-b".to_string(),
        "daw-pre-b".to_string(),
        "Initial".to_string(),
    );
    pre.enable_pre_writes();
    pre.set_signal_state(0);
    pre.set_pre_name("PRE B".to_string());

    let name = wait_for_watch_field(
        &pre,
        &tmp,
        "proj-pre-b",
        "pre-restore-b",
        "pre.json",
        "name",
        "PRE B",
    );
    assert_eq!(name, "PRE B");

    drop(pre);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
}
