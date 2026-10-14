//! PRE epoch ownership changes atomically; ready-file I/O stays outside its session lock.
use super::*;

enum Handshake {
    Ready {
        observed_end: i64,
        rearm_required: bool,
        publish: bool,
    },
    Applied,
    AwaitInput,
    Obsolete,
    Payload,
}

impl SpectrumCoordinator {
    /// `Some` completes the tick in handshake state; `None` continues to payload publication.
    pub(super) fn service_perceptual_handshake(
        &self,
        request_id: Uuid,
        pre_instance_id: &str,
        instance_dir: &Path,
        requested_epoch: Option<i64>,
    ) -> Option<bool> {
        let Some(decision) = self.prepare_perceptual_handshake(request_id, requested_epoch) else {
            return Some(false);
        };
        match decision {
            Handshake::Ready {
                observed_end,
                rearm_required,
                publish,
            } => Some(self.publish_pre_ready(
                request_id,
                pre_instance_id,
                instance_dir,
                observed_end,
                rearm_required,
                publish,
            )),
            Handshake::Applied => Some(true),
            Handshake::AwaitInput => Some(true),
            Handshake::Obsolete => Some(false),
            Handshake::Payload => None,
        }
    }

    fn prepare_perceptual_handshake(
        &self,
        request_id: Uuid,
        requested_epoch: Option<i64>,
    ) -> Option<Handshake> {
        let mut slot = self.try_pre_session()?;
        let current = slot
            .as_mut()
            .filter(|current| current.request_id == request_id)?;
        // A request's committed epoch is immutable. Mode/clock rearm uses a new request ID.
        // An older unarmed read or another epoch of this same ID cannot erase the live owner.
        if current.state_epoch_samples.is_some() && current.state_epoch_samples != requested_epoch {
            return Some(Handshake::Obsolete);
        }
        let Some(observed_end) = self.runtime.latest_presentation_end() else {
            return Some(Handshake::AwaitInput);
        };
        let rearm_required = self.runtime.take_perceptual_rearm_required();
        current.awaiting_rearm |= rearm_required;
        let ready = if current.awaiting_rearm {
            true
        } else if let Some(epoch) = requested_epoch {
            if current.state_epoch_samples == Some(epoch) {
                return Some(Handshake::Payload);
            }
            if observed_end < epoch && self.runtime.set_perceptual_state_epoch(Some(epoch)) {
                current.state_epoch_samples = Some(epoch);
                Self::clear_pre_epoch_publication(current);
                current.last_ready_written_at = None;
                return Some(Handshake::Applied);
            }
            true
        } else {
            false
        };
        if !self.runtime.set_perceptual_state_epoch(None) {
            return None;
        }
        current.state_epoch_samples = None;
        current.awaiting_rearm |= ready;
        Self::clear_pre_epoch_publication(current);
        let now = Instant::now();
        let publish = current
            .last_ready_written_at
            .is_none_or(|written| now.duration_since(written) >= REQUEST_RENEW_INTERVAL);
        if publish {
            current.last_ready_written_at = Some(now);
        }
        Some(Handshake::Ready {
            observed_end,
            rearm_required: ready,
            publish,
        })
    }

