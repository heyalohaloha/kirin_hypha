//! One bounded selected request, with a clock-injected state machine and immutable terminal copy.
use super::snapshot::{AttackBandObservation, AttackFinish, AttackSourceKey};
use super::{AttackDetailedEvent, AttackEvent, AttackRuntime};
use crate::attack_perception::band::{AttackBand, BandSpanEnd};
use std::sync::atomic::Ordering;
use std::sync::TryLockError;

pub const ATTACK_SINGLE_RESPONSE_MILLIS: u64 = 1_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AttackSinglePollError {
    Busy,
    Missing,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum AttackSingleReason {
    #[default]
    None = 0,
    RequestDeadline = 1,
    WorkerUnavailable = 2,
    SourceChanged = 3,
    AudioNotKept = 4,
}

#[derive(Clone, Debug, PartialEq)]
pub struct AttackSingleRequest {
    /// The content event owner may be PRE; `local_source` is the POST being measured.
    pub key_source: AttackSourceKey,
    pub key_event_sample: i64,
    pub key_token: u64,
    pub local_source: AttackSourceKey,
    pub pre_source: Option<AttackSourceKey>,
    pub event: AttackEvent,
    pub band: Option<AttackBand>,
    pub requested_end: i64,
    pub measurement_end: i64,
    pub span_end: BandSpanEnd,
    pub pre: Option<AttackBandObservation>,
    pub pre_detail: Option<AttackDetailedEvent>,
    pub pair_kind: u8,
    pub target: u8,
    pub pair_authority_revision: u64,
    pub proof_token: [u8; 32],
}

#[derive(Clone, Debug, PartialEq)]
pub struct AttackSingleSnapshot {
    pub request: AttackSingleRequest,
    pub token: u64,
    pub revision: u64,
    pub finish: AttackFinish,
    pub reason: AttackSingleReason,
    pub band_observation: Option<AttackBandObservation>,
    pub detail: Option<AttackDetailedEvent>,
}

#[derive(Default)]
pub(super) struct AttackSingleControl {
    serial: u64,
    pub(super) current: Option<AttackSingleSnapshot>,
    accepted_millis: u64,
}

impl AttackSingleControl {
    pub(super) fn complete_pre(
        &mut self,
        token: u64,
        source: AttackSourceKey,
        proof_token: [u8; 32],
        detail: AttackDetailedEvent,
        now: u64,
        worker: bool,
    ) -> bool {
        self.expire(now, worker);
        let Some(current) = self
            .current
            .as_mut()
            .filter(|current| current.token == token)
        else {
            return false;
        };
        if current.finish != AttackFinish::Acquiring
            || current.request.band.is_some()
            || !matches!(current.request.pair_kind, 0 | 1)
            || current.request.pre_source != Some(source)
            || proof_token == [0; 32]
            || current.request.proof_token != proof_token
            || !source.matches(&detail.event)
            || detail.event.event_sample != current.request.event.event_sample
            || !detail.has_valid_layout()
            || !detail.features.complete
            || detail.features.body_end_sample <= current.request.event.event_sample
            || detail.features.body_end_sample > current.request.requested_end
        {
            return false;
        }
        if current
            .request
            .pre_detail
            .is_some_and(|previous| previous.features.complete)
        {
            return current.request.pre_detail == Some(detail);
        }
        current.request.pre_detail = Some(detail);
        current.request.measurement_end = detail.features.body_end_sample;
        current.request.span_end =
            if detail.features.body_end_sample < current.request.requested_end {
                BandSpanEnd::NextHit
            } else {
                BandSpanEnd::Window
            };
        current.revision += 1;
        true
    }
    pub(super) fn begin(&mut self, request: AttackSingleRequest, now: u64) -> u64 {
        self.serial = self.serial.wrapping_add(1).max(1);
        self.accepted_millis = now;
        self.current = Some(AttackSingleSnapshot {
            request,
            token: self.serial,
            revision: 1,
            finish: AttackFinish::Acquiring,
            reason: AttackSingleReason::None,
            band_observation: None,
            detail: None,
        });
        self.serial
    }
    pub(super) fn expire(&mut self, now: u64, worker: bool) {
        let Some(current) = self.current.as_mut() else {
            return;
        };
        if current.finish != AttackFinish::Acquiring {
            return;
        }
        let reason = if now.saturating_sub(self.accepted_millis) >= ATTACK_SINGLE_RESPONSE_MILLIS {
            AttackSingleReason::RequestDeadline
        } else if !worker {
            AttackSingleReason::WorkerUnavailable
        } else {
            return;
        };
        current.finish = AttackFinish::Retired;
        current.reason = reason;
        current.band_observation = None;
        current.detail = None;
        current.revision += 1;
    }
    pub(super) fn commit(&mut self, mut reply: AttackSingleSnapshot, now: u64) -> bool {
        self.expire(now, true);
        let Some(current) = self.current.as_ref() else {
            return false;
        };
        if current.token != reply.token
            || !allows_request_refinement(&current.request, &reply.request)
            || current.finish != AttackFinish::Acquiring
        {
            return false;
        }
        if current.finish == reply.finish
            && current.reason == reply.reason
            && current.band_observation == reply.band_observation
            && current.detail == reply.detail
        {
            return true;
        }
        reply.revision = current.revision + 1;
        self.current = Some(reply);
        true
    }
}

fn allows_request_refinement(current: &AttackSingleRequest, reply: &AttackSingleRequest) -> bool {
    if current == reply {
        return true;
    }
    if current.pair_kind != 4
        || current.proof_token != [0; 32]
        || reply.span_end != BandSpanEnd::NextHit
        || reply.measurement_end >= current.measurement_end
        || reply.requested_end != current.requested_end
    {
        return false;
    }
    let mut restored = reply.clone();
    restored.requested_end = current.requested_end;
    restored.measurement_end = current.measurement_end;
    restored.span_end = current.span_end;
    restored == *current
}

fn retire_state(current: &mut AttackSingleSnapshot, reason: AttackSingleReason) {
    if current.finish != AttackFinish::Retired || current.reason != reason {
        current.revision += 1;
    }
    current.finish = AttackFinish::Retired;
    current.reason = reason;
    current.band_observation = None;
    current.detail = None;
}

impl AttackRuntime {
    /// Poll may supply the original PRE's completed ALL detail while this request acquires.
    /// Identity and acceptance time stay fixed; a late or terminal update cannot change facts.
    pub fn complete_single_pre(
        &self,
        token: u64,
        source: AttackSourceKey,
        proof_token: [u8; 32],
        detail: AttackDetailedEvent,
    ) -> Option<bool> {
        Some(self.selected.try_lock().ok()?.complete_pre(
            token,
            source,
            proof_token,
            detail,
            self.single_clock_millis(),
            self.worker_running.load(Ordering::Acquire),
        ))
    }
    pub(super) fn single_clock_millis(&self) -> u64 {
        self.single_clock_origin
            .elapsed()
            .as_millis()
            .min(u128::from(u64::MAX)) as u64
    }
    pub fn request_single(&self, request: AttackSingleRequest) -> Option<u64> {
        let source = self.try_observation_snapshot()?.source?.source;
        if source != request.local_source
            || !source.matches(&request.event)
            || request.band != self.band()
            || request.measurement_end <= request.event.event_sample
            || request.measurement_end > request.requested_end
        {
            return None;
        }
        let token = self
            .selected
            .try_lock()
            .ok()?
            .begin(request, self.single_clock_millis());
        self.wake.1.notify_all();
        Some(token)
    }
    pub fn poll_single(&self, token: u64) -> Option<AttackSingleSnapshot> {
        self.try_poll_single(token).ok()
    }
    /// A lock conflict leaves the selected request intact; only an absent/replaced token is missing.
    pub fn try_poll_single(
        &self,
        token: u64,
    ) -> Result<AttackSingleSnapshot, AttackSinglePollError> {
        let mut selected = match self.selected.try_lock() {
            Ok(selected) => selected,
            Err(TryLockError::WouldBlock) => return Err(AttackSinglePollError::Busy),
            Err(TryLockError::Poisoned(poisoned)) => {
                let mut selected = poisoned.into_inner();
                if let Some(current) = selected.current.as_mut() {
                    retire_state(current, AttackSingleReason::WorkerUnavailable);
                }
                let result = selected
                    .current
                    .as_ref()
                    .filter(|value| value.token == token)
                    .cloned()
                    .ok_or(AttackSinglePollError::Missing);
                // The selected result is now closed. Future requests/cancellation can proceed.
                self.selected.clear_poison();
                return result;
            }
        };
        selected.expire(
            self.single_clock_millis(),
            self.worker_running.load(Ordering::Acquire),
        );
        let current = selected
            .current
            .as_mut()
            .filter(|value| value.token == token)
            .ok_or(AttackSinglePollError::Missing)?;
        let worker = self.worker_running.load(Ordering::Acquire);
        if !worker
            || !self.is_enabled()
            || current.request.local_source.generation != self.generation.load(Ordering::Acquire)
            || current.request.band != self.band()
        {
            let reason = if !worker {
                AttackSingleReason::WorkerUnavailable
            } else {
                AttackSingleReason::SourceChanged
            };
            retire_state(current, reason);
        }
        Ok(current.clone())
    }
    pub fn retire_single(
        &self,
        token: u64,
        reason: AttackSingleReason,
    ) -> Option<AttackSingleSnapshot> {
        let mut selected = self.selected.try_lock().ok()?;
        let current = selected
            .current
            .as_mut()
            .filter(|value| value.token == token)?;
        retire_state(current, reason);
        Some(current.clone())
    }
    pub fn try_cancel_single(&self, token: u64) -> Option<bool> {
        let mut selected = self.selected.try_lock().ok()?;
        if selected
            .current
            .as_ref()
            .is_none_or(|value| value.token != token)
        {
            return Some(false);
        }
        selected.current = None;
        Some(true)
    }
    pub fn cancel_single(&self, token: u64) -> bool {
        self.try_cancel_single(token) == Some(true)
    }
}

#[cfg(test)]
#[path = "attack_single_tests.rs"]
mod tests;

#[cfg(any(test, feature = "test-support"))]
#[path = "attack_runtime_test_support.rs"]
pub mod test_support;
