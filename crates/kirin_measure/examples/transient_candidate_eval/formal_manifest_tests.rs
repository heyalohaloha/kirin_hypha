use tempfile::tempdir;

use super::*;
use crate::contract::synthetic_formal_authorization;

#[test]
fn parses_selector_manifest_and_cross_checks_all_fold_rows() {
    let fixture = make_fixture();
    let authorization = authorization(&fixture);
    let bundle = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap();
    assert_eq!(bundle.selection.entries.len(), 290);
    assert_eq!(bundle.selection.sha256, fixture.manifest_sha256);
    assert_eq!(bundle.folds_sha256, fixture.folds_sha256);
    assert_eq!(bundle.selection.entries[0].formal.as_ref().unwrap().fold, 0);
    assert_eq!(
        bundle.selection.entries[289]
            .formal
            .as_ref()
            .unwrap()
            .selection_rank,
        290
    );
}

#[test]
fn split_leakage_and_fold_disagreement_fail_closed() {
    let fixture = make_fixture();
    let changed_hash = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        fields[11] = "test".to_string();
        fields[1] = performance_rank_key(&fields[11], &fields[5]).unwrap();
    });
    let authorization = synthetic_formal_authorization(&changed_hash, &fixture.folds_sha256);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("split leakage"), "{error}");

    let fixture = make_fixture();
    let folds_bytes = fs::read_to_string(&fixture.folds).unwrap();
    let changed = folds_bytes.replacen("id0,0,", "id0,1,", 1);
    fs::write(&fixture.folds, &changed).unwrap();
    let changed_hash = sha256_bytes(changed.as_bytes());
    let authorization = synthetic_formal_authorization(&fixture.manifest_sha256, &changed_hash);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("disagrees"), "{error}");
}

#[test]
fn pinned_hashes_and_twenty_three_columns_are_required() {
    let fixture = make_fixture();
    let authorization = synthetic_formal_authorization(&"00".repeat(32), &fixture.folds_sha256);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("manifest SHA-256 mismatch"), "{error}");

    let fixture = make_fixture();
    let text = fs::read_to_string(&fixture.manifest).unwrap();
    let mut lines = text.lines().map(str::to_string).collect::<Vec<_>>();
    lines[1].push_str(",extra");
    let changed = format!("{}\n", lines.join("\n"));
    fs::write(&fixture.manifest, &changed).unwrap();
    let authorization =
        synthetic_formal_authorization(&sha256_bytes(changed.as_bytes()), &fixture.folds_sha256);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("23 nonempty fields"), "{error}");
}

#[test]
fn rank_key_and_hash_window_bounds_are_recomputed_from_identity() {
    let fixture = make_fixture();
    let changed_hash = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        fields[1] = "00".repeat(32);
    });
    let authorization = synthetic_formal_authorization(&changed_hash, &fixture.folds_sha256);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("pinned rank mapping"), "{error}");

    let fixture = make_fixture();
    let changed_hash = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        fields[10] = "60".to_string();
        let expected = excerpt_bounds_44100(60 * 44_100, &fields[11], &fields[5]).unwrap();
        let wrong_start = if expected.start_sample == 0 {
            441
        } else {
            expected.start_sample - 441
        };
        fields[16] = wrong_start.to_string();
        fields[17] = (wrong_start + 1_323_000).to_string();
    });
    let authorization = synthetic_formal_authorization(&changed_hash, &fixture.folds_sha256);
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("pinned mapping"), "{error}");
}

#[test]
fn nonzero_hash_window_uses_window_length_for_density() {
    let fixture = make_fixture();
    let changed_hash = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        fields[10] = "60".to_string();
        let expected = excerpt_bounds_44100(60 * 44_100, &fields[11], &fields[5]).unwrap();
        assert!(expected.start_sample > 0);
        fields[16] = expected.start_sample.to_string();
        fields[17] = expected.end_sample.to_string();
    });
    let authorization = synthetic_formal_authorization(&changed_hash, &fixture.folds_sha256);
    let parsed = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap();
    assert!(
        parsed.selection.entries[0]
            .formal
            .as_ref()
            .unwrap()
            .excerpt_start_sample_44100
            > 0
    );
}

#[test]
fn zero_event_excerpt_is_valid_manifest_metadata_for_negative_intervals() {
    let fixture = make_fixture();
    let changed_hash = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        for field in &mut fields[18..=22] {
            *field = "0".to_string();
        }
    });
    let authorization = synthetic_formal_authorization(&changed_hash, &fixture.folds_sha256);
    let parsed = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap();
    let formal = parsed.selection.entries[0].formal.as_ref().unwrap();
    assert_eq!(formal.declared_excerpt_raw_notes, 0);
    assert_eq!(formal.declared_excerpt_compound_events, 0);
}

#[test]
fn selector_exact_size_and_equal_fold_sizes_are_fail_closed() {
    let fixture = make_fixture_with_rows(285);
    let auth = authorization(&fixture);
    let error = read_formal_selection(
        &auth,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("exactly 290"), "{error}");

    let fixture = make_fixture_with_rows(295);
    let auth = authorization(&fixture);
    let error = read_formal_selection(
        &auth,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("exactly 290"), "{error}");

    let fixture = make_fixture();
    let manifest_sha256 = rewrite_first_manifest_row(&fixture.manifest, |fields| {
        fields[2] = "1".to_string();
    });
    let folds = fs::read_to_string(&fixture.folds)
        .unwrap()
        .replacen("id0,0,", "id0,1,", 1);
    fs::write(&fixture.folds, folds.as_bytes()).unwrap();
    let authorization =
        synthetic_formal_authorization(&manifest_sha256, &sha256_bytes(folds.as_bytes()));
    let error = read_formal_selection(
        &authorization,
        fixture.directory.path(),
        &fixture.manifest,
        &fixture.folds,
    )
    .unwrap_err();
    assert!(error.contains("equal fold size"), "{error}");
}

