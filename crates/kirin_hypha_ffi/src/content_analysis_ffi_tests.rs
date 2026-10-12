//! The real FFI, workers and file exchange, with disposable storage and delayed changing PCM.
use super::*;
use kirin_measure::channel_layout::ChannelLayout;
use kirin_measure::spectrum_exchange::SpectrumTarget;
use std::{collections::VecDeque, thread, time::Duration};

const FRAMES: usize = 256;
const DELAY: i64 = 4096;

fn signal(position: i64) -> [f32; 2] {
    let t = position as f64 / 48_000.0;
    let sweep = (std::f64::consts::TAU * 2.3 * t).sin();
    let value = 0.12 * (t * 628.0).sin() + (0.08 + 0.07 * sweep) * (t * 27000.0).sin();
    [
        value as f32,
        (value * 0.8 + 0.03 * (t * 9000.0).sin()) as f32,
    ]
}

struct Fixture {
    _temp: tempfile::TempDir,
    pre: Box<KirinHyphaEngine>,
    post: Box<KirinHyphaEngine>,
    target: SpectrumTarget,
    line: VecDeque<[f32; 2]>,
    position: i64,
    delay: i64,
    latency: Option<u32>,
    verified: bool,
    include_delay: bool,
    audio_unit: bool,
    project_only: bool,
    loop_samples: i64,
    previous: [Option<i64>; 2],
    revisions: [u64; 2],
}

impl Fixture {
    fn new(mode: AnalysisViewMode, delay: i64, latency: Option<u32>, verified: bool) -> Self {
        Self::at_rate(mode, delay, latency, verified, 48_000)
    }

    fn at_rate(
        mode: AnalysisViewMode,
        delay: i64,
        latency: Option<u32>,
        verified: bool,
        rate: u32,
    ) -> Self {
        let temp = tempfile::tempdir().unwrap();
        let pre_json = temp.path().join("pre/pre.json");
        std::fs::create_dir_all(pre_json.parent().unwrap()).unwrap();
        std::fs::write(&pre_json, b"{}").unwrap();
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        let mut pre = Box::new(KirinHyphaEngine::new(rate, ChannelLayout::stereo()));
        let mut post = Box::new(KirinHyphaEngine::new(rate, ChannelLayout::stereo()));
        if verified {
            unsafe {
                kirin_hypha_set_content_analysis_ingress(pre.as_mut(), false);
                kirin_hypha_set_content_analysis_ingress(post.as_mut(), true);
            }
        }
        assert!(post.spectrum.set_post_analysis_mode(mode));
        post.spectrum.set_post_visible(true);
        let fixture = Self {
            _temp: temp,
            pre,
            post,
            target,
            line: (-delay..0).map(signal).collect(),
            position: 0,
            delay,
            latency,
            verified,
            include_delay: true,
            audio_unit: false,
            project_only: false,
            loop_samples: 0,
            previous: [None; 2],
            revisions: [1; 2],
        };
        fixture.exchange();
        fixture
    }

    fn exchange(&self) {
        self.post
            .spectrum
            .service_post_endpoint("post", Some(self.target.clone()), "fixture");
        self.pre
            .spectrum
            .service_pre_endpoint("pre", &self.target.instance_dir);
        self.post
            .spectrum
            .service_post_endpoint("post", Some(self.target.clone()), "fixture");
    }

