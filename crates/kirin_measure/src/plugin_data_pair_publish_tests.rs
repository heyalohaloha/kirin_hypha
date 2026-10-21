// Exercise both finalizers against the same durable pending snapshots, without worker timing.
use super::*;

struct PendingPair {
    base: PathBuf,
    pre: PluginDataWriter,
    post: PluginDataWriter,
}

impl PendingPair {
    fn new() -> Self {
        Self::with_bound_clock(true)
    }

    fn with_bound_clock(bound: bool) -> Self {
        let base = isolated_dir();
        let mut pre = complete_pair_writer(
            &base,
            Role::Pre,
            "iid-pre-once",
            None,
            Some("iid-post-once".into()),
        );
        let mut post = complete_pair_writer(
            &base,
            Role::Post,
            "iid-post-once",
            Some("iid-pre-once".into()),
            None,
        );
        crate::record_expected::begin_expected_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap();
        let mut expected = fresh_expected_wav_fixture(48_000);
        expected.wav_time_reference_samples = Some(0);
        let start = expected.created_at_ms - 2_000;
        for writer in [&mut pre, &mut post] {
            set_late_expected_record_time(writer, start, expected.created_at_ms);
            writer.set_expected_wav(Some(expected.clone()));
            let reference = writer.data.trace_wav_reference.as_mut().unwrap();
            reference.bounce_id = expected.bounce_id.clone();
            reference.wav_hash = expected.wav_hash.clone().unwrap();
            writer.data.trace_clock_resolution = Some("producer_plus_output_latency".into());
            writer.data.status = Status::Closed;
            writer.data.commit_status = Some("pair_pending".into());
            writer.set_trace_clock_observations(
                writer
                    .data
                    .frames
                    .iter()
                    .enumerate()
                    .map(|(i, frame)| TraceClockObservation {
                        frame: frame.clone(),
                        producer_position_samples: Some((i as i64 + 1) * 4_800),
                        raw_host_position_samples: Some((i as i64 + 1) * 4_800),
                        capture_epoch: Some(1),
                        clock_source: Some("project_timeline".into()),
                        presentation_latency_source: Some("vst3".into()),
                        input_presentation_latency_samples: Some(0),
                        output_presentation_latency_samples: Some(0),
                    })
                    .collect(),
            );
        }
        // Live writers have already bound their clock journal to this exact WAV before close.
        if bound {
            assert!(normalize_late_expected_pair(
                &mut pre.data,
                &mut post.data,
                &expected
            ));
        }
        for writer in [&mut pre, &mut post] {
            // Writer close stages normalized facts as pending, before either finalizer runs.
            writer.data.commit_status = Some("pair_pending".into());
            let paths = self_paths_for(&writer.paths, &writer.data);
            writer.write_atomic(paths.pair_pending_path).unwrap();
        }
        assert!(closed_pair_can_accept_exact_drop(
            &pre.data,
            &post.data,
            "session-pair-atomic"
        ));
        write_drop_commit_fixture(&base, "session-pair-atomic", &expected, true);
        let inspected = crate::record_drop_commit::inspect_drop_commit_for_closed_pair_session(
            &base,
            "project_hash_test",
            "session-pair-atomic",
        )
        .unwrap()
        .expect("fixture has an exact Drop transaction");
        assert_eq!(inspected, expected);
        assert!(crate::trace_alignment::canonical_wav_alignment(&pre.data, &post.data).is_some());
        let (mut proof_pre, mut proof_post) = (pre.data.clone(), post.data.clone());
        assert!(normalize_late_expected_pair(
            &mut proof_pre,
            &mut proof_post,
            &expected
        ));
        Self { base, pre, post }
    }

    fn recover(&self) -> usize {
        reconcile_drop_committed_closed_session(
            &self.base,
            "project_hash_test",
            "session-pair-atomic",
            "iid-pre-once",
            "iid-post-once",
        )
    }

