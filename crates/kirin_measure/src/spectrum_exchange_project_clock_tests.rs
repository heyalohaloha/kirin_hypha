//! Regression: missing presentation latency still permits a same-project-clock FREQ pair.
use super::*;

#[test]
fn project_pair_publishes_pre_snapshot_and_zero_delta() {
    let fixture = Fixture::new();
    feed(&fixture.pre_runtime, SpectrumInputClock::LocalProject);
    feed(&fixture.post_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.serve_pre());
    assert_eq!(
        read_snapshot(&fixture.target.instance_dir)
            .unwrap()
            .clock_kind,
        SpectrumClockKind::ProjectTimeline
    );
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    let view = fixture.post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let delta = view.difference.as_ref().unwrap();
    assert!(delta.raw_db.iter().all(|value| value.abs() < 1.0e-6));
    assert!(view.spectrum_timeline.frames().count() > 0);
}

fn assert_unavailable(fixture: &Fixture) {
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    let view = fixture.post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Unavailable);
    assert!(view.difference.is_none());
    assert_eq!(view.spectrum_timeline.frames().count(), 0);
    assert!(view.post_spectrum.is_some());
}

#[test]
fn mixed_clock_kinds_retire_delta_immediately_in_both_directions() {
    let fixture = Fixture::new();
    fixture.paired();
    feed(&fixture.pre_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.serve_pre());
    assert_unavailable(&fixture);
    // Returning to one kind at the same endpoint publishes a fresh pair, not the old lease.
    feed(&fixture.post_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert_eq!(
        fixture.post.try_view().unwrap().status,
        SpectrumViewStatus::Active
    );
    feed(&fixture.pre_runtime, SpectrumInputClock::LegacyPresentation);
    assert!(fixture.serve_pre());
    assert_unavailable(&fixture);
    feed(
        &fixture.post_runtime,
        SpectrumInputClock::LegacyPresentation,
    );
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert_eq!(
        fixture.post.try_view().unwrap().status,
        SpectrumViewStatus::Active
    );
}

#[test]
fn project_pair_waits_for_both_complete_native_windows() {
    let fixture = Fixture::new();
    for runtime in [&fixture.pre_runtime, &fixture.post_runtime] {
        assert!(runtime.push_block_from_audio_with_clock(
            &[0.0; 512],
            2,
            Some(SpectrumInputClock::LocalProject(0))
        ));
    }
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert!(fixture.post.try_view().unwrap().difference.is_none());
    feed(&fixture.pre_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_some());
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert!(fixture.post.try_view().unwrap().difference.is_none());
    feed(&fixture.post_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert_eq!(
        fixture.post.try_view().unwrap().status,
        SpectrumViewStatus::Active
    );
}

#[test]
fn codec_preserves_each_clock_kind_and_rejects_untagged_or_unknown_publications() {
    let fixture = Fixture::new();
    fixture.paired();
    let snapshot = read_snapshot(&fixture.target.instance_dir).unwrap();
    for kind in [
        SpectrumClockKind::Presentation,
        SpectrumClockKind::ProjectTimeline,
    ] {
        let bytes = encode_snapshot(snapshot.request_id, kind, &snapshot.history);
        let decoded = decode_snapshot(&bytes).unwrap();
        assert_eq!(decoded.clock_kind, kind);
        assert_eq!(decoded.history, snapshot.history);
        for value in [0_u16, 3, u16::MAX] {
            let mut invalid = bytes.clone();
            invalid[26..28].copy_from_slice(&value.to_le_bytes());
            assert!(decode_snapshot(&invalid).is_none());
        }
        let mut old = bytes;
        old[..8].copy_from_slice(b"KHSPEC04");
        assert!(decode_snapshot(&old).is_none());
    }
}
