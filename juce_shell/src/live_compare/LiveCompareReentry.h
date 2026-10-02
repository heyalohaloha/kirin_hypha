#pragma once
#include "LiveCompareRecovery.h"

namespace hypha::live_compare
{
// Audio Thread only. New timing admission is not new output authority or a new MATCH.
// Only named comparison can renew after an identified transport change. Any unexplained
// loss seals the sampled selection; a later explicit PRE command owns its own admission.
class NamedReentry
{
public:
    bool request (SelectionCommand command, bool eligible, bool needsAdmission) noexcept
    {
        const bool explicitPre = command.word != previousCommand && command.pre();
        previousCommand = command.word;
        if (! eligible) { pending = renewing = false; return false; }
        if (! needsAdmission) { pending = false; renewing = renewing || explicitPre; return false; }
        if (! pending && ! explicitPre) return false;
        pending = false;
        renewing = true;
        return true;
    }
    void lost (TimelineBreak cause, bool namedEligible) noexcept
    {
        if (cause == TimelineBreak::metadataPending && namedEligible) return;
        renewing = false;
        pending = namedEligible && namedTransportRestart (cause);
    }
    void explicitlyRenew() noexcept { pending = false; renewing = true; }
    bool accepted (bool approvalReady = true) noexcept
    {
        const bool result = renewing && approvalReady;
        if (approvalReady) renewing = false;
        return result;
    }
    bool waiting() const noexcept { return pending || renewing; }
private:
    std::uint64_t previousCommand = 0;
    bool pending = false, renewing = false;
};
}
