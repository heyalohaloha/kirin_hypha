//! Lock-free ownership handoff for Measure Thread ring Producer replacement.
//!
//! The Audio Thread only moves three atomic pointers. Allocation and reclamation are owned by
//! the Watchdog Thread, so a Measure restart cannot introduce a lock or allocator call into the
//! audio callback.

use std::ptr;
use std::sync::atomic::{AtomicPtr, Ordering};

/// Measure restart 時の ring Producer 所有権を Audio Thread と Watchdog Thread の間で
/// lock-free に受け渡す。
///
/// `active` / `pending` / `retired` はすべて control/watchdog 側で確保した `Box` への
/// pointer である。Audio Thread は pointer を移動するだけで、`Box::new` / `drop` を
/// 実行しない。通常時の Audio Thread cost は `pending.load()` 1 回だけ。
///
/// # Ownership contract
///
/// - `publish_from_watchdog` / `reclaim_retired_from_watchdog`: Watchdog Thread のみ。
/// - `swap_pending_from_audio` / `with_active_producer_from_audio`: 単一 Audio Thread のみ。
/// - 破棄時は Audio callback が停止し、Watchdog が終了した後であること。
pub struct WatchProducerHandoff {
    active: AtomicPtr<rtrb::Producer<f32>>,
    pending: AtomicPtr<rtrb::Producer<f32>>,
    retired: AtomicPtr<rtrb::Producer<f32>>,
}

impl WatchProducerHandoff {
    /// 初代 Producer を control thread で確保する。
    pub fn new(active: rtrb::Producer<f32>) -> Self {
        Self {
            active: AtomicPtr::new(Box::into_raw(Box::new(active))),
            pending: AtomicPtr::new(ptr::null_mut()),
            retired: AtomicPtr::new(ptr::null_mut()),
        }
    }

    /// 新しい Measure 世代の Producer を publish する。
    ///
    /// Audio callback がまだ取り込んでいない旧 pending があれば、それは既に
    /// 終了した Measure 世代の Producer なので Watchdog 側で破棄し、常に
    /// 最新世代だけを残す。
    pub fn publish_from_watchdog(&self, producer: rtrb::Producer<f32>) {
        self.reclaim_retired_from_watchdog();
        let next = Box::into_raw(Box::new(producer));
        let stale = self.pending.swap(next, Ordering::AcqRel);
        if !stale.is_null() {
            // SAFETY: pending.swap() で所有権を排他的に回収した。Watchdog Thread 上の
            // deallocation であり Audio Thread に allocator を持ち込まない。
            unsafe { drop(Box::from_raw(stale)) };
        }
    }

    /// Audio Thread が最新 pending を active に入れ替え、旧 active を Watchdog 側の
    /// reclaim slot へ返す。allocation / deallocation / lock / I/O は行わない。
    ///
    /// # Safety
    ///
    /// 同時に呼び出すのは単一 Audio Thread だけでなければならない。呼び出し後、
    /// その callback は旧 active Producer へアクセスしてはならない。
    #[inline]
    pub unsafe fn swap_pending_from_audio(&self) -> bool {
        // 通常経路はこの 1 load で終了。
        if self.pending.load(Ordering::Acquire).is_null() {
            return false;
        }
        // 前世代を Watchdog がまだ reclaim していなければ、pending をそのまま
        // 保持し次 callback に延期する。Audio Thread で old Producer を drop しない。
        if !self.retired.load(Ordering::Acquire).is_null() {
            return false;
        }

        let next = self.pending.swap(ptr::null_mut(), Ordering::AcqRel);
        if next.is_null() {
            return false;
        }
        let old = self.active.swap(next, Ordering::AcqRel);
        // SAFETY contract により retired へ書くのは単一 Audio Thread のみ。Watchdog は
        // null へ回収することしかないため、直前の null 確認後に上書きは起きない。
        self.retired.store(old, Ordering::Release);
        true
    }

    /// 現 active Producer を Audio Thread 専用 closure の実行中だけ借用する。
    ///
    /// # Safety
    ///
    /// `swap_pending_from_audio` と同じ単一 Audio Thread 契約の下で呼ぶこと。
    /// mutable reference は closure 外へ返せない。
    #[inline]
    pub unsafe fn with_active_producer_from_audio<R>(
        &self,
        use_producer: impl FnOnce(&mut rtrb::Producer<f32>) -> R,
    ) -> R {
        // SAFETY: active は new() で常に non-null となり、破棄まで active 自体は
        // Watchdog から dereference されない。排他は呼び出し側の単一 Audio Thread 契約。
        use_producer(unsafe { &mut *self.active.load(Ordering::Acquire) })
    }

