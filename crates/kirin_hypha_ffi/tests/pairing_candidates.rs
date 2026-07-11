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

fn wait_until_recording(engine: &KirinHyphaEngine, label: &str) {
    for _ in 0..30 {
        if engine.is_recording() {
            return;
        }
        engine.push_samples(&[], 2);
        sleep(Duration::from_millis(100));
    }
    panic!("engine did not enter Record after Keep/ACK barrier: {label}");
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

fn spawn_pre(instance_id: &str, project_uuid: &str, name: &str) -> KirinHyphaEngine {
    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity(
        instance_id.to_string(),
        project_uuid.to_string(),
        "daw-current".to_string(),
        name.to_string(),
    );
    pre.enable_pre_writes();
    pre.set_signal_state(0);
    pre
}

fn spawn_post(instance_id: &str, project_uuid: &str, name: &str) -> KirinHyphaEngine {
    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity(
        instance_id.to_string(),
        project_uuid.to_string(),
        "daw-current".to_string(),
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

fn read_pair_claims(post: &KirinHyphaEngine) -> Vec<(String, String)> {
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
            (iid, pair)
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
/// `Drum` / `Mix` are fixture labels only. The invariant is that an existing named POST claim must
/// not hide a second differently named PRE from the same C ABI candidate list.
#[test]
#[ignore = "slow: C ABI candidate enumeration with PRE/POST io threads (sets HOME/TMPDIR)"]
fn juce_candidate_abi_keeps_second_pre_visible_after_first_ready_post() {
    let (home, tmp) = isolate_env("drum_then_mix");

    let pre_drum = spawn_pre("iid-pre-drum", "puid-pre-drum", "Drum");
    let pre_mix = spawn_pre("iid-pre-mix", "puid-pre-mix", "Mix");
    let post_drum = spawn_post("iid-post-drum", "puid-post", "Drum");
    post_drum.set_pair_target("Drum".to_string());
    let post_mix = spawn_post("iid-post-mix", "puid-post", "");

    for _ in 0..12 {
        pre_drum.push_samples(&[], 2);
        pre_mix.push_samples(&[], 2);
        post_drum.push_samples(&[], 2);
        post_mix.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
    }

    let ready = unsafe {
        let handle = (&post_mix as *const KirinHyphaEngine).cast_mut();
        kirin_hypha_count_keep_ready(handle)
    };
    assert_eq!(ready, 1, "Drum POST should be the single ready POST");
    let claims = read_pair_claims(&post_mix);
    assert!(
        claims
            .iter()
            .any(|(iid, pair)| iid == "iid-post-drum" && pair == "Drum"),
        "POST pair claim list must expose the already ready Drum pair for JUCE menu status: {claims:?}"
    );

    let names = wait_for_candidate_names(&post_mix, &["Drum", "Mix"]);
    assert!(
        names.iter().any(|n| n == "Drum"),
        "Drum candidate missing from JUCE C ABI list: {names:?}"
    );
    assert!(
        names.iter().any(|n| n == "Mix"),
        "Mix candidate must remain visible after Drum is ready: {names:?}"
    );

    post_mix.set_pair_target("Mix".to_string());
    assert!(
        post_mix.keep(),
        "Mix should be keepable once selected from the same C ABI candidate list"
    );
    wait_until_recording(&post_mix, "pairing-candidates-mix");

    drop(post_mix);
    drop(post_drum);
    drop(pre_mix);
    drop(pre_drum);
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
