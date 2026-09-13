#include "ReferenceRuntimeV2Controller.h"
#include <algorithm>

namespace hypha::reference_audition
{
bool RuntimeV2Controller::selectLibraryCheck (const juce::String& id)
{
    const auto split = id.indexOfChar ('/');
    if (split < 0 || blind.ongoing()) return false;
    {
        const juce::ScopedLock lock (stateLock);
        if (! requestedConfiguration.identity.library
            || std::none_of (currentSnapshot.checkTargets.begin(), currentSnapshot.checkTargets.end(),
                            [&] (const auto& option) { return option.id == id; })) return false;
        requestedSelection.checkId = id.substring (0, split);
        requestedSelection.candidateId = id.substring (split + 1);
        requestedSelection.cueId.clear();
        requestedSelection.sampleRateApprovalKey.clear();
        ++requestedSelection.generation;
        pendingApprovalKey.clear();
        currentSnapshot.sampleRateApprovalRequired = false;
        revokeAuditionPublication();
    }
    selectA(); notify(); return true;
}

bool RuntimeV2Controller::selectLibraryVersion (const juce::String& id)
{
    const auto parts = juce::StringArray::fromTokens (id, "/", {});
    if (parts.size() != 3 || blind.ongoing()) return false;
    {
        const juce::ScopedLock lock (stateLock);
        if (! requestedConfiguration.identity.library
            || std::none_of (currentSnapshot.versions.begin(), currentSnapshot.versions.end(),
                            [&] (const auto& option) { return option.id == id; })) return false;
        requestedSelection.presetId = parts[0];
        requestedSelection.checkId = parts[1];
        requestedSelection.candidateId = parts[2];
        requestedSelection.cueId.clear();
        requestedSelection.sampleRateApprovalKey.clear();
        ++requestedSelection.generation;
        pendingApprovalKey.clear();
        currentSnapshot.sampleRateApprovalRequired = false;
        revokeAuditionPublication();
    }
    selectA();
    notify();
    return true;
}
ReferenceChoice RuntimeV2Controller::savedChoice() const
{
    const juce::ScopedLock lock (stateLock);
    return { requestedSelection.presetId, requestedSelection.checkId,
             requestedSelection.candidateId, requestedSelection.cueId };
}

void RuntimeV2Controller::restoreChoice (const ReferenceChoice& value)
{
    selectA();
    {
        const juce::ScopedLock lock (stateLock);
        const auto choice = value.valid() ? value : ReferenceChoice {};
        const auto generation = requestedSelection.generation + 1;
        requestedSelection = { choice.presetId, choice.checkId, choice.candidateId,
                               choice.cueId, {}, generation };
        pendingApprovalKey.clear();
        currentSnapshot.sampleRateApprovalRequired = false;
        revokeAuditionPublication();
    }
    notify();
}

}