    fn block(&mut self, proof: bool) {
        let mut pre_audio = Vec::with_capacity(FRAMES * 2);
        let mut post_audio = Vec::with_capacity(FRAMES * 2);
        for i in 0..FRAMES as i64 {
            let sample = signal(self.position + i);
            pre_audio.extend(sample);
            self.line.push_back(sample);
            post_audio.extend(self.line.pop_front().unwrap());
        }
        let coordinates = [
            self.coordinate(self.position),
            self.coordinate(self.position - self.delay),
        ];
        for (post, engine, audio) in [
            (false, self.pre.as_mut(), pre_audio.as_slice()),
            (true, self.post.as_mut(), post_audio.as_slice()),
        ] {
            let side = usize::from(post);
            let content = coordinates[side];
            let moved = self.project_only
                && self.previous[side].is_some_and(|p| p + FRAMES as i64 != content);
            if moved {
                self.revisions[side] += 1;
            }
            self.previous[side] = Some(content);
            engine.note_capture_window_with_clocks(
                true,
                content,
                FRAMES as u64,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples {
                    source: if self.audio_unit {
                        PresentationLatencySource::AudioUnitV2
                    } else {
                        PresentationLatencySource::Vst3
                    },
                    input: None,
                    output: self.latency.map(|base| {
                        base + if post || !self.include_delay {
                            0
                        } else {
                            self.delay as u32
                        }
                    }),
                },
                AuxiliaryClockSamples {
                    source: if self.audio_unit {
                        AuxiliaryClockSource::AudioUnitRender
                    } else {
                        AuxiliaryClockSource::Vst3Continuous
                    },
                    samples: if self.project_only {
                        None
                    } else {
                        Some(if self.audio_unit {
                            self.position
                        } else {
                            content
                        })
                    },
                },
                false,
            );
            engine.push_samples(audio, 2);
            if self.verified {
                unsafe {
                    kirin_hypha_push_paired_analysis(
                        engine,
                        audio.as_ptr(),
                        FRAMES,
                        2,
                        !moved,
                        if self.audio_unit {
                            self.position
                        } else {
                            content
                        },
                        self.revisions[side],
                        proof && !moved,
                        content,
                        self.revisions[side],
                    );
                }
            }
        }
        self.position += FRAMES as i64;
        self.exchange();
        // Pace below real time: debug psychoacoustic workers must not overflow their RT queue.
        thread::sleep(Duration::from_millis(8));
    }

    fn coordinate(&self, position: i64) -> i64 {
        if self.loop_samples > 0 {
            position.rem_euclid(self.loop_samples)
        } else {
            position
        }
    }

    fn change_delay(&mut self, delay: i64) {
        self.delay = delay;
        // A host rebuilding PDC primes the delay from the known source history.
        self.line = (self.position - delay..self.position).map(signal).collect();
    }

    fn deltas(&self) -> Option<(f64, f64)> {
        let view = self.post.spectrum.try_view()?;
        if view.status != SpectrumViewStatus::Active {
            return None;
        }
        if let Some(delta) = view.difference {
            return Some((
                delta
                    .raw_db
                    .iter()
                    .map(|v| f64::from(v.abs()))
                    .fold(0.0, f64::max),
                0.0,
            ));
        }
        let delta = view.perceptual_difference?;
        let psb = delta
            .pre_psb?
            .into_iter()
            .zip(delta.post_psb?)
            .map(|(pre, post)| (post - pre).abs())
            .fold(0.0, f64::max);
        Some((delta.delta_sharpness.abs(), psb))
    }

    fn observe(&mut self, proof: bool) -> (usize, f64, f64) {
        let mut result = (0, 0.0_f64, 0.0_f64);
        for _ in 0..600 {
            self.block(proof);
            if let Some((delta, psb)) = self.deltas() {
                result.0 += 1;
                result.1 = result.1.max(delta);
                result.2 = result.2.max(psb);
            }
        }
        result
    }
}

#[test]
fn content_clock_delayed_pcm_joins_zero_with_and_without_presentation_report() {
    for mode in [AnalysisViewMode::Spectrum, AnalysisViewMode::Perceptual] {
        // Missing notification, explicit zero/base latency, and downstream delay included.
        for (delay, latency) in [
            (0, Some(512)),
            (DELAY, None),
            (DELAY, Some(0)),
            (DELAY, Some(512)),
        ] {
            let mut fixture = Fixture::new(mode, delay, latency, true);
            fixture.include_delay = latency != Some(0);
            let (count, delta, psb) = fixture.observe(true);
            assert!(
                count > 20,
                "real workers must publish: {mode:?}, {latency:?}"
            );
            assert!(
                delta < 1.0e-6 && psb < 1.0e-10,
                "{mode:?}: delta={delta}, PSB={psb}"
            );
            // A single failed callback immediately revokes the old exact view and its lease.
            fixture.block(false);
            let view = fixture.post.spectrum.try_view().unwrap();
            assert!(view.difference.is_none() && view.perceptual_difference.is_none());
            let (count, delta, psb) = fixture.observe(true);
            assert!(
                count > 20 && delta < 1.0e-6 && psb < 1.0e-10,
                "fresh recovery: {mode:?} count={count}, delta={delta}, psb={psb}"
            );
        }
    }
}

