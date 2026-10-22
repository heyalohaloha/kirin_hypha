//! Audio capture transaction shared by every shell format.
use super::*;

impl KirinHyphaEngine {
    /// Shipping Audio-Thread transaction. A non-empty callback must already have an exact host
    /// descriptor staged by the wrapper; the direct Rust shelf above is never used by JUCE.
    pub(super) fn push_samples_transaction(&self, interleaved: &[f32], num_channels: u32) -> bool {
        self.aax_analysis.begin_callback();
        // (1) heartbeat は常に進める（空ブロック keepalive でも Active を維持できる）。
        self.heartbeat.fetch_add(1, Ordering::Relaxed);
        if self.watch_playback_pass_id.load(Ordering::Acquire) == 0 {
            self.watch_playback_pass_id.store(1, Ordering::Release);
            reset_watch_ring_cursor(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                1,
            );
        }

        // Watchdog 再起動後の Producer 所有権を atomic pointer だけで取り込む。
        // 旧 Producer は retired slot へ移し、Watchdog Thread が破棄するため、この
        // Audio Thread 経路に lock / allocation / deallocation はない。
        // SAFETY: push_samples は FFI 契約上、単一 Audio Thread からのみ呼ばれる。
        if unsafe { self.producer_handoff.swap_pending_from_audio() } {
            self.record_take_tracker.reset_capture_clock();
            self.record_take_tracker
                .rebind_meter_active_after_clock_reset();
            self.capture_last_offline.store(false, Ordering::Release);
            let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
            reset_watch_ring_cursor(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                pass_id,
            );
            self.watch_ring_replacing.store(false, Ordering::Release);
        }
        // SAFETY: same single Audio Thread. Record adoption only moves atomic pointers.
        unsafe { self.record_ingress.adopt_from_audio() };

        // (2) create 時の layout と異なるブロックは測定に入れない。
        if num_channels as usize != self.num_channels {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            return false;
        }

        let recording = self.record_sm.is_recording();
        if !interleaved.len().is_multiple_of(self.num_channels) {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        let frames = (interleaved.len() / self.num_channels) as u64;
        if frames > MAX_AUDIO_BLOCK_FRAMES as u64 {
            self.pending_capture_valid.store(false, Ordering::Release);
            self.pending_record_valid.store(false, Ordering::Release);
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        let had_pending_clock = self.pending_capture_valid.load(Ordering::Acquire);
        let pending_clock = self.take_pending_capture_window(frames);
        let had_pending_record = self.pending_record_valid.load(Ordering::Acquire);
        let pending_record = self.take_pending_record_block(frames);
        if had_pending_clock && pending_clock.is_none() {
            // A descriptor/sample cardinality mismatch is an invalid callback transaction. Drop
            // it whole; accepting unclocked audio would shift every later producer coordinate.
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        if had_pending_record && pending_record.is_none() {
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        if interleaved.is_empty() {
            if let Some(block) = pending_record {
                self.commit_pending_record_block(block);
            }
            return false;
        }
        if pending_clock.is_none() {
            // A sample without an immutable producer coordinate can never enter raw pre-roll or
            // Record truthfully. The JUCE ABI guarantees note_capture_window immediately before
            // every non-empty push, so this is a malformed transaction.
            self.push_overflow
                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            return false;
        }
        self.push_optional_analysis(interleaved, pending_clock);
        let accepted_offline_mode = pending_record.map(|block| block.offline);
        let offline_capture_boundary = accepted_offline_mode.is_some_and(|offline| offline)
            && !self.capture_last_offline.load(Ordering::Acquire);
        let pushed = if recording {
            let generation = self.record_sm.generation();
            // SAFETY: producer access is closure-bounded to this one Audio callback.
            let accepted = unsafe {
                self.record_ingress
                    .with_producer_from_audio(generation, |producer| {
                        if producer.slots() < interleaved.len() {
                            self.push_overflow
                                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
                            return 0;
                        }
                        let capture_origin = self.record_take_tracker.captured_frames_total();
                        self.record_ingress
                            .begin_generation_from_audio(generation, capture_origin);
                        if let Some(block) = pending_record {
                            self.commit_pending_record_block(block);
                        }
                        if let Some(clock) = pending_clock {
                            self.record_take_tracker
                                .note_capture_window_with_clocks_boundary(
                                    clock.position_valid,
                                    clock.position_samples,
                                    clock.num_frames,
                                    clock.clock_source,
                                    clock.presentation_latency,
                                    clock.auxiliary,
                                    clock.force_new_epoch || offline_capture_boundary,
                                );
                        }
                        for &sample in interleaved {
                            // The single producer checked the complete cardinality above. A
                            // concurrent consumer can only increase, never consume, free slots.
                            let _ = producer.push(sample);
                        }
                        interleaved.len() as u64
                    })
            };
            if accepted.is_none() {
                self.push_overflow
                    .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
            }
            accepted.unwrap_or(0)
        } else {
            // SAFETY: push_samples is the sole Audio Thread owner. Producer access cannot cross
            // the callback or a handoff swap.
            unsafe {
                self.producer_handoff
                    .with_active_producer_from_audio(|producer| {
                        if producer.slots() < interleaved.len() {
                            self.push_overflow
                                .fetch_add(interleaved.len() as u64, Ordering::Relaxed);
                            return 0;
                        }
                        if let Some(block) = pending_record {
                            self.commit_pending_record_block(block);
                        }
                        if let Some(clock) = pending_clock {
                            self.record_take_tracker
                                .note_capture_window_with_clocks_boundary(
                                    clock.position_valid,
                                    clock.position_samples,
                                    clock.num_frames,
                                    clock.clock_source,
                                    clock.presentation_latency,
                                    clock.auxiliary,
                                    clock.force_new_epoch || offline_capture_boundary,
                                );
                        }
                        for &sample in interleaved {
                            let _ = producer.push(sample);
                        }
                        interleaved.len() as u64
                    })
            }
        };
        if pushed > 0 {
            if let Some(offline) = accepted_offline_mode {
                self.capture_last_offline.store(offline, Ordering::Release);
            }
        }
        if !recording && pushed > 0 && !self.watch_ring_replacing.load(Ordering::Acquire) {
            let pass_id = self.watch_playback_pass_id.load(Ordering::Acquire);
            add_watch_ring_cursor_samples(
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                pass_id,
                pushed,
            );
        }
        pushed == interleaved.len() as u64
    }
}
