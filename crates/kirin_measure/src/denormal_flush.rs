//! Subnormal floats on the measurement threads.
//!
//! A Keep measures admitted silence: once the Audio Thread admitted a buffer, silence is factual
//! audio and passes through every Record engine. After a signal stops, the IIR states of Phase D
//! (the 28-band ISO 532-1 filter bank and its smoothing) decay into the subnormal range, where
//! x86 SSE arithmetic takes a microcode assist on every operation. On an Intel/AMD host a Keep
//! that plays on past the material then measures far below realtime and the Stop tail drains for
//! as long. ebur128 already flushes inside its own filter; this applies the same rule to the whole
//! measurement thread. A flushed value is below `f64::MIN_POSITIVE` (2.2e-308) and never reaches a
//! displayed or saved figure. The DAW audio path never runs on these threads (R-12).
use std::thread::{self, JoinHandle};

/// Set flush-to-zero and denormals-are-zero for the calling thread (x86_64 SSE). Other targets
/// keep their default floating-point environment.
pub(crate) fn flush_denormals_on_this_thread() {
    #[cfg(target_arch = "x86_64")]
    #[allow(deprecated)]
    // SAFETY: only the FTZ (bit 15) and DAZ (bit 6) flags of this thread's MXCSR change; the
    // rounding mode and exception masks stay as they are. Rust deprecates `_mm_setcsr` because
    // the compiler assumes the default floating-point environment, and documents a DAZ change as
    // undefined behaviour. This is an accepted deviation, as audio software commonly does (JUCE's
    // ScopedNoDenormals sets the same flags): the change happens once, first on the thread, before
    // any measurement code runs; the thread runs only this crate's measurement (no DAW audio path);
    // and the figures it measures are tested equal to those of a default thread. Flushing the
    // Phase D filter states in the algorithm itself would remove the deviation.
    unsafe {
        use std::arch::x86_64::{_mm_getcsr, _mm_setcsr};
        _mm_setcsr(_mm_getcsr() | 0x8040);
    }
}

/// `thread::Builder::spawn` for a named measurement worker (spectrum, attack, spectrum exchange):
/// the body runs with subnormals flushed.
pub(crate) fn spawn_named_measurement_thread<F, T>(
    name: &str,
    body: F,
) -> std::io::Result<JoinHandle<T>>
where
    F: FnOnce() -> T + Send + 'static,
    T: Send + 'static,
{
    thread::Builder::new()
        .name(name.to_string())
        .spawn(move || {
            flush_denormals_on_this_thread();
            body()
        })
}

/// `thread::spawn` for a measurement thread: the body runs with subnormals flushed.
pub(crate) fn spawn_measurement_thread<F, T>(body: F) -> JoinHandle<T>
where
    F: FnOnce() -> T + Send + 'static,
    T: Send + 'static,
{
    thread::spawn(move || {
        flush_denormals_on_this_thread();
        body()
    })
}

#[cfg(test)]
mod tests {
    use super::{spawn_measurement_thread, spawn_named_measurement_thread};
    use crate::channel_layout::ChannelLayout;
    use crate::engine::MeasureEngine;
    use crate::phase_d::channels::PhaseDChannelStream;
    use crate::phase_d::tables::FieldType;
    use crate::resampler::ResamplerTo48k;
    use std::hint::black_box;
    use std::time::{Duration, Instant};

    /// Frames of one 100 ms slot at 48 kHz.
    const SLOT: usize = 4_800;

    /// 100 ms at `rate` of a tone plus broadband noise that excites every band of the Phase D
    /// filter bank; level 0 is digital silence.
    fn slot_at(rate: u32, level: f64, offset: usize) -> Vec<f64> {
        (0..rate as usize / 10)
            .flat_map(|i| {
                let n = (offset + i) as u64;
                let hash = n.wrapping_mul(0x9E37_79B9_7F4A_7C15) >> 11;
                let noise = hash as f64 / (1_u64 << 53) as f64 - 0.5;
                let tone = (std::f64::consts::TAU * 110.0 * n as f64 / f64::from(rate)).sin();
                let x = level * (0.8 * tone + 0.4 * noise);
                [x, x]
            })
            .collect()
    }

    fn slot(level: f64, offset: usize) -> Vec<f64> {
        slot_at(48_000, level, offset)
    }

