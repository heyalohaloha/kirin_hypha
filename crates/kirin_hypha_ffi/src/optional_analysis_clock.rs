//! Optional analysis clock policy; independent from Record and capture coordinates.
use super::*;
use kirin_measure::spectrum_runtime::SpectrumInputClock;

/// The wrapper defers AAX Perceptual and AU/certified VST3 FREQ/Perceptual until its existing
/// timing observer has run, or the measurement-only host project certificate has supplied its
/// coordinate. The latter never grants LISTEN permission. Absolute analysis (LIVE, absolute PSB) keeps the ordinary project-timeline route on
/// every AAX host, so it never waits for a pair proof or a certified engine clock.
/// Watch/Record/ATTACK retain their original, independently staged clocks and audio ingress.
#[derive(Default)]
pub(super) struct AaxAnalysisIngress {
    enabled: AtomicBool,
    content: AtomicBool,
    audio_unit: AtomicBool,
    post: AtomicBool,
    frames: AtomicU64,
    local_spectrum_start: AtomicI64,
    local_spectrum_valid: AtomicBool,
}

impl AaxAnalysisIngress {
    pub(super) fn configure(&self, enabled: bool, post: bool) {
        self.content.store(false, Ordering::Release);
        self.audio_unit.store(false, Ordering::Release);
        self.frames.store(0, Ordering::Release);
        self.local_spectrum_valid.store(false, Ordering::Release);
        self.post.store(post, Ordering::Release);
        self.enabled.store(enabled, Ordering::Release);
    }

    fn deferred(&self, mode: AnalysisViewMode) -> bool {
        self.enabled.load(Ordering::Acquire)
            && (mode == AnalysisViewMode::Perceptual
                || (self.content.load(Ordering::Acquire) && mode == AnalysisViewMode::Spectrum))
    }

    pub(super) fn begin_callback(&self) {
        self.frames.store(0, Ordering::Relaxed);
        self.local_spectrum_valid.store(false, Ordering::Relaxed);
    }
}

impl KirinHyphaEngine {
    pub(super) fn push_optional_analysis(
        &self,
        interleaved: &[f32],
        clock: Option<PendingCaptureWindow>,
    ) {
        if self
            .aax_analysis
            .deferred(self.spectrum_runtime.analysis_mode())
        {
            // AU's local observation needs only this admitted callback's own clock. The
            // correspondence observer also checks PRE/POST and loop movement, so its local
            // validity must not gate either role's local FREQ. Keep it strictly unjoinable.
            if self.aax_analysis.audio_unit.load(Ordering::Acquire)
                && self.spectrum_runtime.analysis_mode() == AnalysisViewMode::Spectrum
            {
                if let Some(clock) = clock.filter(|clock| {
                    clock.position_valid
                        && matches!(
                            clock.clock_source,
                            CaptureClockSource::ProjectTimeline
                                | CaptureClockSource::AudioRenderTimeline
                        )
                }) {
                    self.aax_analysis
                        .local_spectrum_start
                        .store(clock.position_samples, Ordering::Relaxed);
                    self.aax_analysis
                        .local_spectrum_valid
                        .store(true, Ordering::Relaxed);
                }
            }
            self.aax_analysis.frames.store(
                (interleaved.len() / self.num_channels) as u64,
                Ordering::Release,
            );
        } else {
            let _ = self.spectrum_runtime.push_block_from_audio_with_clock(
                interleaved,
                self.num_channels,
                clock.and_then(spectrum_input_clock),
            );
        }
        if let Some(runtime) = self.attack_runtime.as_ref() {
            let _ = runtime.push_block_from_audio(
                interleaved,
                self.num_channels,
                clock.and_then(attack_timeline_start),
            );
        }
    }
}

/// Prepare-thread only. Select the AAX wrapper's deferred Perceptual route; this grants no block
/// proof. The wrapper decides per callback which engine-clock proof (certified bound or the
/// ordinary linear PRE/POST proof) it supplies.
///
/// # Safety
/// `handle` must be null or a live engine; no audio callback may be running during configuration.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_aax_analysis_ingress(
    handle: *mut KirinHyphaEngine,
    enabled: bool,
    post: bool,
) {
    if let Some(engine) = unsafe { handle.as_ref() } {
        engine.aax_analysis.configure(enabled, post);
    }
}