    pub(super) fn clear_pre_epoch_publication(current: &mut PreSession) {
        current.last_written_end = None;
        current.last_write_attempt_end = None;
        current.last_write_attempt_at = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pending_clock_rearm_survives_request_loss_before_the_handshake() {
        for missing in [false, true] {
            let temp = tempfile::tempdir().unwrap();
            let request_id = Uuid::new_v4();
            let mut request = AnalysisRequest {
                schema: REQUEST_SCHEMA.into(),
                request_id: request_id.to_string(),
                requested_by_post_instance_id: "post".into(),
                target_pre_instance_id: "pre".into(),
                sample_rate: 48_000,
                analysis_mode: AnalysisViewMode::Perceptual as u8,
                channel_mode: SpectrumChannelMode::Lr as u8,
                state_epoch_samples: Some(14_400),
                expires_at_unix_ms: unix_ms_now() + REQUEST_LEASE_MS,
                attack_band: None,
            };
            write_request(temp.path(), &request).unwrap();
            let runtime =
                SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
            let pre = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
            assert!(pre.pre_tick("pre", temp.path()));
            assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
            assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
            assert!(pre.pre_tick("pre", temp.path()));
            assert_eq!(runtime.perceptual_state_epoch(), Some(14_400));
            runtime.note_analysis_clock_drop_from_audio();
            if missing {
                remove_request(temp.path());
            } else {
                request.expires_at_unix_ms = 0;
                write_request(temp.path(), &request).unwrap();
            }
            assert!(!pre.pre_tick("pre", temp.path()));
            assert!(!runtime.is_enabled());
            assert!(
                pre.pre_session
                    .lock()
                    .unwrap()
                    .as_ref()
                    .unwrap()
                    .awaiting_rearm
            );
            request.expires_at_unix_ms = unix_ms_now() + REQUEST_LEASE_MS;
            write_request(temp.path(), &request).unwrap();
            assert!(pre.pre_tick("pre", temp.path()));
            assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
            assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
            assert!(pre.pre_tick("pre", temp.path()));
            assert_eq!(runtime.perceptual_state_epoch(), None);
            assert!(read_ready(temp.path()).unwrap().rearm_required());
            pre.shutdown();
            runtime.shutdown_and_join();
        }
    }

    #[test]
    fn delayed_old_or_missing_read_cannot_replace_or_retire_a_new_pre_owner() {
        for missing in [false, true] {
            let temp = tempfile::tempdir().unwrap();
            let runtime =
                SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
            let pre = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
            let write = |id: Uuid, epoch| {
                write_request(
                    temp.path(),
                    &AnalysisRequest {
                        schema: REQUEST_SCHEMA.into(),
                        request_id: id.to_string(),
                        requested_by_post_instance_id: "post".into(),
                        target_pre_instance_id: "pre".into(),
                        sample_rate: 48_000,
                        analysis_mode: AnalysisViewMode::Perceptual as u8,
                        channel_mode: SpectrumChannelMode::Lr as u8,
                        state_epoch_samples: Some(epoch),
                        expires_at_unix_ms: unix_ms_now() + REQUEST_LEASE_MS,
                        attack_band: None,
                    },
                )
                .unwrap();
            };
            write(Uuid::new_v4(), 14_400);
            if missing {
                remove_request(temp.path());
            }
            let (entered_tx, entered_rx) = std::sync::mpsc::channel();
            let (resume_tx, resume_rx) = std::sync::mpsc::channel();
            let old_pre = Arc::clone(&pre);
            let old_dir = temp.path().to_path_buf();
            let old = std::thread::spawn(move || {
                old_pre.pre_tick_observed("pre", &old_dir, || {
                    entered_tx.send(()).unwrap();
                    resume_rx.recv_timeout(Duration::from_secs(30)).unwrap();
                })
            });
            entered_rx.recv_timeout(Duration::from_secs(30)).unwrap();
            let current_id = Uuid::new_v4();
            write(current_id, 19_200);
            assert!(pre.pre_tick("pre", temp.path()));
            assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
            assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
            assert!(pre.pre_tick("pre", temp.path()));
            assert_eq!(runtime.perceptual_state_epoch(), Some(19_200));
            resume_tx.send(()).unwrap();
            assert!(!old.join().unwrap());
            let current = pre.pre_session.lock().unwrap();
            assert_eq!(current.as_ref().unwrap().request_id, current_id);
            assert_eq!(current.as_ref().unwrap().state_epoch_samples, Some(19_200));
            drop(current);
            assert!(runtime.is_enabled());
            assert_eq!(runtime.perceptual_state_epoch(), Some(19_200));
            pre.shutdown();
            runtime.shutdown_and_join();
        }
    }

    #[test]
    fn clock_rearm_waits_for_a_new_request_id_even_after_seek_before_old_epoch() {
        let temp = tempfile::tempdir().unwrap();
        let request_id = Uuid::new_v4();
        let write = |id: Uuid, epoch| {
            write_request(
                temp.path(),
                &AnalysisRequest {
                    schema: REQUEST_SCHEMA.into(),
                    request_id: id.to_string(),
                    requested_by_post_instance_id: "post".into(),
                    target_pre_instance_id: "pre".into(),
                    sample_rate: 48_000,
                    analysis_mode: AnalysisViewMode::Perceptual as u8,
                    channel_mode: SpectrumChannelMode::Lr as u8,
                    state_epoch_samples: epoch,
                    expires_at_unix_ms: unix_ms_now() + REQUEST_LEASE_MS,
                    attack_band: None,
                },
            )
            .unwrap();
        };
        let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
        write(request_id, Some(14_400));
        assert!(pre.pre_tick("pre", temp.path()));
        assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(runtime.perceptual_state_epoch(), Some(14_400));
        // The actual RT clock-loss entry revokes the old facts; input seeks before old E.
        runtime.note_analysis_clock_drop_from_audio();
        assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(runtime.perceptual_state_epoch(), None);
        assert!(read_ready(temp.path()).unwrap().rearm_required());
        for missing in [true, false] {
            if missing {
                remove_request(temp.path());
            } else {
                let mut expired = read_request(temp.path()).unwrap();
                expired.expires_at_unix_ms = 0;
                write_request(temp.path(), &expired).unwrap();
            }
            assert!(!pre.pre_tick("pre", temp.path()));
            assert!(!runtime.is_enabled());
            assert!(
                pre.pre_session
                    .lock()
                    .unwrap()
                    .as_ref()
                    .unwrap()
                    .awaiting_rearm
            );
            write(request_id, Some(14_400));
            assert!(pre.pre_tick("pre", temp.path()));
            assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
            assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
            assert!(pre.pre_tick("pre", temp.path()));
            assert_eq!(runtime.perceptual_state_epoch(), None);
        }
        for epoch in [Some(14_400), None, Some(19_200), Some(14_400)] {
            write(request_id, epoch);
            assert!(pre.pre_tick("pre", temp.path()));
            assert_eq!(runtime.perceptual_state_epoch(), None);
            let current = pre.pre_session.lock().unwrap();
            let current = current.as_ref().unwrap();
            assert!(current.awaiting_rearm);
            assert_eq!(current.state_epoch_samples, None);
        }
        let next_id = Uuid::new_v4();
        write(next_id, Some(19_200));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(runtime.perceptual_state_epoch(), None);
        assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(runtime.perceptual_state_epoch(), Some(19_200));
        assert!(
            !pre.pre_session
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .awaiting_rearm
        );
        pre.shutdown();
        runtime.shutdown_and_join();
    }

    #[test]
    fn stale_pre_read_cannot_reset_or_rearm_the_committed_owner() {
        let temp = tempfile::tempdir().unwrap();
        let request_id = Uuid::new_v4();
        let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&runtime));
        write_request(
            temp.path(),
            &AnalysisRequest {
                schema: REQUEST_SCHEMA.into(),
                request_id: request_id.to_string(),
                requested_by_post_instance_id: "post".into(),
                target_pre_instance_id: "pre".into(),
                sample_rate: 48_000,
                analysis_mode: AnalysisViewMode::Perceptual as u8,
                channel_mode: SpectrumChannelMode::Lr as u8,
                state_epoch_samples: Some(14_400),
                expires_at_unix_ms: unix_ms_now() + REQUEST_LEASE_MS,
                attack_band: None,
            },
        )
        .unwrap();
        assert!(pre.pre_tick("pre", temp.path()));
        assert!(runtime.push_block_from_audio(&[0.0; 512], 2, Some(0)));
        assert!(runtime.wait_for_input_for_test(Duration::from_secs(30)));
        assert!(pre.pre_tick("pre", temp.path()));
        assert_eq!(runtime.perceptual_state_epoch(), Some(14_400));
        for (id, epoch) in [
            (Uuid::new_v4(), Some(19_200)),
            (request_id, None),
            (request_id, Some(19_200)),
        ] {
            assert_eq!(
                pre.service_perceptual_handshake(id, "pre", temp.path(), epoch),
                Some(false)
            );
            assert_eq!(runtime.perceptual_state_epoch(), Some(14_400));
            assert_eq!(
                pre.pre_session
                    .lock()
                    .unwrap()
                    .as_ref()
                    .unwrap()
                    .state_epoch_samples,
                Some(14_400)
            );
        }
        assert_eq!(
            pre.service_perceptual_handshake(request_id, "pre", temp.path(), Some(14_400)),
            None
        );
        pre.shutdown();
        runtime.shutdown_and_join();
    }
}
