use super::*;
use crate::pair_claim_index::StablePairClaimObservation;

#[derive(Clone, Debug, Eq, PartialEq)]
// This poll runs repeatedly on the non-RT service thread. Keep the admitted request inline instead
// of allocating a Box for every stable observation; the 256-byte stack value is bounded.
#[allow(clippy::large_enum_variant)]
pub enum ActiveCaptureRequestPoll {
    Unavailable,
    Contended,
    Current(LocalBlindCaptureRequest),
}

/// Keep transactional claim-lock contention distinct from a stable missing or changed pair.
/// A caller that already owns an admitted capture may retain that exact request through
/// `Contended`, then fail closed if the next stable observation is `Unavailable`.
#[allow(clippy::too_many_arguments)]
pub fn poll_validated_local_blind_capture_request_for_active_result(
    kirin_root: &Path,
    instance_dir: &Path,
    pre_project_hash: &str,
    pre_instance_id: &str,
    sample_rate: u32,
    channels: u8,
    now_unix_ms: i64,
) -> ActiveCaptureRequestPoll {
    let Some(request) = read_capture_request(instance_dir) else {
        return ActiveCaptureRequestPoll::Unavailable;
    };
    let target = CaptureTarget {
        kirin_root,
        instance_dir,
        pre_project_hash,
        pre_instance_id,
        sample_rate,
        channels,
    };
    if !request.valid_shape()
        || request.issued_at_unix_ms > now_unix_ms
        || !request.matches_static_target(target)
    {
        return ActiveCaptureRequestPoll::Unavailable;
    }
    match crate::pair_claim_index::try_observe_pair_claim(
        kirin_root,
        request.authority.host_process_id,
        &request.authority.pre_instance_id,
    ) {
        Ok(StablePairClaimObservation::Contended) => ActiveCaptureRequestPoll::Contended,
        Ok(StablePairClaimObservation::Stable {
            claim: Some(claim),
            owned: true,
        }) if claim.pre_instance_id == request.authority.pre_instance_id
            && claim.project_hash == request.authority.post_project_hash
            && claim.post_instance_id == request.authority.post_instance_id
            && claim.pair_owner_id == request.authority.pair_owner_id
            && claim.host_process_id == request.authority.host_process_id
            && claim.pair_claimed_at_bits == request.authority.pair_claimed_at_bits =>
        {
            ActiveCaptureRequestPoll::Current(request)
        }
        Ok(_) | Err(_) => ActiveCaptureRequestPoll::Unavailable,
    }
}
