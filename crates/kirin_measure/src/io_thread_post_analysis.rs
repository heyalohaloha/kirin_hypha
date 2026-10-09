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
    pub(super) project_hash: &'a str,
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
    // Audition changes the optional listening path, not the original PRE/POST TIME facts.
    // Spectrum keeps its existing audition suppression while TIME retains the same latch.
    let (spectrum, meter_history) = confirmed_analysis_targets(latched_pre);
    if comparison_audition_active {
        (None, meter_history)
    } else {
        (spectrum, meter_history)
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

fn attack_pair_origin(
    target: &SpectrumTarget,
    binding: PostAnalysisBinding<'_>,
) -> Option<crate::spectrum_exchange::AttackPairAuthority> {
    if binding.generation == 0
        || !binding.claimed_at.is_finite()
        || binding.claimed_at <= 0.0
        || !crate::is_path_safe_component(binding.project_hash)
        || !crate::is_path_safe_component(binding.post_instance_id)
        || !crate::is_path_safe_component(binding.pair_owner_id)
        || Some(target.pre_instance_id.as_str()) != binding.paired_pre_instance_id
        || target.instance_dir.file_name()?.to_str()? != target.pre_instance_id
    {
        return None;
    }
    // The latch has already passed project/session/host scope admission. PRE and POST may
    // have separate role-local shelves, so provenance names the exact resolved PRE shelf.
    let project_hash = target.instance_dir.parent()?.file_name()?.to_str()?;
    if !crate::is_path_safe_component(project_hash)
        || !crate::is_path_safe_component(&target.pre_instance_id)
    {
        return None;
    }
    Some(crate::spectrum_exchange::AttackPairAuthority {
        generation: binding.generation,
        project_hash: project_hash.into(),
        pre_instance_id: target.pre_instance_id.clone(),
        post_instance_id: binding.post_instance_id.into(),
        owner_id: binding.pair_owner_id.into(),
        claimed_at_bits: binding.claimed_at.to_bits(),
    })
}

pub(super) fn service_post_analysis_endpoints(
    spectrum: Option<&Arc<SpectrumCoordinator>>,
    meter_history: Option<&Arc<MeterDeltaHistoryExchange>>,
    latched_pre: &Arc<Mutex<Option<LatchedPre>>>,
    binding: PostAnalysisBinding<'_>,
    comparison_audition_active: bool,
) {
    if binding.generation == 0 {
        return;
    }
    let (spectrum_target, meter_history_target) =
        bound_analysis_targets(latched_pre, binding, comparison_audition_active);
    if let Some(spectrum) = spectrum {
        let origin = spectrum_target
            .as_ref()
            .and_then(|target| attack_pair_origin(target, binding));
        spectrum.set_attack_pair_authority(binding.generation, origin);
        spectrum.service_post_endpoint(
            binding.post_instance_id,
            spectrum_target,
            binding.pair_pre_name,
        );
    }
    if let Some(meter_history) = meter_history {
        meter_history.set_pair_authority_revision(binding.generation);
        meter_history.service_post_endpoint(meter_history_target);
    }
}

#[cfg(test)]
#[path = "io_thread_post_analysis_tests.rs"]
mod tests;
