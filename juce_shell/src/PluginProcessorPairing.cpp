#include "PluginProcessor.h"

#include "kirin_hypha_pair_snapshot_ffi.h"

bool KirinHyphaProcessorBase::localBlindPairBinding (
    hypha::local_blind::ExactPairBinding& out) const
{
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Post || hyphaHandle == nullptr)
        return false;

    KirinExactPairBinding binding {};
    if (! kirin_hypha_get_local_blind_pair_binding (hyphaHandle, &binding))
        return false;

    hypha::local_blind::ExactPairBinding decoded {
        binding.pair_generation,
        std::string (binding.project_hash),
        std::string (binding.pre_instance_id)
    };
    if (! decoded.valid())
        return false;
    out = std::move (decoded);
    return true;
}
