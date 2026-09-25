use super::*;

fn features() -> AttackPerceptualFeatures {
    AttackPerceptualFeatures {
        sample_rate: 48_000,
        channels: 2,
        bin_frames: 48,
        window_start_sample: 4_800,
        attack_rms_dbfs: -18.0,
        sample_peak_dbfs: -6.0,
        crest_db: 12.0,
        body_end_sample: 4_800 + 130 * 48,
        body_rms_dbfs: Some(-24.0),
        transient_db: Some(6.0),
        sharpness_acum: Some(2.5),
    }
}

#[test]
fn bins_are_whole_sample_milliseconds_for_every_supported_rate() {
    assert_eq!(attack_bin_frames(44_100), 44);
    assert_eq!(attack_bin_frames(48_000), 48);
    assert_eq!(attack_bin_frames(96_000), 96);
    assert_eq!(attack_bin_frames(192_000), 192);
    assert_eq!(attack_bin_frames(1_000), 1);
}

#[test]
fn windows_start_at_the_bin_that_contains_the_onset() {
    assert!(features().has_valid_layout());
    assert!(features().starts_at(4_800));
    assert!(features().starts_at(4_847));
    assert!(!features().starts_at(4_848));
    assert!(!features().starts_at(4_799));
    let mut off_grid = features();
    off_grid.window_start_sample += 1;
    assert!(!off_grid.has_valid_layout());
    let mut wrong_bin = features();
    wrong_bin.bin_frames = 44;
    assert!(!wrong_bin.has_valid_layout());
}

#[test]
fn the_body_follows_the_head_and_carries_its_transient() {
    let mut long_body = features();
    long_body.body_end_sample += 48;
    assert!(!long_body.has_valid_layout());
    let mut inside_head = features();
    inside_head.body_end_sample = 4_800 + 29 * 48;
    assert!(!inside_head.has_valid_layout());
    let mut orphan = features();
    orphan.transient_db = None;
    assert!(!orphan.has_valid_layout());
    let mut cut = features();
    cut.body_end_sample = 4_800 + 40 * 48;
    cut.body_rms_dbfs = None;
    cut.transient_db = None;
    assert!(cut.has_valid_layout());
    let mut short_with_value = features();
    short_with_value.body_end_sample = 4_800 + 49 * 48;
    assert!(!short_with_value.has_valid_layout(), "19 bins have no body");
    short_with_value.body_end_sample += 48;
    assert!(short_with_value.has_valid_layout(), "20 bins have one");
    let mut full_without_value = features();
    full_without_value.body_rms_dbfs = None;
    full_without_value.transient_db = None;
    assert!(!full_without_value.has_valid_layout());
}

#[test]
fn non_finite_or_negative_values_are_invalid() {
    let mut peak = features();
    peak.sample_peak_dbfs = f32::NAN;
    assert!(!peak.has_valid_layout());
    let mut body = features();
    body.body_rms_dbfs = Some(f32::INFINITY);
    assert!(!body.has_valid_layout());
    let mut sharpness = features();
    sharpness.sharpness_acum = Some(-0.1);
    assert!(!sharpness.has_valid_layout());
    let mut unmeasured = features();
    unmeasured.sharpness_acum = None;
    assert!(unmeasured.has_valid_layout());
}