/// Prepare-thread only. Enable the separate certified VST3 content-coordinate route for FREQ
/// and Perceptual. Uncertified VST3, Absolute and Record retain their own clocks.
/// # Safety
/// Same suspended-audio contract as `kirin_hypha_set_aax_analysis_ingress`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_content_analysis_ingress(
    handle: *mut KirinHyphaEngine,
    post: bool,
) {
    if let Some(engine) = unsafe { handle.as_ref() } {
        engine.aax_analysis.configure(true, post);
        engine.aax_analysis.content.store(true, Ordering::Release);
    }
}

/// Prepare-thread only. AU FREQ/Perceptual use a distinct render coordinate with the callback's
/// LISTEN pair proof. A render timestamp alone grants no PRE/POST correspondence.
/// # Safety
/// Same suspended-audio contract as `kirin_hypha_set_aax_analysis_ingress`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_audio_unit_analysis_ingress(
    handle: *mut KirinHyphaEngine,
    post: bool,
) {
    if let Some(engine) = unsafe { handle.as_ref() } {
        engine.aax_analysis.configure(true, post);
        engine.aax_analysis.content.store(true, Ordering::Release);
        engine
            .aax_analysis
            .audio_unit
            .store(true, Ordering::Release);
    }
}

/// Audio Thread only. Feed the original, pre-audition scratch exactly once after timing preparation.
/// A POST difference requires the current PRE
/// lifetime/generation/K proof, or the certified measurement-only project coordinate; PRE publishes
/// the corresponding local AddClock, AU render or certified content coordinate.
/// `revision` changes whenever that proof changes, and never wraps. Neither coordinate is an
/// output-presentation latency or a Record clock. Absolute analysis never uses this route;
/// unproven POST FREQ and AU PRE FREQ retain a local, unjoinable spectrum. AU takes its position from
/// this callback's admitted capture window, independently of the correspondence observer.
///
/// # Safety
/// `handle` is null or live. A non-null `interleaved` spans `num_frames * num_channels` floats from
/// this callback's admitted capture transaction. Only its single Audio Thread calls this function.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn kirin_hypha_push_paired_analysis(
    handle: *mut KirinHyphaEngine,
    interleaved: *const f32,
    num_frames: usize,
    num_channels: u32,
    local_valid: bool,
    local_start: i64,
    local_revision: u64,
    paired_valid: bool,
    paired_start: i64,
    paired_revision: u64,
) -> bool {
    let Some(engine) = (unsafe { handle.as_ref() }) else {
        return false;
    };
    if !engine
        .aax_analysis
        .deferred(engine.spectrum_runtime.analysis_mode())
        || !engine.spectrum_runtime.is_enabled()
    {
        return false;
    }
    let staged = engine.aax_analysis.frames.swap(0, Ordering::AcqRel);
    let content = engine.aax_analysis.content.load(Ordering::Acquire);
    let post = engine.aax_analysis.post.load(Ordering::Acquire);
    let audio_unit = engine.aax_analysis.audio_unit.load(Ordering::Acquire);
    // Local FREQ needs no correspondence proof. AU PRE may lack an independent project clock;
    // LocalRender retains its observation but cannot be published by the PRE exchange.
    let local_spectrum = content
        && if post {
            !paired_valid
        } else {
            audio_unit && !local_valid
        }
        && engine.spectrum_runtime.analysis_mode() == AnalysisViewMode::Spectrum;
    let (valid, start, revision) = if audio_unit && local_spectrum {
        (
            engine
                .aax_analysis
                .local_spectrum_valid
                .load(Ordering::Relaxed),
            engine
                .aax_analysis
                .local_spectrum_start
                .load(Ordering::Relaxed),
            1,
        )
    } else if post && !local_spectrum {
        (paired_valid, paired_start, paired_revision)
    } else {
        (local_valid, local_start, local_revision)
    };
    let revision_limit = 1_u64
        << if audio_unit {
            60
        } else if content {
            61
        } else {
            62
        };
    let samples = num_frames.checked_mul(num_channels as usize);
    if !valid
        || revision == 0
        || revision >= revision_limit
        || interleaved.is_null()
        || num_frames == 0
        || num_frames > MAX_AUDIO_BLOCK_FRAMES
        || start.checked_add(num_frames as i64).is_none()
        || staged != num_frames as u64
        || num_channels as usize != engine.num_channels
        || !(1..=2).contains(&num_channels)
        || samples.is_none_or(|n| n > isize::MAX as usize / std::mem::size_of::<f32>())
    {
        engine
            .spectrum_runtime
            .note_analysis_clock_drop_from_audio();
        return false;
    }
    let audio = unsafe { std::slice::from_raw_parts(interleaved, samples.unwrap_or(0)) };
    engine.spectrum_runtime.push_block_from_audio_with_clock(
        audio,
        engine.num_channels,
        Some(if local_spectrum {
            SpectrumInputClock::LocalRender(start)
        } else if audio_unit {
            SpectrumInputClock::VerifiedAudioUnit {
                start_samples: start,
                proof_revision: revision,
            }
        } else if content {
            SpectrumInputClock::VerifiedVst3 {
                start_samples: start,
                proof_revision: revision,
            }
        } else {
            SpectrumInputClock::VerifiedAax {
                start_samples: start,
                proof_revision: revision,
            }
        }),
    )
}

