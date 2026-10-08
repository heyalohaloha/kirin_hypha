//! Selected work is serviced before background history, without waiting on GUI locks.
use super::band_worker::{needs_refresh, plan, BandWorker, Plan};
use super::single::AttackSingleReason;
use super::snapshot::{AttackBandObservation, AttackFinish};
use super::AttackRuntime;
use crate::attack_perception::band::BandSpanEnd;

impl AttackRuntime {
    pub(super) fn service_single(
        &self,
        worker: &mut BandWorker,
        idle: bool,
        decided_before: Option<i64>,
    ) {
        let now = self.single_clock_millis();
        let mut reply = {
            let Ok(mut selected) = self.selected.try_lock() else {
                return;
            };
            selected.expire(now, true);
            let Some(current) = selected.current.as_ref() else {
                return;
            };
            if current.finish != AttackFinish::Acquiring {
                return;
            }
            current.clone()
        };
        let mut request = reply.request.clone();
        // A head-only PRE publication is an in-progress observation, not proof of audio end.
        // Only a same-source completion admitted by poll can finalize paired ALL. If PRE never
        // supplies it, the original acceptance deadline retires this request explicitly.
        if request.band.is_none()
            && matches!(request.pair_kind, 0 | 1)
            && request
                .pre_detail
                .is_none_or(|detail| !detail.features.complete)
        {
            return;
        }
        let mut tail_known = true;
        if request.pair_kind == 4 && request.proof_token == [0; 32] {
            let next = if request.band.is_some() {
                worker
                    .events
                    .iter()
                    .find(|event| event.event_sample > request.event.event_sample)
                    .map(|event| event.event_sample)
            } else {
                self.history.try_lock().ok().and_then(|history| {
                    history
                        .events()
                        .find(|event| {
                            event.generation == request.local_source.generation
                                && event.event_sample > request.event.event_sample
                        })
                        .map(|event| event.event_sample)
                })
            };
            let end = if request.band.is_some() {
                crate::attack_perception::band::span_end_for(
                    self.sample_rate,
                    request.event.event_sample,
                    next,
                )
                .0
            } else {
                self.bins
                    .try_lock()
                    .ok()
                    .map_or(request.measurement_end, |bins| {
                        bins.body_end_sample(request.event.event_sample, next)
                    })
            };
            if end < request.measurement_end {
                request.measurement_end = end;
                request.span_end = BandSpanEnd::NextHit;
                reply.request = request.clone();
            }
            tail_known = next.is_some_and(|next| next <= request.measurement_end)
                || decided_before.is_some_and(|sample| sample >= request.measurement_end);
        }
        if request.local_source.generation != worker.generation || request.band != worker.band {
            return;
        }
        if let Some(band) = request.band {
            let cached = worker
                .results
                .anchored_at(request.event.event_sample, request.measurement_end)
                .filter(|detail| !needs_refresh(detail, worker.ring.as_ref(), idle))
                .or_else(|| {
                    worker
                        .results
                        .own_at(request.event.event_sample)
                        .filter(|value| value.span_end_sample == request.measurement_end)
                        .filter(|detail| !needs_refresh(detail, worker.ring.as_ref(), idle))
                });
            let was_cached = cached.is_some();
            let observed = if let Some(detail) = cached {
                Some(AttackBandObservation::measured(
                    request.event,
                    request.requested_end,
                    1,
                    detail.measure,
                ))
            } else {
                match plan(
                    worker.ring.as_ref(),
                    band,
                    self.sample_rate,
                    request.event.event_sample,
                    request.measurement_end,
                    request.span_end,
                    tail_known,
                    idle,
                ) {
                    Plan::Wait => None,
                    Plan::NotKept => Some(AttackBandObservation::measured(
                        request.event,
                        request.requested_end,
                        1,
                        None,
                    )),
                    Plan::Measure(end, reason) => {
                        let measure = self.measure_band_at(
                            worker,
                            band,
                            request.event.event_sample,
                            end,
                            reason,
                        );
                        Some(AttackBandObservation::measured(
                            request.event,
                            request.requested_end,
                            1,
                            measure,
                        ))
                    }
                }
            };
            if let Some(observed) = observed {
                if !was_cached {
                    worker.cache_selected(super::AttackBandDetail {
                        event: request.event,
                        band,
                        span_end_sample: request.measurement_end,
                        measure: observed.measure,
                    });
                }
                reply.finish = observed.finish;
                if observed.finish == AttackFinish::NotKept {
                    reply.reason = AttackSingleReason::AudioNotKept;
                }
                reply.band_observation = Some(observed);
            }
        } else {
            let Ok(bins) = self.bins.try_lock() else {
                return;
            };
            let event = request.event;
            let full = (tail_known && request.span_end != BandSpanEnd::AudioEnd)
                .then(|| bins.measure(event, Some(request.measurement_end)))
                .flatten();
            reply.detail = full
                .or_else(|| bins.measure(event, None))
                .map(|(features, shape)| super::AttackDetailedEvent {
                    event,
                    features,
                    shape,
                });
            if full.is_some() {
                reply.finish = AttackFinish::Full;
            } else if idle || request.span_end == BandSpanEnd::AudioEnd {
                reply.finish = if reply.detail.is_some() {
                    AttackFinish::AudioEnd
                } else {
                    AttackFinish::NotKept
                };
                if reply.detail.is_none() {
                    reply.reason = AttackSingleReason::AudioNotKept;
                }
            }
        }
        if let Ok(mut selected) = self.selected.try_lock() {
            selected.commit(reply, self.single_clock_millis());
        }
    }
}

#[cfg(test)]
#[path = "attack_single_worker_tests.rs"]
mod tests;