    fn published_files(&self) -> Vec<(PathBuf, Vec<u8>)> {
        let mut files = vec![pair_commit_manifest_path(&self.pre.paths, &self.pre.data).unwrap()];
        for writer in [&self.pre, &self.post] {
            let paths = self_paths_for(&writer.paths, &writer.data);
            let shelf = pair_trace_shelf_path(&paths, &writer.data).unwrap();
            let member = fs::read(&paths.member_path).unwrap();
            assert_eq!(
                fs::read(&shelf).unwrap(),
                member,
                "TRACE must mirror its member"
            );
            let data: PluginDataFile = serde_json::from_slice(&member).unwrap();
            assert!(verify_checksum(&data));
            assert_eq!(
                data.trace_clock_observations.len(),
                writer.data.trace_clock_observations.len()
            );
            files.extend([paths.member_path, shelf]);
        }
        files
            .into_iter()
            .map(|path| {
                let bytes = fs::read(&path).unwrap();
                (path, bytes)
            })
            .collect()
    }
}

impl Drop for PendingPair {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.base).unwrap();
    }
}

#[test]
fn bound_pending_pair_is_published_only_by_close_in_either_recovery_order() {
    for recover_first in [true, false] {
        let pair = PendingPair::new();
        if recover_first {
            assert_eq!(
                pair.recover(),
                0,
                "bound live Drop belongs to the closing writer"
            );
            assert!(!pair_commit_manifest_path(&pair.pre.paths, &pair.pre.data)
                .unwrap()
                .exists());
        }
        try_finalize_pair_session(&pair.pre.paths, &pair.pre.data).unwrap();
        let before = pair.published_files();
        assert_eq!(pair.recover(), 0);
        // Include the manifest in the byte comparison: even a coherent second publication is wrong.
        assert_eq!(pair.published_files(), before);
    }
}

#[test]
fn late_and_normal_preparation_preserve_identical_clock_journal_bytes() {
    let pair = PendingPair::new();
    let mut late_pre = pair.pre.data.clone();
    let mut late_post = pair.post.data.clone();
    let expected = late_pre.expected_wav.clone().unwrap();
    assert!(normalize_late_expected_pair(
        &mut late_pre,
        &mut late_post,
        &expected
    ));
    assert!(
        prepare_late_expected_pair(&mut late_pre, &mut late_post),
        "{:?}",
        pair_visibility_failure_reasons(&late_pre, &late_post)
    );
    try_finalize_pair_session(&pair.pre.paths, &pair.pre.data).unwrap();
    for (writer, late) in [(&pair.pre, &mut late_pre), (&pair.post, &mut late_post)] {
        let paths = self_paths_for(&writer.paths, &writer.data);
        let candidate = pair
            .base
            .join(format!("candidate-{}.json", late.role.dir_name()));
        write_plugin_data_atomic(&candidate, late).unwrap();
        assert_eq!(
            fs::read(candidate).unwrap(),
            fs::read(paths.member_path).unwrap(),
            "both preparation paths must encode the same measured facts"
        );
    }
}

#[test]
fn stopped_or_partially_bound_drop_still_recovers() {
    for clear_both in [true, false] {
        let mut pair = PendingPair::with_bound_clock(false);
        pair.post.data.expected_wav = None;
        if clear_both {
            pair.pre.data.expected_wav = None;
        }
        for writer in [&mut pair.pre, &mut pair.post] {
            let paths = self_paths_for(&writer.paths, &writer.data);
            writer.write_atomic(paths.pair_pending_path).unwrap();
        }
        assert_eq!(
            pair.recover(),
            2,
            "Stop-before-Drop rescue must remain available"
        );
        pair.published_files();
    }
}

#[test]
fn startup_finishes_bound_pending_pair_without_live_writers() {
    let pair = PendingPair::new();
    assert_eq!(pair.recover(), 0);
    assert!(finalize_pair_pending_sessions(&pair.base) > 0);
    pair.published_files();
    assert_eq!(finalize_pair_pending_sessions(&pair.base), 0);
}
