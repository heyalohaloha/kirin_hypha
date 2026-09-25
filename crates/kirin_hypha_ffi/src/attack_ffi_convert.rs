use kirin_measure::{
    AttackDetailedEvent, AttackEvent, AttackHistory, AttackOdfFrame, AttackPairEvent,
    AttackPairEventKind, AttackPairViewSnapshot, AttackRuntimeStats, AttackWaveformPoint,
    SpectrumViewStatus,
};

use super::*;

pub(super) fn to_c_attack_frame(frame: &AttackOdfFrame) -> KirinAttackOdfFrame {
    KirinAttackOdfFrame {
        generation: frame.generation,
        sample_rate: frame.sample_rate,
        channels: frame.channels,
        reserved: [0; 3],
        definition_hash: frame.definition_hash,
        window_samples: frame.window_samples,
        hop_samples: frame.hop_samples,
        support_start_samples: frame.support_start_samples,
        support_end_samples: frame.support_end_samples,
        event_sample: frame.event_sample,
        value: frame.value,
    }
}

pub(super) fn to_c_attack_batch(history: &AttackHistory) -> KirinAttackBatch {
    let mut batch = KirinAttackBatch::default();
    let skip = history
        .frames()
        .len()
        .saturating_sub(KIRIN_ATTACK_BATCH_CAPACITY);
    for (destination, source) in batch.frames.iter_mut().zip(history.frames().skip(skip)) {
        *destination = to_c_attack_frame(source);
        batch.count += 1;
    }
    batch
}

pub(super) fn to_c_attack_stats(stats: AttackRuntimeStats) -> KirinAttackStats {
    KirinAttackStats {
        available: 1,
        enabled: stats.enabled as u8,
        worker_running: stats.worker_running as u8,
        channels: stats.channels,
        reserved: [0; 4],
        pushed_blocks: stats.pushed_blocks,
        dropped_blocks: stats.dropped_blocks,
        analyzed_frames: stats.analyzed_frames,
    }
}

pub(super) fn to_c_attack_event(event: &AttackEvent) -> KirinAttackEvent {
    KirinAttackEvent {
        generation: event.generation,
        sample_rate: event.sample_rate,
        channels: event.channels,
        reserved: [0; 3],
        definition_hash: event.definition_hash,
        event_sample: event.event_sample,
        decision_sample: event.decision_sample,
        value: event.value,
    }
}

pub(super) fn to_c_attack_event_batch(history: &AttackHistory) -> KirinAttackEventBatch {
    let mut batch = KirinAttackEventBatch::default();
    let skip = history
        .events()
        .len()
        .saturating_sub(KIRIN_ATTACK_EVENT_BATCH_CAPACITY);
    for (destination, source) in batch.events.iter_mut().zip(history.events().skip(skip)) {
        *destination = to_c_attack_event(source);
        batch.count += 1;
    }
    batch
}

fn to_c_attack_waveform(point: &AttackWaveformPoint) -> KirinAttackWaveformPoint {
    KirinAttackWaveformPoint {
        generation: point.generation,
        sample_rate: point.sample_rate,
        channels: point.channels,
        reserved: [0; 3],
        start_sample: point.start_sample,
        end_sample: point.end_sample,
        peak_linear: point.peak_linear,
        rms_dbfs: point.rms_dbfs,
    }
}

pub(super) fn to_c_attack_waveform_batch(history: AttackHistory) -> KirinAttackWaveformBatch {
    let mut batch = KirinAttackWaveformBatch::default();
    for (destination, source) in batch.points.iter_mut().zip(history.waveform()) {
        *destination = to_c_attack_waveform(source);
        batch.count += 1;
    }
    batch
}

fn to_c_attack_detail(detail: &AttackDetailedEvent) -> KirinAttackDetail {
    let features = detail.features;
    KirinAttackDetail {
        generation: detail.event.generation,
        sample_rate: detail.event.sample_rate,
        channels: detail.event.channels,
        transient_available: features.transient_db.is_some() as u8,
        sharpness_available: features.sharpness_acum.is_some() as u8,
        complete: features.complete as u8,
        definition_hash: detail.event.definition_hash,
        event_sample: detail.event.event_sample,
        decision_sample: detail.event.decision_sample,
        shape_start_sample: detail.shape.start_sample,
        shape_end_sample: detail.shape.end_sample,
        value: detail.event.value,
        transient_db: features.transient_db.unwrap_or(0.0),
        body_rms_dbfs: features.body_rms_dbfs.unwrap_or(0.0),
        attack_rms_dbfs: features.attack_rms_dbfs,
        sample_peak_dbfs: features.sample_peak_dbfs,
        crest_db: features.crest_db,
        body_end_sample: features.body_end_sample,
        sharpness_acum: features.sharpness_acum.unwrap_or(0.0),
        bin_frames: features.bin_frames,
        shape_count: KIRIN_ATTACK_SHAPE_CAPACITY as u32,
        reserved2: 0,
        shape: detail.shape.points,
    }
}

