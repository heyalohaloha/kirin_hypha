//! Qualify a staggered common seek from both source runs' measured clock displacement.
use super::TimeWirePoint;

#[derive(Clone, Copy, PartialEq, Eq)]
struct ClockDisplacement {
    clock: u8,
    frames: i128,
}

fn displacement(before: TimeWirePoint, after: TimeWirePoint) -> Option<ClockDisplacement> {
    let old = before.exact_key()?;
    let new = after.exact_key()?;
    if before.span != after.span || before.run == after.run || old.0 != new.0 {
        return None;
    }
    let advance = after.observed.checked_sub(before.observed)?;
    let frames = i128::from(new.1) - i128::from(old.1) - i128::from(advance);
    // A run number alone cannot establish a seek. Worker/source replacements and a zero
    // displacement keep the one-sided retirement rule.
    (frames != 0).then_some(ClockDisplacement {
        clock: new.0,
        frames,
    })
}

struct PendingSeek {
    changed_pre: bool,
    changed: TimeWirePoint,
    opposite: TimeWirePoint,
    displacement: ClockDisplacement,
}

#[derive(Default)]
pub(super) struct SeekAdmission {
    pre: Option<TimeWirePoint>,
    post: Option<TimeWirePoint>,
    pending: Option<PendingSeek>,
}

impl SeekAdmission {
    pub(super) fn clear_pending(&mut self) {
        self.pending = None;
    }

    pub(super) fn observe(&mut self, pre: TimeWirePoint, post: TimeWirePoint) -> bool {
        let old = self.pre.zip(self.post);
        // A 100 ms slot overlapping the seek can be unusable. Keep its same-span last exact
        // baseline so the first complete new-run slot can still prove the clock displacement.
        // A source replacement clears that baseline immediately, even if its first slot is mixed.
        let eligible = |previous: Option<TimeWirePoint>, point: TimeWirePoint| {
            if point.exact_key().is_some() {
                Some(point)
            } else {
                previous.filter(|baseline| baseline.span == point.span)
            }
        };
        self.pre = eligible(self.pre, pre);
        self.post = eligible(self.post, post);
        let Some(((old_pre, old_post), (pre, post))) = old.zip(self.pre.zip(self.post)) else {
            self.pending = None;
            return false;
        };
        if old_pre.span != pre.span || old_post.span != post.span {
            self.pending = None;
            return false;
        }
        let pre_changed = old_pre.run != pre.run;
        let post_changed = old_post.run != post.run;
        if pre_changed && post_changed {
            self.pending = None;
            return true;
        }
        if let Some(pending) = self.pending.as_ref() {
            let (changed, opposite, old_opposite, opposite_changed) = if pending.changed_pre {
                (pre, post, old_post, post_changed)
            } else {
                (post, pre, old_pre, pre_changed)
            };
            // Stay within one bounded publication tail and the originally changed run. This
            // cannot turn a later unrelated seek or a replaced worker into the pending seek.
            let step = ((u64::from(changed.span.sample_rate) + 5) / 10).max(1);
            let still_pending = changed.span == pending.changed.span
                && changed.run == pending.changed.run
                && changed
                    .observed
                    .checked_sub(pending.changed.observed)
                    .is_some_and(|n| n <= step * super::METER_HISTORY_EXCHANGE_POINTS as u64)
                && old_opposite.span == pending.opposite.span
                && old_opposite.run == pending.opposite.run;
            if still_pending && opposite_changed {
                let same_seek =
                    displacement(pending.opposite, opposite) == Some(pending.displacement);
                self.pending = None;
                return same_seek;
            }
            if !still_pending {
                self.pending = None;
            }
        }
        if pre_changed ^ post_changed {
            let (changed, opposite, before) = if pre_changed {
                (pre, post, old_pre)
            } else {
                (post, pre, old_post)
            };
            self.pending = displacement(before, changed).map(|displacement| PendingSeek {
                changed_pre: pre_changed,
                changed,
                opposite,
                displacement,
            });
        }
        false
    }
}