#[test]
fn legacy_presentation_sum_reproduces_false_delta_with_compensated_project_time() {
    for audio_unit in [false, true] {
        for mode in [AnalysisViewMode::Spectrum, AnalysisViewMode::Perceptual] {
            let mut fixture = Fixture::new(mode, DELAY, Some(512), false);
            fixture.audio_unit = audio_unit;
            let (count, delta, psb) = fixture.observe(true);
            assert!(count > 20);
            match mode {
                AnalysisViewMode::Spectrum => assert!(
                    delta > 1.0,
                    "changing audio exposes the 4096-sample mismatch"
                ),
                _ => assert!(delta > 0.01 && psb > 0.01, "sharp={delta}, PSB={psb}"),
            }
        }
    }
}

#[test]
fn content_clock_unproven_pair_never_publishes_delta() {
    for mode in [AnalysisViewMode::Spectrum, AnalysisViewMode::Perceptual] {
        let mut fixture = Fixture::new(mode, DELAY, Some(512), true);
        assert_eq!(fixture.observe(false).0, 0);
        if mode == AnalysisViewMode::Spectrum {
            assert!(
                fixture
                    .post
                    .spectrum
                    .try_view()
                    .unwrap()
                    .post_spectrum
                    .is_some(),
                "unproven pairing preserves the absolute POST spectrum"
            );
        }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        self.pre.spectrum.shutdown();
        self.post.spectrum.shutdown();
        self.pre.spectrum_runtime.shutdown_and_join();
        self.post.spectrum_runtime.shutdown_and_join();
    }
}

#[test]
fn audio_unit_clock_delayed_pcm_and_proof_loss_use_separate_coordinates() {
    for mode in [AnalysisViewMode::Spectrum, AnalysisViewMode::Perceptual] {
        for latency in [None, Some(0), Some(512)] {
            let mut fixture = Fixture::new(mode, DELAY, latency, true);
            fixture.audio_unit = true;
            fixture.include_delay = latency != Some(0);
            unsafe {
                kirin_hypha_set_audio_unit_analysis_ingress(fixture.pre.as_mut(), false);
                kirin_hypha_set_audio_unit_analysis_ingress(fixture.post.as_mut(), true);
            }
            assert_eq!(
                fixture.observe(false).0,
                0,
                "AU cannot borrow presentation proof"
            );
            let (count, delta, psb) = fixture.observe(true);
            assert!(count > 20 && delta < 1.0e-6 && psb < 1.0e-10);
            fixture.block(false);
            assert!(
                fixture.deltas().is_none(),
                "old AU view must expire on the failing callback"
            );
            let (count, delta, psb) = fixture.observe(true);
            assert!(count > 20 && delta < 1.0e-6 && psb < 1.0e-10);
        }
    }
}

#[test]
fn certified_project_without_auxiliary_or_latency_joins_reaper_delta_at_96k() {
    for mode in [AnalysisViewMode::Spectrum, AnalysisViewMode::Perceptual] {
        for looping in [false, true] {
            let mut fixture = Fixture::at_rate(mode, DELAY, None, true, 96_000);
            fixture.project_only = true;
            fixture.loop_samples = if looping { 96_000 } else { 0 };
            fixture.change_delay(DELAY);
            for delay in [DELAY, 0, DELAY] {
                fixture.change_delay(delay);
                let (count, delta, psb) = fixture.observe(true);
                assert!(
                    count > 20,
                    "project delta must recover: {mode:?}, loop={looping}, delay={delay}"
                );
                assert!(
                    delta < 1.0e-6 && psb < 1.0e-10,
                    "project delta: {delta}, PSB {psb}"
                );
            }
            fixture.block(false);
            assert!(
                fixture.deltas().is_none(),
                "proof loss clears old project delta"
            );
        }
    }
}
