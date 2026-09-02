use super::*;
use std::sync::atomic::AtomicU64;

fn isolated_dir(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_preset_poll_test_{pid}_{n}_{tag}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn empty_dir_has_no_current_pointer() {
    let dir = isolated_dir("empty");
    assert!(!current_preset_exists(&dir));
}

#[test]
fn missing_dir_has_no_current_pointer() {
    let dir = isolated_dir("missing");
    let child = dir.join("no_such");
    assert!(!current_preset_exists(&child));
}

#[test]
fn current_pointer_is_available() {
    let dir = isolated_dir("one");
    fs::write(dir.join("current.json"), b"x").unwrap();
    assert!(current_preset_exists(&dir));
}

#[test]
fn history_tmp_and_non_json_never_make_preset_available() {
    let dir = isolated_dir("ignore");
    fs::write(dir.join("history.json"), b"x").unwrap();
    fs::write(dir.join("notes.txt"), b"x").unwrap();
    fs::write(dir.join("current.json.tmp"), b"x").unwrap();
    assert!(!current_preset_exists(&dir));
}

#[test]
fn current_pointer_must_be_a_file() {
    let dir = isolated_dir("current_dir");
    fs::create_dir_all(dir.join("current.json")).unwrap();
    assert!(!current_preset_exists(&dir));
}