#[test]
fn duration_quota_uses_checked_integer_sample_totals() {
    assert!(
        validate_development_minima(&minimum_entries(MIN_EXCERPT_DURATION_SAMPLES_44100)).is_ok()
    );
    let error =
        validate_development_minima(&minimum_entries(MIN_EXCERPT_DURATION_SAMPLES_44100 - 1))
            .unwrap_err();
    assert!(error.contains("79380000 excerpt samples"), "{error}");
}

fn authorization(fixture: &Fixture) -> crate::contract::FormalAuthorization {
    synthetic_formal_authorization(&fixture.manifest_sha256, &fixture.folds_sha256)
}

struct Fixture {
    directory: tempfile::TempDir,
    manifest: PathBuf,
    folds: PathBuf,
    manifest_sha256: String,
    folds_sha256: String,
}

fn make_fixture() -> Fixture {
    make_fixture_with_rows(290)
}

fn make_fixture_with_rows(row_count: usize) -> Fixture {
    let directory = tempdir().unwrap();
    let mut manifest = format!("{DEVELOPMENT_HEADER}\n");
    let mut folds = format!("{FOLDS_HEADER}\n");
    for index in 0..row_count {
        let rank = index + 1;
        let fold = index % 5;
        let drummer = REQUIRED_DRUMMERS[index % REQUIRED_DRUMMERS.len()];
        let session = format!("session{}", index % 15);
        let id = format!("id{index}");
        let style = format!("style{}", index % 8);
        let beat_type = if index % 2 == 0 { "beat" } else { "fill" };
        let split = if index < 6 { "validation" } else { "train" };
        let selection_key = performance_rank_key(split, &id).unwrap();
        let midi = format!("synthetic_{index}.mid");
        let audio = format!("synthetic_{index}.wav");
        let kit = format!("kit{}", index % 8);
        fs::write(
            directory.path().join(&midi),
            b"synthetic path placeholder; not MIDI data",
        )
        .unwrap();
        fs::write(
            directory.path().join(&audio),
            b"synthetic path placeholder; not audio data",
        )
        .unwrap();
        manifest.push_str(&format!(
            "{rank},{selection_key},{fold},{drummer},{session},{id},{style},120,{beat_type},4-4,30,{split},{midi},{audio},{kit},{:064x},0,1323000,20,10,4,4,0.333333333\n",
            index + 2_000,
        ));
        folds.push_str(&format!(
            "{id},{fold},{drummer},{session},{drummer},{session},{rank}\n"
        ));
    }
    let manifest_path = directory.path().join("manifest.csv");
    let folds_path = directory.path().join("folds.csv");
    fs::write(&manifest_path, manifest.as_bytes()).unwrap();
    fs::write(&folds_path, folds.as_bytes()).unwrap();
    Fixture {
        manifest_sha256: sha256_bytes(manifest.as_bytes()),
        folds_sha256: sha256_bytes(folds.as_bytes()),
        directory,
        manifest: manifest_path,
        folds: folds_path,
    }
}

fn rewrite_first_manifest_row(path: &Path, edit: impl FnOnce(&mut Vec<String>)) -> String {
    let text = fs::read_to_string(path).unwrap();
    let mut lines = text.lines().map(str::to_string).collect::<Vec<_>>();
    let mut fields = parse_csv_line(&lines[1]).unwrap();
    edit(&mut fields);
    lines[1] = fields.join(",");
    let changed = format!("{}\n", lines.join("\n"));
    fs::write(path, changed.as_bytes()).unwrap();
    sha256_bytes(changed.as_bytes())
}

fn minimum_entries(total_samples: u64) -> Vec<Selection> {
    let base = total_samples / FORMAL_PERFORMANCE_IDS as u64;
    let remainder = total_samples % FORMAL_PERFORMANCE_IDS as u64;
    (0..FORMAL_PERFORMANCE_IDS)
        .map(|index| {
            let length = base + u64::from((index as u64) < remainder);
            Selection {
                drummer: REQUIRED_DRUMMERS[index % REQUIRED_DRUMMERS.len()].to_string(),
                session: format!("session{}", index % 15),
                id: format!("minimum-id-{index}"),
                style: format!("style{}", index % 8),
                bpm: 120.0,
                beat_type: if index.is_multiple_of(2) {
                    "beat".to_string()
                } else {
                    "fill".to_string()
                },
                time_signature: "4-4".to_string(),
                declared_duration: length as f64 / f64::from(EXCERPT_SAMPLE_RATE),
                split: "train".to_string(),
                midi: PathBuf::from(format!("minimum-{index}.mid")),
                audio: PathBuf::from(format!("minimum-{index}.wav")),
                kit_name: format!("kit{}", index % 8),
                formal: Some(FormalSelectionMetadata {
                    selection_rank: index as u32 + 1,
                    selection_key: format!("{index:064x}"),
                    fold: (index % 5) as u8,
                    expected_midi_sha256: "11".repeat(32),
                    declared_excerpt_raw_notes: 0,
                    declared_excerpt_compound_events: 0,
                    declared_excerpt_kick_only_events: 0,
                    declared_excerpt_hat_only_events: 0,
                    declared_excerpt_density_events_per_second: 0.0,
                    excerpt_start_sample_44100: 0,
                    excerpt_end_sample_44100: length,
                }),
            }
        })
        .collect()
}
