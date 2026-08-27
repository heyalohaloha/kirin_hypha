//! Bounded Audio -> Measure ingest contract.
//!
//! A non-blocking Audio Thread cannot promise lossless delivery for an unbounded number of
//! callbacks with finite memory. Hypha therefore publishes one explicit, testable host contract:
//! every supported instance accepts three complete maximum-sized callbacks before the Measure
//! Thread has to reclaim a slot. Watch and Record use different preallocated lanes so ordinary
//! metering no longer pays for a 30-second Record backlog.

use crate::capture_contract::MAX_CAPTURE_PAIRS;

/// Largest callback accepted by both JUCE and Rust producers.
pub const MAX_AUDIO_BLOCK_FRAMES: usize = 262_144;

/// Maximum number of complete Record callbacks that may remain unconsumed.
pub const RECORD_UNCONSUMED_BURST_BLOCKS: usize = 3;

/// Largest supported input layout (mono and stereo are accepted).
pub const MAX_AUDIO_CHANNELS: usize = 2;

/// Maximum supported native sample rate. The raw pre-roll allocation is part of the 12-pair RSS
/// contract, so accepting an unbounded sample rate would make that contract fictitious.
pub const MAX_SUPPORTED_SAMPLE_RATE: u32 = 192_000;

/// In addition to one complete maximum host callback, retain one second of producer-clocked raw
/// audio. This covers the bounded Keep notification/ACK handoff without deriving Record data from
/// Watch metrics. The history remains finite and independent of bounce duration.
pub const PRE_ROLL_NOTIFICATION_WINDOW_SECONDS: usize = 1;

/// One Watch lane only has to retain one complete host callback.
pub const fn watch_ring_capacity_samples(n_channels: usize) -> usize {
    MAX_AUDIO_BLOCK_FRAMES.saturating_mul(n_channels)
}

/// A Record lane retains the complete advertised unconsumed callback burst.
pub const fn record_ring_capacity_samples(n_channels: usize) -> usize {
    MAX_AUDIO_BLOCK_FRAMES
        .saturating_mul(RECORD_UNCONSUMED_BURST_BLOCKS)
        .saturating_mul(n_channels)
}

/// JUCE's planar-to-interleaved scratch is one maximum callback per instance.
pub const fn interleave_scratch_capacity_samples(n_channels: usize) -> usize {
    MAX_AUDIO_BLOCK_FRAMES.saturating_mul(n_channels)
}

pub const fn raw_pre_roll_capacity_frames(sample_rate: u32) -> usize {
    let bounded_sample_rate = if sample_rate > MAX_SUPPORTED_SAMPLE_RATE {
        MAX_SUPPORTED_SAMPLE_RATE
    } else {
        sample_rate
    };
    MAX_AUDIO_BLOCK_FRAMES.saturating_add(
        (bounded_sample_rate as usize).saturating_mul(PRE_ROLL_NOTIFICATION_WINDOW_SECONDS),
    )
}

pub const fn raw_pre_roll_capacity_samples(sample_rate: u32, n_channels: usize) -> usize {
    raw_pre_roll_capacity_frames(sample_rate).saturating_mul(n_channels)
}

/// Measure consumes the producer in 100 ms native-rate chunks. Capping the conversion buffer
/// prevents an offline-bounce backlog from turning into an unbounded `Vec<f64>` growth spike.
pub const fn measure_chunk_capacity_samples(sample_rate: u32, n_channels: usize) -> usize {
    ((sample_rate as usize).saturating_add(9) / 10).saturating_mul(n_channels)
}

/// Maximum Audio-ingest + JUCE scratch allocation for all 12 PRE/POST pairs.
pub const fn max_generation_ingest_bytes() -> usize {
    let instances = MAX_CAPTURE_PAIRS.saturating_mul(2);
    let per_instance_samples = watch_ring_capacity_samples(MAX_AUDIO_CHANNELS)
        .saturating_add(record_ring_capacity_samples(MAX_AUDIO_CHANNELS))
        .saturating_add(interleave_scratch_capacity_samples(MAX_AUDIO_CHANNELS))
        .saturating_add(raw_pre_roll_capacity_samples(
            MAX_SUPPORTED_SAMPLE_RATE,
            MAX_AUDIO_CHANNELS,
        ));
    instances
        .saturating_mul(per_instance_samples)
        .saturating_mul(std::mem::size_of::<f32>())
}

/// Known per-generation ingest, non-audio spool copy/read buffers, and the bounded f32→f64
/// Measure conversion workspace at the maximum supported sample rate. Spool file contents live on
/// disk, not RSS. DSP engine internals and control state use the remaining reserve; neither is
/// sized by bounce duration or callback count.
pub const fn max_generation_known_pipeline_bytes() -> usize {
    let instances = MAX_CAPTURE_PAIRS.saturating_mul(2);
    max_generation_ingest_bytes()
        .saturating_add(instances.saturating_mul(crate::record_spool::memory_bytes_per_instance()))
        .saturating_add(
            instances
                .saturating_mul(measure_chunk_capacity_samples(192_000, MAX_AUDIO_CHANNELS))
                .saturating_mul(std::mem::size_of::<f64>()),
        )
}

/// Hard RSS allocation envelope reserved for ingest, engines and bounded control state.
pub const CAPTURE_GENERATION_RSS_BUDGET_BYTES: usize = 384 * 1024 * 1024;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn contract_accepts_three_complete_maximum_blocks() {
        assert_eq!(RECORD_UNCONSUMED_BURST_BLOCKS, 3);
        assert_eq!(
            record_ring_capacity_samples(2),
            MAX_AUDIO_BLOCK_FRAMES * 3 * 2
        );
    }

    #[test]
    fn twelve_pairs_stay_below_the_process_rss_budget() {
        assert_eq!(max_generation_ingest_bytes(), 338_853_888);
        assert_eq!(max_generation_known_pipeline_bytes(), 349_372_416);
        assert!(max_generation_known_pipeline_bytes() <= CAPTURE_GENERATION_RSS_BUDGET_BYTES);
    }

    #[test]
    fn capacities_are_sample_rate_independent_for_48_96_and_192k() {
        for sample_rate in [48_000_u32, 96_000, 192_000] {
            let _ = sample_rate;
            assert_eq!(watch_ring_capacity_samples(2), 524_288);
            assert_eq!(record_ring_capacity_samples(2), 1_572_864);
            assert_eq!(
                raw_pre_roll_capacity_samples(sample_rate, 2),
                (MAX_AUDIO_BLOCK_FRAMES + sample_rate as usize) * 2
            );
            assert_eq!(
                measure_chunk_capacity_samples(sample_rate, 2),
                sample_rate as usize / 5
            );
        }
    }

    #[test]
    fn pre_roll_allocation_is_hard_bounded_above_supported_sample_rate() {
        assert_eq!(
            raw_pre_roll_capacity_samples(384_000, 2),
            raw_pre_roll_capacity_samples(MAX_SUPPORTED_SAMPLE_RATE, 2)
        );
    }
}
