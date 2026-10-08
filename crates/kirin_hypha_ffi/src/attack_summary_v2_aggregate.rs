//! Select all public event keys first, then aggregate their scalar and point evidence.
use super::*;

#[derive(Clone, Debug)]
pub(crate) struct SummaryEnvelope {
    pub head: [f64; KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD],
    pub tail: [f64; KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL],
    pub head_valid: [u8; KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD],
    pub tail_valid: [u8; KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL],
}

impl SummaryEnvelope {
    fn is_valid(&self) -> bool {
        self.head
            .iter()
            .chain(self.tail.iter())
            .all(|value| value.is_finite())
            && self
                .head_valid
                .iter()
                .chain(self.tail_valid.iter())
                .all(|mask| *mask <= 1)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct SummaryEvent {
    pub key: KirinSnapshotEventKey,
    pub kind: u8,
    pub lanes: [KirinSnapshotScalarEvidence; 4],
    pub pre: Option<SummaryEnvelope>,
    pub post: Option<SummaryEnvelope>,
    /// Per-point verified mapping support, separate from measurement masks.
    pub proof_head: [u8; KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD],
    pub proof_tail: [u8; KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL],
}

pub(crate) fn fixed_cohort<T>(
    events: &[T],
    cutoff: i64,
    rate: u32,
    sample: impl Fn(&T) -> i64,
) -> Vec<&T> {
    let start = cutoff.saturating_sub(i64::from(rate) * 6);
    let mut eligible: Vec<_> = events
        .iter()
        .filter(|e| {
            let at = sample(e);
            at >= start && at <= cutoff
        })
        .collect();
    eligible.sort_by_key(|e| sample(e));
    if eligible.len() > KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY {
        eligible.drain(..eligible.len() - KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY);
    }
    eligible
}

pub(crate) fn assemble_summary(
    header: KirinSnapshotHeader,
    events: &[SummaryEvent],
) -> Option<KirinAttackBandSummaryV2> {
    if events.len() > KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY
        || !header.source.is_valid()
        || header.kind != KIRIN_SNAPSHOT_BAND_SUMMARY
        || header.target > KIRIN_TARGET_DELTA
        || header.version != KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION
        || header.struct_size as usize != std::mem::size_of::<KirinAttackBandSummaryV2>()
        || !(1..=8).contains(&header.band)
        || header.signal_state > 2
    {
        return None;
    }
    let window_start = header
        .cutoff_sample
        .saturating_sub(i64::from(header.source.sample_rate) * 6);
    for (i, e) in events.iter().enumerate() {
        if !e.key.source.is_valid()
            || e.kind > 4
            || e.key.source.sample_rate != header.source.sample_rate
            || e.key.source.channels != header.source.channels
            || e.key.source.odf_hash != header.source.odf_hash
            || e.key.event_sample < window_start
            || e.key.event_sample > header.cutoff_sample
            || (i > 0 && events[i - 1].key.event_sample > e.key.event_sample)
            || events[..i].iter().any(|other| other.key == e.key)
            || e.pre.as_ref().is_some_and(|env| !env.is_valid())
            || e.post.as_ref().is_some_and(|env| !env.is_valid())
            || e.proof_head
                .iter()
                .chain(e.proof_tail.iter())
                .any(|mask| *mask > 1)
        {
            return None;
        }
    }
    let mut result = KirinAttackBandSummaryV2 {
        header,
        cohort_count: events.len() as u8,
        ..Default::default()
    };
    for (i, event) in events.iter().enumerate() {
        result.events[i] = event.key;
        result.pair_kind[i] = event.kind;
        result.evidence[i] = event.lanes;
    }
    for lane in 0..4 {
        result.lanes[lane] = summarize_lane(events, lane, header.target)?;
    }
    result.head = average_points(events, header.target, true)?;
    result.tail = average_points(events, header.target, false)?;
    Some(result)
}

fn average_points<const N: usize>(
    events: &[SummaryEvent],
    target: u8,
    head: bool,
) -> Option<[KirinAttackBandAveragePointV2; N]> {
    let mut result = [KirinAttackBandAveragePointV2::default(); N];
    for point in 0..N {
        let mut pre = Vec::with_capacity(events.len());
        let mut post = Vec::with_capacity(events.len());
        for (i, event) in events.iter().enumerate() {
            let reading = |env: &SummaryEnvelope| {
                if head {
                    (env.head[point], env.head_valid[point])
                } else {
                    (env.tail[point], env.tail_valid[point])
                }
            };
            let Some(post_env) = event.post.as_ref() else {
                continue;
            };
            let (post_value, post_mask) = reading(post_env);
            if post_mask > 1 {
                return None;
            }
            if post_mask == 0 {
                continue;
            }
            if !post_value.is_finite() {
                return None;
            }
            if target == KIRIN_TARGET_DELTA {
                let proof = if head {
                    event.proof_head[point]
                } else {
                    event.proof_tail[point]
                };
                if proof > 1 {
                    return None;
                }
                if proof == 0 {
                    continue;
                }
                let Some(pre_env) = event.pre.as_ref() else {
                    continue;
                };
                let (pre_value, pre_mask) = reading(pre_env);
                if pre_mask > 1 {
                    return None;
                }
                if pre_mask == 0 {
                    continue;
                }
                if !pre_value.is_finite() {
                    return None;
                }
                pre.push(pre_value);
            }
            post.push(post_value);
            result[point].participating_bits |= 1 << i;
        }
        let previous_bits = if point > 0 {
            result[point - 1].participating_bits
        } else {
            0
        };
        let item = &mut result[point];
        item.valid_count = item.participating_bits.count_ones() as u8;
        if item.valid_count == 0 {
            continue;
        }
        fn stats(values: &[f64]) -> (f64, f64, f64) {
            (
                values.iter().map(|v| v / values.len() as f64).sum(),
                values.iter().copied().fold(f64::INFINITY, f64::min),
                values.iter().copied().fold(f64::NEG_INFINITY, f64::max),
            )
        }
        (item.post_mean, item.post_min, item.post_max) = stats(&post);
        if target == KIRIN_TARGET_DELTA {
            item.has_pre = 1;
            (item.pre_mean, item.pre_min, item.pre_max) = stats(&pre);
        }
        if point > 0 && previous_bits == item.participating_bits {
            item.connect_previous = 1;
        }
    }
    Some(result)
}
