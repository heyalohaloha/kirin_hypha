//! Atomic Audio Thread admission of one clock descriptor and its matching sample block.

use super::*;

impl KirinHyphaEngine {
    pub fn note_capture_window(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
    ) {
        self.note_capture_window_with_presentation(
            position_valid,
            position_samples,
            num_frames,
            clock_source,
            PresentationLatencySamples::default(),
            false,
        );
    }

    pub fn note_capture_window_with_presentation(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        force_new_epoch: bool,
    ) {
        self.note_capture_window_with_clocks(
            position_valid,
            position_samples,
            num_frames,
            clock_source,
            presentation_latency,
            AuxiliaryClockSamples::default(),
            force_new_epoch,
        );
    }

    #[allow(clippy::too_many_arguments)]
    pub fn note_capture_window_with_clocks(
        &self,
        position_valid: bool,
        position_samples: i64,
        num_frames: u64,
        clock_source: CaptureClockSource,
        presentation_latency: PresentationLatencySamples,
        auxiliary: AuxiliaryClockSamples,
        force_new_epoch: bool,
    ) {
        // Stage facts only. `push_samples_transaction` commits this descriptor after the
        // destination SPSC proves it can accept the complete matching audio block.
        self.pending_capture_version.fetch_add(1, Ordering::AcqRel);
        self.pending_capture_valid.store(false, Ordering::Relaxed);
        self.pending_position_valid
            .store(position_valid, Ordering::Relaxed);
        self.pending_position_samples
            .store(position_samples, Ordering::Relaxed);
        self.pending_num_frames.store(num_frames, Ordering::Relaxed);
        self.pending_clock_source
            .store(clock_source as u8, Ordering::Relaxed);
        self.pending_presentation_source
            .store(presentation_latency.source as u8, Ordering::Relaxed);
        self.pending_input_presentation_samples.store(
            presentation_latency.input.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.pending_output_presentation_samples.store(
            presentation_latency.output.map_or(u64::MAX, u64::from),
            Ordering::Relaxed,
        );
        self.pending_auxiliary_source
            .store(auxiliary.source as u8, Ordering::Relaxed);
        self.pending_auxiliary_valid
            .store(auxiliary.samples.is_some(), Ordering::Relaxed);
        self.pending_auxiliary_samples
            .store(auxiliary.samples.unwrap_or(i64::MIN), Ordering::Relaxed);
        self.pending_force_new_epoch
            .store(force_new_epoch, Ordering::Relaxed);
        self.pending_capture_valid.store(true, Ordering::Relaxed);
        self.pending_capture_version.fetch_add(1, Ordering::Release);
    }

    #[inline]
    pub(super) fn take_pending_capture_window(
        &self,
        expected_frames: u64,
    ) -> Option<PendingCaptureWindow> {
        for _ in 0..4 {
            let before = self.pending_capture_version.load(Ordering::Acquire);
            if before & 1 != 0 || !self.pending_capture_valid.load(Ordering::Relaxed) {
                continue;
            }
            let pending = PendingCaptureWindow {
                position_valid: self.pending_position_valid.load(Ordering::Relaxed),
                position_samples: self.pending_position_samples.load(Ordering::Relaxed),
                num_frames: self.pending_num_frames.load(Ordering::Relaxed),
                clock_source: CaptureClockSource::from_abi(
                    self.pending_clock_source.load(Ordering::Relaxed),
                ),
                presentation_latency: PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(
                        self.pending_presentation_source.load(Ordering::Relaxed),
                    ),
                    input: u32::try_from(
                        self.pending_input_presentation_samples
                            .load(Ordering::Relaxed),
                    )
                    .ok(),
                    output: u32::try_from(
                        self.pending_output_presentation_samples
                            .load(Ordering::Relaxed),
                    )
                    .ok(),
                },
                auxiliary: AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::from_abi(
                        self.pending_auxiliary_source.load(Ordering::Relaxed),
                    ),
                    samples: self
                        .pending_auxiliary_valid
                        .load(Ordering::Relaxed)
                        .then(|| self.pending_auxiliary_samples.load(Ordering::Relaxed)),
                },
                force_new_epoch: self.pending_force_new_epoch.load(Ordering::Relaxed),
            };
            let after = self.pending_capture_version.load(Ordering::Acquire);
            if before == after && after & 1 == 0 {
                self.pending_capture_valid.store(false, Ordering::Release);
                return (pending.num_frames == expected_frames).then_some(pending);
            }
        }
        self.pending_capture_valid.store(false, Ordering::Release);
        None
    }
}

/// Stage the immutable host clocks consumed by the immediately following audio transaction.
///
/// # Safety
/// `handle` must be null or a live pointer returned by `kirin_hypha_create`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_note_capture_window(
    handle: *mut KirinHyphaEngine,
    position_valid: bool,
    position_samples: i64,
    num_frames: u64,
    clock_source: u8,
    presentation_source: u8,
    input_presentation_valid: bool,
    input_presentation_samples: u32,
    output_presentation_valid: bool,
    output_presentation_samples: u32,
    auxiliary_source: u8,
    auxiliary_valid: bool,
    auxiliary_samples: i64,
    force_new_epoch: bool,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return;
        }
        unsafe {
            (*handle).note_capture_window_with_clocks(
                position_valid,
                position_samples,
                num_frames,
                CaptureClockSource::from_abi(clock_source),
                PresentationLatencySamples {
                    source: PresentationLatencySource::from_abi(presentation_source),
                    input: input_presentation_valid.then_some(input_presentation_samples),
                    output: output_presentation_valid.then_some(output_presentation_samples),
                },
                AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::from_abi(auxiliary_source),
                    samples: auxiliary_valid.then_some(auxiliary_samples),
                },
                force_new_epoch,
            );
        }
    }));
}
