//! Bounded, single-Audio-writer edge history, indexed by the capture-frame clock.
use std::marker::PhantomData;
use std::sync::atomic::{AtomicU64, Ordering};

pub const AUDIO_EDGE_JOURNAL_CAPACITY: usize = 256;

/// Values cross the thread boundary as one atomic word.
pub trait AudioEdgeValue: Copy {
    fn into_word(self) -> u64;
    fn from_word(word: u64) -> Self;
}

impl AudioEdgeValue for i64 {
    fn into_word(self) -> u64 {
        self as u64
    }
    fn from_word(word: u64) -> Self {
        word as i64
    }
}
impl AudioEdgeValue for bool {
    fn into_word(self) -> u64 {
        u64::from(self)
    }
    fn from_word(word: u64) -> Self {
        word != 0
    }
}

#[derive(Debug)]
struct EdgeSlot {
    version: AtomicU64,
    captured_frames: AtomicU64,
    value: AtomicU64,
}
impl EdgeSlot {
    const fn new() -> Self {
        Self {
            version: AtomicU64::new(0),
            captured_frames: AtomicU64::new(0),
            value: AtomicU64::new(0),
        }
    }
}

/// `note` and `invalidate` have exactly one writer: the Audio Thread. Reads are bounded and
/// fail closed on a concurrent overwrite or reset. Construction is outside the Audio Thread;
/// publication and invalidation use only atomic operations on inline, preallocated slots.
#[derive(Debug)]
pub struct AudioEdgeJournal<T: AudioEdgeValue> {
    slots: [EdgeSlot; AUDIO_EDGE_JOURNAL_CAPACITY],
    published: AtomicU64,
    reset_version: AtomicU64,
    value: PhantomData<T>,
}

pub type TransportStartJournal = AudioEdgeJournal<i64>;
pub type ActiveEdgeJournal = AudioEdgeJournal<bool>;

impl<T: AudioEdgeValue> Default for AudioEdgeJournal<T> {
    fn default() -> Self {
        Self::new()
    }
}
impl<T: AudioEdgeValue> AudioEdgeJournal<T> {
    pub const fn new() -> Self {
        Self {
            slots: [const { EdgeSlot::new() }; AUDIO_EDGE_JOURNAL_CAPACITY],
            published: AtomicU64::new(0),
            reset_version: AtomicU64::new(0),
            value: PhantomData,
        }
    }

    pub fn note(&self, captured_frames: u64, value: T) {
        let sequence = self.published.load(Ordering::Relaxed);
        // Never reuse a version after exhaustion; even at an edge per sample this is centuries.
        if sequence >= u64::MAX / 2 {
            return;
        }
        let slot = &self.slots[sequence as usize % AUDIO_EDGE_JOURNAL_CAPACITY];
        slot.version.store(sequence * 2 + 1, Ordering::SeqCst);
        slot.captured_frames
            .store(captured_frames, Ordering::SeqCst);
        slot.value.store(value.into_word(), Ordering::SeqCst);
        slot.version.store(sequence * 2 + 2, Ordering::SeqCst);
        self.published.store(sequence + 1, Ordering::Release);
    }

    pub fn latest_at_or_before(&self, captured_frame: u64) -> Option<T> {
        let reset = self.reset_version.load(Ordering::SeqCst);
        if reset & 1 != 0 {
            return None;
        }
        let end = self.published.load(Ordering::Acquire);
        let mut found = None;
        for sequence in (end.saturating_sub(AUDIO_EDGE_JOURNAL_CAPACITY as u64)..end).rev() {
            let slot = &self.slots[sequence as usize % AUDIO_EDGE_JOURNAL_CAPACITY];
            let expected = sequence * 2 + 2;
            if slot.version.load(Ordering::SeqCst) != expected {
                return None;
            }
            let key = slot.captured_frames.load(Ordering::SeqCst);
            let value = slot.value.load(Ordering::SeqCst);
            if slot.version.load(Ordering::SeqCst) != expected {
                return None;
            }
            if key <= captured_frame {
                found = Some(T::from_word(value));
                break;
            }
        }
        (self.reset_version.load(Ordering::SeqCst) == reset)
            .then_some(found)
            .flatten()
    }

    pub fn invalidate(&self) {
        self.reset_version.fetch_add(1, Ordering::SeqCst);
        for slot in &self.slots {
            slot.version.store(0, Ordering::SeqCst);
        }
        // Keep publication sequence monotonic to prevent a reset/read ABA.
        self.reset_version.fetch_add(1, Ordering::SeqCst);
    }
}

#[cfg(test)]
#[path = "audio_edge_journal_tests.rs"]
mod tests;