    /// Every figure a Keep displays or saves (LUFS-M/S, TP, Max TP, CREST, PSR, Phase D N, S, N'
    /// and PSB, then I, LRA and Max TP of the summary) for 3 s of signal and 3 s of silence at
    /// `rate`. As in a Keep, the native audio feeds the EBU engine and its 48 kHz resampling
    /// feeds Phase D.
    fn measured_figures(rate: u32) -> Vec<f64> {
        let mut engine = MeasureEngine::new(rate, ChannelLayout::stereo()).unwrap();
        let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
        let mut resampler = (rate != 48_000).then(|| ResamplerTo48k::new(rate, 2).unwrap());
        let mut phase_input = Vec::new();
        let mut figures = Vec::new();
        let figure = |value: Option<f64>| value.unwrap_or(f64::NAN);
        for index in 0..60 {
            let level = if index < 30 { 0.25 } else { 0.0 };
            let slot = slot_at(rate, level, index * rate as usize / 10);
            match resampler.as_mut() {
                Some(resampler) => resampler.process(&slot, &mut phase_input).unwrap(),
                None => phase_input.extend_from_slice(&slot),
            }
            if index == 59 {
                if let Some(resampler) = resampler.as_mut() {
                    resampler.finish(&mut phase_input).unwrap();
                }
            }
            let whole = phase_input.len() / (2 * SLOT) * 2 * SLOT;
            for phase_slot in phase_input[..whole].chunks_exact(2 * SLOT) {
                if let Some(result) = phase.push_interleaved_slot(phase_slot) {
                    figures.extend([result.loudness, result.sharpness]);
                    figures.extend(result.n_specific);
                    figures.extend(result.psb);
                }
            }
            phase_input.drain(..whole);
            if let Some(m) = engine.push(&slot) {
                figures.extend(
                    [
                        m.lufs_m,
                        m.lufs_s,
                        m.true_peak,
                        m.tp_session_max,
                        m.crest,
                        m.psr,
                    ]
                    .map(figure),
                );
            }
        }
        let summary = engine.finalize();
        figures.extend([summary.lufs_i, summary.lra, summary.max_true_peak].map(figure));
        figures
    }

    /// The flush changes no figure: only values below `f64::MIN_POSITIVE` (2.2e-308) can differ,
    /// so a measurement thread measures what a thread with the default floating-point
    /// environment measures, for the signal and the silence after it.
    #[test]
    fn a_measurement_thread_measures_the_same_figures() {
        for rate in [48_000, 192_000] {
            let flushed = spawn_measurement_thread(move || measured_figures(rate))
                .join()
                .unwrap();
            let default = std::thread::spawn(move || measured_figures(rate))
                .join()
                .unwrap();
            assert!(flushed.len() > 500, "{rate} Hz: every slot was measured");
            assert_eq!(flushed.len(), default.len());
            for (index, (a, b)) in flushed.iter().zip(&default).enumerate() {
                assert!(
                    a == b || (a.is_nan() && b.is_nan()) || (a - b).abs() <= 1e-9,
                    "{rate} Hz figure {index}: {a} on a measurement thread against {b}"
                );
            }
        }
    }

    #[test]
    fn a_measurement_thread_flushes_subnormal_results() {
        let product = spawn_measurement_thread(|| black_box(f64::MIN_POSITIVE) * black_box(0.5))
            .join()
            .unwrap();
        let worker = spawn_named_measurement_thread("kirin-hypha-flush-test", || {
            black_box(f64::MIN_POSITIVE) * black_box(0.5)
        })
        .unwrap()
        .join()
        .unwrap();
        if cfg!(target_arch = "x86_64") {
            assert_eq!((product, worker), (0.0, 0.0));
        }
    }

    /// Phase D measures digital silence after a signal at the signal's cost, not a multiple.
    /// Without the flush an x86 release build measured the silence 5 to 25 times slower from its
    /// first second (11x here). A debug build hides most of it in its own overhead (1.7x).
    #[test]
    #[ignore = "timing: run with --release on an x86_64 host"]
    fn phase_d_measures_silence_after_a_signal_at_the_cost_of_the_signal() {
        let (signal, silence) = spawn_measurement_thread(|| {
            let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
            let mut timed = |slots: usize, level: f64, offset: usize| {
                let mut spent = Duration::ZERO;
                for index in 0..slots {
                    let slot = slot(level, offset + index * SLOT);
                    let start = Instant::now();
                    black_box(phase.push_interleaved_slot(&slot));
                    // Only the second half counts: the states need time to decay.
                    if index >= slots / 2 {
                        spent += start.elapsed();
                    }
                }
                spent
            };
            let signal = timed(30, 0.25, 0);
            let silence = timed(30, 0.0, 30 * SLOT);
            (signal, silence)
        })
        .join()
        .unwrap();
        assert!(
            silence <= signal * 3,
            "silence after a signal costs {silence:?} against {signal:?} for the signal"
        );
    }
}
