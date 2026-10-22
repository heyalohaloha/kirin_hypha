//! Audio-thread host transport publication.
use super::*;

impl KirinHyphaEngine {
    /// Publish one host transport block. Audio Thread only; atomics and the
    /// existing ring-cursor seqlock make this RT-safe.
    pub fn note_transport_block(
        &self,
        playing: bool,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        force_new_pass: bool,
    ) {
        let previous_playing = self
            .transport_previous_playing
            .swap(playing, Ordering::AcqRel);
        let previous_valid = self
            .transport_previous_position_valid
            .swap(position_valid, Ordering::AcqRel);
        let previous_position = self
            .transport_previous_position_samples
            .swap(position_samples, Ordering::AcqRel);
        let previous_frames = self
            .transport_previous_num_frames
            .swap(num_frames, Ordering::AcqRel);

        let discontinuity = playing
            && previous_playing
            && position_valid
            && previous_valid
            && previous_position.checked_add(previous_frames as i64) != Some(position_samples);
        if playing && (!previous_playing || discontinuity || (previous_valid && !position_valid)) {
            if position_valid {
                self.record_take_tracker
                    .note_transport_start(position_samples);
            } else {
                self.record_take_tracker.note_unknown_transport_start();
            }
        }
        if watch_transport_starts_new_pass(playing, previous_playing, discontinuity, force_new_pass)
        {
            publish_watch_playback_pass_boundary(
                &self.watch_playback_pass_id,
                &self.watch_ring_cursor_epoch,
                &self.watch_ring_cursor_pass_id,
                &self.watch_ring_cursor_samples,
                &self.watch_playback_pass_cutover_samples,
            );
        }
    }
}

#[inline]
fn watch_transport_starts_new_pass(
    playing: bool,
    previous_playing: bool,
    discontinuity: bool,
    force_new_pass: bool,
) -> bool {
    playing && (force_new_pass || !previous_playing || discontinuity)
}

#[cfg(test)]
mod tests {
    use super::watch_transport_starts_new_pass;

    #[test]
    fn shared_unavailable_to_active_boundary_starts_a_new_watch_pass() {
        assert!(watch_transport_starts_new_pass(true, true, false, true));
    }

    #[test]
    fn continuous_playback_without_a_shared_boundary_keeps_the_current_pass() {
        assert!(!watch_transport_starts_new_pass(true, true, false, false));
    }

    #[test]
    fn unavailable_boundary_cannot_start_a_pass_while_transport_is_stopped() {
        assert!(!watch_transport_starts_new_pass(false, true, false, true));
    }
}

#[cfg(test)]
#[path = "transport_block_start_tests.rs"]
mod start_tests;

#[cfg(test)]
#[path = "transport_block_retirement_tests.rs"]
mod retirement_tests;