    /// 現 active Producer の空きスロット数を Audio/test Thread から読む。
    ///
    /// # Safety
    ///
    /// 単一 Audio/test Thread が `with_active_producer_from_audio` と直列に呼ぶこと。
    #[inline]
    pub unsafe fn active_slots_from_audio(&self) -> usize {
        // SAFETY: 上記の単一 Audio/test Thread 契約。slots() は read-only。
        unsafe { &*self.active.load(Ordering::Acquire) }.slots()
    }

    /// Audio Thread が返した旧 Producer を Watchdog Thread で破棄する。
    pub fn reclaim_retired_from_watchdog(&self) {
        let retired = self.retired.swap(ptr::null_mut(), Ordering::AcqRel);
        if !retired.is_null() {
            // SAFETY: retired.swap() で所有権を排他的に回収した。
            unsafe { drop(Box::from_raw(retired)) };
        }
    }

    /// shutdown 時に未取込の pending を Watchdog Thread で破棄する。
    pub(crate) fn discard_pending_from_watchdog(&self) {
        let pending = self.pending.swap(ptr::null_mut(), Ordering::AcqRel);
        if !pending.is_null() {
            // SAFETY: pending.swap() で所有権を排他的に回収した。
            unsafe { drop(Box::from_raw(pending)) };
        }
    }
}

impl Drop for WatchProducerHandoff {
    fn drop(&mut self) {
        // 最後の Arc は plugin/control か Watchdog Thread で破棄される。Audio callback と
        // 同時に Drop しないことは型の ownership contract で保証する。
        for pointer in [
            *self.active.get_mut(),
            *self.pending.get_mut(),
            *self.retired.get_mut(),
        ] {
            if !pointer.is_null() {
                // SAFETY: &mut self により concurrent access はなく、各 pointer は別の Box を所有。
                unsafe { drop(Box::from_raw(pointer)) };
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn handoff_is_shareable_between_audio_and_watchdog_threads() {
        assert_send_sync::<WatchProducerHandoff>();
    }

    #[test]
    fn latest_unconsumed_generation_wins_and_old_active_is_reclaimed_off_audio() {
        let (initial_producer, initial_consumer) = rtrb::RingBuffer::new(8);
        let handoff = WatchProducerHandoff::new(initial_producer);
        let (generation_one, generation_one_consumer) = rtrb::RingBuffer::new(8);
        let (generation_two, mut generation_two_consumer) = rtrb::RingBuffer::new(8);

        handoff.publish_from_watchdog(generation_one);
        handoff.publish_from_watchdog(generation_two);
        assert!(
            generation_one_consumer.is_abandoned(),
            "an unconsumed stale generation must be reclaimed by the watchdog"
        );

        // SAFETY: this test is the sole Audio Thread owner of the handoff.
        assert!(unsafe { handoff.swap_pending_from_audio() });
        // SAFETY: same single-thread contract; reference does not cross this statement.
        unsafe { handoff.with_active_producer_from_audio(|producer| producer.push(0.25).unwrap()) };
        assert_eq!(generation_two_consumer.pop().unwrap(), 0.25);

        handoff.reclaim_retired_from_watchdog();
        assert!(
            initial_consumer.is_abandoned(),
            "the watchdog, not the audio thread, reclaims the old active producer"
        );
    }

    #[test]
    fn repeated_restart_cycles_keep_the_newest_ring_live() {
        let (initial, _initial_consumer) = rtrb::RingBuffer::new(4);
        let handoff = WatchProducerHandoff::new(initial);

        for expected in 1..=3 {
            let (next, mut consumer) = rtrb::RingBuffer::new(4);
            handoff.publish_from_watchdog(next);
            // SAFETY: this test is the sole Audio Thread owner of the handoff.
            assert!(unsafe { handoff.swap_pending_from_audio() });
            // SAFETY: same single-thread contract; reference does not escape.
            unsafe {
                handoff.with_active_producer_from_audio(|producer| {
                    producer.push(expected as f32).unwrap()
                })
            };
            assert_eq!(consumer.pop().unwrap(), expected as f32);
            handoff.reclaim_retired_from_watchdog();
        }
    }

    #[test]
    fn audio_swap_implementation_contains_no_lock_allocator_or_deallocator_call() {
        let source = include_str!("watchdog_handoff.rs");
        let start = source
            .find("pub unsafe fn swap_pending_from_audio")
            .expect("audio swap function exists");
        let end = source[start..]
            .find("/// 現 active Producer")
            .map(|offset| start + offset)
            .expect("next function marker exists");
        let audio_source = &source[start..end];

        for forbidden in ["Mutex", ".lock(", "Box::", "drop(", "Vec::", "String::"] {
            assert!(
                !audio_source.contains(forbidden),
                "Audio Thread swap must not contain {forbidden}"
            );
        }
    }
}
