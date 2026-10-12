use super::*;

fn content(position: i64) -> SpectrumInputClock {
    SpectrumInputClock::VerifiedVst3 {
        start_samples: position,
        proof_revision: 1,
    }
}

#[test]
fn content_clock_freq_rejects_both_mixed_directions_and_recovers() {
    let fixture = Fixture::new();
    for (pre_content, post_content) in [(true, true), (false, true), (true, false), (true, true)] {
        feed(&fixture.pre_runtime, |p| {
            if pre_content {
                content(p)
            } else {
                SpectrumInputClock::LocalProject(p)
            }
        });
        feed(&fixture.post_runtime, |p| {
            if post_content {
                content(p)
            } else {
                SpectrumInputClock::LocalProject(p)
            }
        });
        assert!(fixture.serve_pre());
        assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
        let view = fixture.post.try_view().unwrap();
        if pre_content && post_content {
            assert_eq!(view.status, SpectrumViewStatus::Active);
            assert!(view
                .difference
                .unwrap()
                .raw_db
                .iter()
                .all(|v| v.abs() < 1.0e-6));
            assert_eq!(
                read_snapshot(&fixture.target.instance_dir)
                    .unwrap()
                    .clock_kind,
                SpectrumClockKind::Vst3Content
            );
        } else {
            assert_eq!(view.status, SpectrumViewStatus::Unavailable);
            assert!(view.difference.is_none());
        }
    }
    let cut = fixture
        .post
        .try_view_after_clone(|| fixture.post_runtime.note_analysis_clock_drop_from_audio())
        .unwrap();
    assert!(cut.difference.is_none());
    assert_eq!(cut.status, SpectrumViewStatus::WarmingUp);
    // A join that was already reading when the proof disappeared must not renew the old lease.
    let session = fixture
        .post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .clone();
    fixture
        .post
        .join_post_view_after_read(&session, &fixture.target, Instant::now(), || {
            fixture.post_runtime.note_analysis_clock_drop_from_audio();
        });
    assert!(fixture.post.try_view().unwrap().difference.is_none());
}

#[test]
fn audio_unit_clock_never_joins_vst3_or_legacy_coordinates() {
    let fixture = Fixture::new();
    for foreign in [
        None,
        Some(content(0)),
        Some(SpectrumInputClock::LocalProject(0)),
        Some(SpectrumInputClock::LegacyPresentation(0)),
    ] {
        for reverse in [false, true] {
            for (pre, runtime) in [(true, &fixture.pre_runtime), (false, &fixture.post_runtime)] {
                feed(runtime, |start| {
                    if pre == reverse {
                        match foreign {
                            Some(SpectrumInputClock::VerifiedVst3 { .. }) => return content(start),
                            Some(SpectrumInputClock::LocalProject(_)) => {
                                return SpectrumInputClock::LocalProject(start)
                            }
                            Some(SpectrumInputClock::LegacyPresentation(_)) => {
                                return SpectrumInputClock::LegacyPresentation(start)
                            }
                            _ => (),
                        }
                    }
                    SpectrumInputClock::VerifiedAudioUnit {
                        start_samples: start,
                        proof_revision: 1,
                    }
                });
            }
            assert!(fixture.serve_pre());
            assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
            let view = fixture.post.try_view().unwrap();
            assert_eq!(view.difference.is_some(), foreign.is_none());
            if let Some(delta) = view.difference {
                assert!(delta.raw_db.iter().all(|v| v.abs() < 1.0e-6));
            }
        }
    }
}
