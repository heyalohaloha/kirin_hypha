use super::*;
use std::sync::atomic::AtomicU64;

fn isolated_project_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir()
        .join(format!("kirin_compute_delta_test_{pid}_{n}"))
        .join("ph");
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_pre(project_dir: &Path, instance_id: &str, t: &str, lufs: f64) {
    let dir = project_dir.join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"{t}","lufs_m":{lufs},"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

fn write_pre_with_short_term(
    project_dir: &Path,
    instance_id: &str,
    t: &str,
    lufs_m: f64,
    lufs_s: f64,
) {
    let dir = project_dir.join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"{t}","lufs_m":{lufs_m},"lufs_s":{lufs_s},"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

fn write_pre_named(project_dir: &Path, instance_id: &str, name: &str, t: &str, lufs: f64) {
    let dir = project_dir.join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","name":"{name}","signal_state":"active","t":"{t}","lufs_m":{lufs},"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

#[test]
fn no_pre_dir_returns_no_pre_mode() {
    let pd = isolated_project_dir();
    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        None,
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::NoPre);
}

#[test]
fn short_term_delta_is_additive_across_old_and_new_pre_json() {
    let pd = isolated_project_dir();
    let now = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string();
    let post = MeasureResult {
        lufs_m: Some(-10.0),
        lufs_s: Some(-11.0),
        ..MeasureResult::default()
    };

    write_pre(&pd, "pre-old", &now, -14.0);
    let old = compute_delta_with_state(&pd, &post, None).unwrap().0;
    assert_eq!(old.mode, DeltaMode::Active);
    assert_eq!(old.lufs, Some(4.0));
    assert_eq!(
        old.lufs_s, None,
        "a legacy PRE without lufs_s must keep existing deltas and expose ΔS as unavailable"
    );

    fs::remove_dir_all(pd.join("pre-old")).unwrap();
    write_pre_with_short_term(&pd, "pre-new", &now, -14.0, -15.0);
    let new = compute_delta_with_state(&pd, &post, None).unwrap().0;
    assert_eq!(new.mode, DeltaMode::Active);
    assert_eq!(new.lufs, Some(4.0));
    assert_eq!(new.lufs_s, Some(4.0));
}

#[test]
fn single_instance_pass_through_when_no_pair() {
    let pd = isolated_project_dir();
    let now = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string();
    write_pre(&pd, "iid-A", &now, -14.0);

    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        None,
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::Active);
    assert!(result.0.lufs.is_some());
}

#[test]
fn record_signal_subdir_is_skipped() {
    let pd = isolated_project_dir();
    let signal_dir = pd.join(SIGNALS_SUBDIR);
    fs::create_dir_all(&signal_dir).unwrap();
    fs::write(signal_dir.join("post-1.json"), b"{}").unwrap();
    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        None,
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::NoPre);
}

#[test]
fn pair_filter_skips_non_matching_name() {
    let pd = isolated_project_dir();
    let now = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string();
    write_pre_named(&pd, "iid-A", "snare", &now, -14.0);
    write_pre_named(&pd, "iid-B", "kick", &now, -15.0);

    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        Some("snare"),
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::Active);
    let delta_lufs = result.0.lufs.expect("delta.lufs should be Some");
    assert!(
        (delta_lufs - 4.0).abs() < 0.01,
        "expected Δ from iid-A (snare) ~+4.0, got {delta_lufs}"
    );
}

#[test]
fn pair_filter_picks_max_t_within_pair() {
    let pd = isolated_project_dir();
    let base = chrono::Utc::now();
    let fmt_t = |secs_ago: i64| {
        (base - chrono::Duration::seconds(secs_ago))
            .format("%Y-%m-%dT%H:%M:%S%.3fZ")
            .to_string()
    };
    write_pre_named(&pd, "iid-snare-old", "snare", &fmt_t(2), -14.0);
    write_pre_named(&pd, "iid-snare-new", "snare", &fmt_t(1), -16.0);
    write_pre_named(&pd, "iid-kick", "kick", &fmt_t(0), -12.0);

    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        Some("snare"),
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::Active);
    let delta_lufs = result.0.lufs.expect("delta.lufs should be Some");
    assert!(
        (delta_lufs - 6.0).abs() < 0.01,
        "expected Δ from snare-new ~+6.0, got {delta_lufs}"
    );
}

#[test]
fn pair_filter_zero_match_falls_to_no_pre() {
    let pd = isolated_project_dir();
    let now = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string();
    write_pre_named(&pd, "iid-A", "snare", &now, -14.0);
    write_pre_named(&pd, "iid-B", "kick", &now, -15.0);

    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        Some("vocal"),
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::NoPre);
    assert!(result.0.lufs.is_none());
}

#[test]
fn no_pair_with_multiple_instances_falls_to_no_pre() {
    let pd = isolated_project_dir();
    let now = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%S%.3fZ")
        .to_string();
    write_pre(&pd, "iid-A", &now, -14.0);
    write_pre(&pd, "iid-B", &now, -15.0);

    let result = compute_delta_with_state(
        &pd,
        &MeasureResult {
            lufs_m: Some(-10.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
            ..Default::default()
        },
        None,
    )
    .unwrap();
    assert_eq!(result.0.mode, DeltaMode::NoPre);
    assert!(result.0.lufs.is_none());
}
