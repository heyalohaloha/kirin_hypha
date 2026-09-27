//! Optional POST analysis endpoints bound to one exact confirmed PRE latch.

use std::sync::{Arc, Mutex};

use crate::pairing_scope::{LatchedPre, LatchedPreReadiness};
use crate::{MeterDeltaHistoryExchange, MeterHistoryTarget, SpectrumCoordinator, SpectrumTarget};

pub(super) struct PostAnalysisEndpoints {
    spectrum: Option<Arc<SpectrumCoordinator>>,
    meter_history: Option<Arc<MeterDeltaHistoryExchange>>,
}

#[derive(Clone, Copy)]
pub(super) struct PostAnalysisBinding<'a> {
    pub(super) post_instance_id: &'a str,
    pub(super) pair_pre_name: &'a str,
    pub(super) paired_pre_instance_id: Option<&'a str>,
    pub(super) pair_owner_id: &'a str,
    pub(super) generation: u64,
    pub(super) claimed_at: f64,
}

impl PostAnalysisEndpoints {
    pub(super) fn new(
        spectrum: Option<Arc<SpectrumCoordinator>>,
        meter_history: Option<Arc<MeterDeltaHistoryExchange>>,
    ) -> Self {
        Self {
            spectrum,
            meter_history,
        }
    }

    pub(super) fn service(
        &self,
        latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
        binding: PostAnalysisBinding<'_>,
        comparison_audition_active: bool,
    ) {
        service_post_analysis_endpoints(
            self.spectrum.as_ref(),
            self.meter_history.as_ref(),
            latched_pre,
            binding,
            comparison_audition_active,
        );
    }
}

fn confirmed_analysis_targets(
    latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
) -> (Option<SpectrumTarget>, Option<MeterHistoryTarget>) {
    let confirmed = latched_pre
        .lock()
        .ok()
        .and_then(|latched| latched.clone())
        .filter(|latched| latched.readiness == LatchedPreReadiness::Confirmed);
    let spectrum = confirmed.as_ref().and_then(|latched| {
        SpectrumTarget::from_pre_json(latched.instance_id.clone(), &latched.pre_json)
    });
    let meter_history = confirmed.as_ref().and_then(|latched| {
        MeterHistoryTarget::from_pre_json(latched.instance_id.clone(), &latched.pre_json)
    });
    (spectrum, meter_history)
}

fn active_analysis_targets(
    latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
    comparison_audition_active: bool,
) -> (Option<SpectrumTarget>, Option<MeterHistoryTarget>) {
    if comparison_audition_active {
        (None, None)
    } else {
        confirmed_analysis_targets(latched_pre)
    }
}

fn bound_analysis_targets(
    latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
    binding: PostAnalysisBinding<'_>,
    comparison_audition_active: bool,
) -> (Option<SpectrumTarget>, Option<MeterHistoryTarget>) {
    let (spectrum, meter_history) =
        active_analysis_targets(latched_pre, comparison_audition_active);
    if meter_history
        .as_ref()
        .map(|target| target.pre_instance_id.as_str())
        != binding.paired_pre_instance_id
    {
        return (None, None);
    }
    let meter_history = meter_history.and_then(|target| {
        target.with_post_binding(
            binding.pair_owner_id,
            binding.post_instance_id,
            binding.generation,
            binding.claimed_at,
        )
    });
    (spectrum, meter_history)
}

pub(super) fn service_post_analysis_endpoints(
    spectrum: Option<&Arc<SpectrumCoordinator>>,
    meter_history: Option<&Arc<MeterDeltaHistoryExchange>>,
    latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
    binding: PostAnalysisBinding<'_>,
    comparison_audition_active: bool,
) {
    let (spectrum_target, meter_history_target) =
        bound_analysis_targets(latched_pre, binding, comparison_audition_active);
    if let Some(spectrum) = spectrum {
        spectrum.service_post_endpoint(
            binding.post_instance_id,
            spectrum_target,
            binding.pair_pre_name,
        );
    }
    if let Some(meter_history) = meter_history {
        meter_history.service_post_endpoint(meter_history_target);
    }
}

#[cfg(test)]
#[path = "io_thread_post_analysis_tests.rs"]
mod tests;