pub(super) fn to_c_attack_detail_batch(history: AttackHistory) -> KirinAttackDetailBatch {
    let mut batch = KirinAttackDetailBatch::default();
    for (destination, source) in batch.details.iter_mut().zip(history.details()) {
        *destination = to_c_attack_detail(source);
        batch.count += 1;
    }
    batch
}

/// POST details while a pair is active: POST measured at every matched PRE onset replaces the
/// POST detector's own detail at that pair's POST onset (reported as the PRE onset), and its own
/// details remain for every other event. The newest details are kept in content-sample order.
pub(super) fn to_c_paired_post_detail_batch(
    own: &[AttackDetailedEvent],
    view: &AttackPairViewSnapshot,
) -> KirinAttackDetailBatch {
    let mut replaced = view
        .pair_events
        .iter()
        .filter(|pair| pair.kind == AttackPairEventKind::Matched)
        .filter_map(|pair| pair.post_event_sample)
        .chain(
            view.post_anchored
                .iter()
                .map(|anchor| anchor.event.event_sample),
        )
        .collect::<Vec<_>>();
    replaced.sort_unstable();
    let mut details = view.post_anchored.clone();
    details.extend(
        own.iter()
            .copied()
            .filter(|own| replaced.binary_search(&own.event.event_sample).is_err()),
    );
    details.sort_by_key(|detail| detail.event.event_sample);
    let skip = details
        .len()
        .saturating_sub(KIRIN_ATTACK_DETAIL_BATCH_CAPACITY);
    let mut batch = KirinAttackDetailBatch::default();
    for (destination, source) in batch.details.iter_mut().zip(details.iter().skip(skip)) {
        *destination = to_c_attack_detail(source);
        batch.count += 1;
    }
    batch
}

fn to_c_attack_pair_event(event: AttackPairEvent) -> KirinAttackPairEvent {
    KirinAttackPairEvent {
        pair_generation: event.pair_generation,
        pre_generation: event.pre_generation,
        post_generation: event.post_generation,
        sample_rate: event.sample_rate,
        channels: event.channels,
        kind: match event.kind {
            AttackPairEventKind::Matched => 0,
            AttackPairEventKind::PreOnly => 1,
            AttackPairEventKind::PostOnly => 2,
            AttackPairEventKind::Ambiguous => 3,
        },
        pre_available: event.pre_event_sample.is_some() as u8,
        post_available: event.post_event_sample.is_some() as u8,
        definition_hash: event.definition_hash,
        event_sample: event.event_sample,
        decision_sample: event.decision_sample,
        pre_event_sample: event.pre_event_sample.unwrap_or(0),
        // A matched POST is measured at the PRE onset (B-1016); its detail is found there.
        post_event_sample: match event.kind {
            AttackPairEventKind::Matched => event.pre_event_sample,
            _ => event.post_event_sample,
        }
        .unwrap_or(0),
        pre_value: event.pre_value.unwrap_or(0.0),
        post_value: event.post_value.unwrap_or(0.0),
        delta_value: event.delta_value.unwrap_or(0.0),
        delta_available: event.delta_value.is_some() as u8,
        reserved: [0; 3],
    }
}

pub(super) fn to_c_attack_pair_event_batch(
    view: AttackPairViewSnapshot,
) -> KirinAttackPairEventBatch {
    let mut batch = KirinAttackPairEventBatch {
        status: match view.status {
            SpectrumViewStatus::Hidden => 0,
            SpectrumViewStatus::NoPair => 1,
            SpectrumViewStatus::WarmingUp => 2,
            SpectrumViewStatus::Active => 3,
            SpectrumViewStatus::Unavailable => 4,
            SpectrumViewStatus::InUse => 5,
        },
        ..Default::default()
    };
    let skip = view
        .pair_events
        .len()
        .saturating_sub(KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY);
    for (destination, source) in batch
        .events
        .iter_mut()
        .zip(view.pair_events.into_iter().skip(skip))
    {
        *destination = to_c_attack_pair_event(source);
        batch.count += 1;
    }
    batch
}
