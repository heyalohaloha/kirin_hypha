//! The live engine behind the Watch and Keep displays measures one continuous 48 kHz stream at
//! every native rate. A TRACE pass end restarts only Phase D's own resampler: the live true peak of
//! a steady tone stays where it was, while restarting the live stream itself would move it.
use super::*;
use crate::channel_layout::ChannelLayout;
use crate::engine::MeasureEngine;
use crate::phase_d::tables::FieldType;

const RATE: u32 = 96_000;
/// 25 ms of audio per Measure Thread chunk.
const CHUNK: usize = 2_400;
const CHUNKS: usize = 160;

fn tone(index: usize) -> Vec<f64> {
    (index * CHUNK..(index + 1) * CHUNK)
        .flat_map(|frame| {
            let x = 0.3 * (std::f64::consts::TAU * 997.0 * frame as f64 / f64::from(RATE)).sin();
            [x, x]
        })
        .collect()
}

/// The live engine's (LUFS-M, true peak) per 100 ms, and Phase D's slots before the pass end, for
/// a TRACE pass that ends before chunk `pass_end`. `shared` feeds the live engine from Phase D's
/// resampler, as one resampler for both did, so the pass end restarts the live stream too.
fn run(pass_end: Option<usize>, shared: bool) -> (Vec<(f64, f64)>, usize) {
    let mut live = Some(ResamplerTo48k::new(RATE, 2).unwrap());
    let mut trace = TraceResampler::new(RATE, 2).unwrap();
    assert!(
        trace.resampler.is_some(),
        "Phase D resamples on its own at 96 kHz"
    );
    let mut engine = MeasureEngine::new(ENGINE_SR, ChannelLayout::stereo()).unwrap();
    let mut phase_d = PhaseDChannelStream::new(FieldType::Free, 2);
    let (mut live_out, mut slot_pending) = (Vec::new(), Vec::new());
    let (mut latest_pd, mut phase_pending, mut core_pending) =
        (None, VecDeque::new(), VecDeque::new());
    let (mut readings, mut slots) = (Vec::new(), 0);
    for index in 0..CHUNKS {
        let chunk = tone(index);
        let take = pass_end.is_none_or(|end| index < end);
        if Some(index) == pass_end {
            slots += phase_pending.len();
            close_trace_pass(
                &mut trace,
                2,
                &mut slot_pending,
                &mut phase_d,
                &mut latest_pd,
                &mut phase_pending,
                &mut core_pending,
                None,
                RATE,
            );
        }
        live_out.clear();
        if shared {
            live_out.extend_from_slice(trace.process(&chunk).unwrap());
        } else {
            live.as_mut()
                .unwrap()
                .process(&chunk, &mut live_out)
                .unwrap();
        }
        if take {
            let audio = if shared {
                live_out.as_slice()
            } else {
                trace_48k(&mut trace, &chunk)
            };
            feed_phase_d_slots(
                audio,
                2,
                &mut slot_pending,
                &mut phase_d,
                &mut latest_pd,
                &mut phase_pending,
                true,
            );
        }
        let _ = engine.push_observed(&live_out, |_, result, _| {
            readings.push((result.lufs_m.unwrap(), result.true_peak.unwrap()));
        });
    }
    (readings, slots)
}

#[test]
fn a_trace_pass_end_leaves_the_live_stream_continuous() {
    let (continuous, _) = run(None, false);
    // Where the pass ends against the resampler's 1024-frame blocks decides how far a restart of
    // the live stream moves its true peak; each of these moves it.
    for pass_end in [81, 83] {
        let (separate, slots) = run(Some(pass_end), false);
        assert_eq!(
            separate, continuous,
            "pass end at chunk {pass_end}: the live engine reads the same stream"
        );
        // Phase D measures the pass through its own resampler: every whole 100 ms of it but the
        // last, which its resampler still holds until the pass end flushes it.
        let whole = pass_end * CHUNK * 10 / RATE as usize;
        assert!(
            (whole - 1..=whole).contains(&slots),
            "{slots} of {whole} slots"
        );
        let (cut, _) = run(Some(pass_end), true);
        let moved = continuous
            .iter()
            .zip(&cut)
            .map(|(a, b)| (b.1 - a.1).abs())
            .fold(0.0, f64::max);
        assert!(
            moved > 0.5,
            "pass end at chunk {pass_end}: a restart of the live stream moves its true peak ({moved:.3} dB)"
        );
    }
}
