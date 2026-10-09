//! Fixed-capacity producer evidence for Record audio, independent of auxiliary pair epochs.
use super::*;

#[derive(Debug)]
pub(in crate::record_take) struct RecordAudioMapping {
    slots: [CaptureClockSlot; 2],
    current: AtomicU64,
    selected_epoch: AtomicU64,
    last_extended_epochs: [AtomicU64; 2],
}

impl RecordAudioMapping {
    pub(in crate::record_take) fn new() -> Self {
        Self {
            slots: [CaptureClockSlot::new(), CaptureClockSlot::new()],
            current: AtomicU64::new(0),
            selected_epoch: AtomicU64::new(0),
            last_extended_epochs: [AtomicU64::new(0), AtomicU64::new(0)],
        }
    }

    // One audio producer. Two slots retain the current and immediately previous selected take.
    // A discontinuity freezes the prefix; later callbacks cannot extend or revive that prefix.
    pub(in crate::record_take) fn note(&self, selected: u64, span: CaptureClockSpan) {
        if selected == 0 || span.generation == 0 {
            return;
        }
        let mut index = self.current.load(Ordering::Acquire) as usize;
        if self.selected_epoch.load(Ordering::Acquire) != selected {
            if span.epoch != selected {
                return; // Never synthesize a start whose producer span has already retired.
            }
            index ^= 1;
            self.last_extended_epochs[index].store(span.epoch, Ordering::Release);
            self.slots[index].publish(span);
            self.current.store(index as u64, Ordering::Release);
            self.selected_epoch.store(selected, Ordering::Release);
            return;
        }
        let Some(prefix) = self.slots[index].read(selected) else {
            return;
        };
        let last = self.last_extended_epochs[index].load(Ordering::Acquire);
        let same_epoch = last == span.epoch;
        let qualified_cut = last.checked_add(1) == Some(span.epoch)
            && span.auxiliary_only_cut
            && prefix.capture_end_frame == span.capture_start_frame;
        let Some(offset) = span
            .capture_start_frame
            .checked_sub(prefix.capture_start_frame)
            .and_then(|frames| i64::try_from(frames).ok())
        else {
            return;
        };
        let same_axis = prefix
            .raw_host_position_start_samples
            .and_then(|start| start.checked_add(offset))
            == span.raw_host_position_start_samples
            && prefix
                .position_start_samples
                .and_then(|start| start.checked_add(offset))
                == span.position_start_samples;
        if (same_epoch || qualified_cut)
            && same_axis
            && prefix.generation == span.generation
            && prefix.source == span.source
            && prefix.presentation_latency == span.presentation_latency
            && span.capture_start_frame <= prefix.capture_end_frame
            && span.capture_end_frame >= prefix.capture_end_frame
        {
            self.last_extended_epochs[index].store(span.epoch, Ordering::Release);
            self.slots[index].extend(selected, span.capture_end_frame);
        }
    }

    pub(in crate::record_take) fn read(&self, epoch: u64) -> Option<CaptureClockSpan> {
        self.slots.iter().find_map(|slot| slot.read(epoch))
    }

    pub(in crate::record_take) fn read_proof(
        &self,
        epoch: u64,
    ) -> Option<crate::capture_clock::RecordAudioPrefixProof> {
        for (index, slot) in self.slots.iter().enumerate() {
            let last = self.last_extended_epochs[index].load(Ordering::Acquire);
            let Some(span) = slot.read(epoch) else {
                continue;
            };
            if last >= epoch
                && last == self.last_extended_epochs[index].load(Ordering::Acquire)
                && slot.read(epoch).is_some()
            {
                return Some(crate::capture_clock::RecordAudioPrefixProof {
                    span,
                    last_epoch: last,
                });
            }
        }
        None
    }
}