/// Compatibility entry for callers of the original AAX-only ABI.
/// # Safety
/// Same original-input and single Audio Thread contract as `kirin_hypha_push_paired_analysis`.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn kirin_hypha_push_aax_analysis(
    handle: *mut KirinHyphaEngine,
    interleaved: *const f32,
    num_frames: usize,
    num_channels: u32,
    local_valid: bool,
    local_start: i64,
    local_revision: u64,
    paired_valid: bool,
    paired_start: i64,
    paired_revision: u64,
) -> bool {
    unsafe {
        kirin_hypha_push_paired_analysis(
            handle,
            interleaved,
            num_frames,
            num_channels,
            local_valid,
            local_start,
            local_revision,
            paired_valid,
            paired_start,
            paired_revision,
        )
    }
}

/// Missing optional latency retains project coordinates; FREQ pairs only the same clock kind.
#[inline]
pub(super) fn spectrum_input_clock(clock: PendingCaptureWindow) -> Option<SpectrumInputClock> {
    if !clock.position_valid {
        return None;
    }
    if matches!(
        clock.presentation_latency.source,
        PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
    ) && clock.presentation_latency.output.is_some()
    {
        return Some(SpectrumInputClock::Presentation {
            start_samples: spectrum_presentation_start(clock)?,
            source: clock.presentation_latency.source,
            output_latency_samples: clock.presentation_latency.output?,
        });
    }
    match clock.clock_source {
        CaptureClockSource::ProjectTimeline => {
            Some(SpectrumInputClock::LocalProject(clock.position_samples))
        }
        CaptureClockSource::AudioRenderTimeline => {
            Some(SpectrumInputClock::LocalRender(clock.position_samples))
        }
        _ => None,
    }
}

#[inline]
pub(super) fn spectrum_presentation_start(clock: PendingCaptureWindow) -> Option<i64> {
    if !clock.position_valid
        || !matches!(
            clock.presentation_latency.source,
            PresentationLatencySource::Vst3 | PresentationLatencySource::AudioUnitV2
        )
    {
        return None;
    }
    clock
        .presentation_latency
        .output
        .and_then(|latency| clock.position_samples.checked_add(i64::from(latency)))
}

/// Clock for the POST-only on-demand ATTACK worker.
///
/// Prefer the host's output-presentation clock when the optional VST3/AU extension is present.
/// Studio Pro can omit that optional callback while still supplying the exact project sample
/// position on every rendered block. The ATTACK worker may use that producer clock because it
/// displays only relative POST event positions; it does not perform the public PRE/POST join.
/// Unknown or invalid producer clocks remain fail-closed.
#[inline]
pub(super) fn attack_timeline_start(clock: PendingCaptureWindow) -> Option<i64> {
    spectrum_presentation_start(clock).or_else(|| {
        (clock.position_valid
            && matches!(
                clock.clock_source,
                CaptureClockSource::ProjectTimeline | CaptureClockSource::AudioRenderTimeline
            ))
        .then_some(clock.position_samples)
    })
}

#[cfg(test)]
#[path = "aax_analysis_ffi_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "content_analysis_ffi_tests.rs"]
mod content_tests;

#[cfg(test)]
#[path = "au_cycle_spectrum_ffi_tests.rs"]
mod au_cycle_tests;
