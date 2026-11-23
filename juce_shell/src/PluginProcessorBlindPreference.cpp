#include "PluginProcessor.h"
#include "HyphaLiveBlindMemory.h"
#include <cmath>
#include "reference_audition/ReferenceBlindSession.h"

using namespace hypha::live_compare;

std::shared_ptr<hypha::live_blind_ui::TrialMemory> KirinHyphaProcessorBase::liveBlindTrialMemory()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (liveCompare.blindTrialMemory == nullptr)
        liveCompare.blindTrialMemory = std::make_shared<hypha::live_blind_ui::TrialMemory>();
    return liveCompare.blindTrialMemory;
}

void KirinHyphaProcessorBase::forgetBlindTrial()
{
    if (liveCompare.blindTrialMemory != nullptr) *liveCompare.blindTrialMemory = {};
}

std::optional<bool> KirinHyphaProcessorBase::acceptLiveBlindPreference (
    int source, std::uint64_t generation, std::uint32_t epoch)
{
    if (role != Role::Post || generation != liveCompare.blindPreparation
        || !liveCompare.authority.permitted() || liveCompare.blindStage != BlindStage::active
        || !currentBlindReceipt (liveCompare)) return std::nullopt;
    return liveCompare.blind.acceptPreference (source, epoch);
}

bool KirinHyphaProcessorBase::nextLiveBlindPreferenceRound (std::uint64_t generation, std::uint32_t epoch)
{
    if (role != Role::Post || generation != liveCompare.blindPreparation
        || !liveCompare.authority.permitted() || liveCompare.blindStage != BlindStage::active
        || !currentBlindReceipt (liveCompare)) return false;
    const auto result = liveCompare.blind.nextPreferenceWith (epoch, hypha::reference_audition::secureRandomBit);
    if (result == BlindSession::NextPreference::randomUnavailable)
    {
        liveCompare.blindPreparationReason = RecoveryReason::randomUnavailable;
        liveCompare.blindStage = BlindStage::invalidated;
        stopLiveCompare (RecoveryReason::randomUnavailable);
        return false;
    }
    if (result != BlindSession::NextPreference::started) return false;
    // No gain, approval, ceiling, attenuation or named selection changes here. A tuple the audio thread commits
    // meanwhile keeps the trial (its receipt follows); only a real loss ends the assignment just made.
    if (!liveCompare.authority.permitted() || blindReceipt (liveCompare) == BlindReceipt::lost)
    { liveCompare.blind.end(); return false; }
    requestServiceTimer();
    return true;
}

bool KirinHyphaProcessorBase::revealLiveBlindPreference (std::uint64_t generation, std::uint32_t epoch)
{
    if (role != Role::Post || generation != liveCompare.blindPreparation
        || ! liveCompare.authority.permitted() || liveCompare.blindStage != BlindStage::active
        || ! currentBlindReceipt (liveCompare)) return false;
    const auto notice = liveBlindStatus().contentNoticeMs;
    if (! liveCompare.blind.revealPreference (epoch)) return false;
    liveCompare.blindContentNoticeMs = notice; // RESULT keeps the estimate it was revealed with
    return true;
}

// One coherent approved tuple, not separately sampled display gain/POST targets.
// Call at trial entry and again before showing retained Details; any invalidation
// returns empty rather than reconstructing the historical approval from UI values.
std::optional<GainSnapshot> KirinHyphaProcessorBase::liveBlindApprovedPreferenceGain (
    std::uint64_t generation, std::uint32_t epoch) const
{
    const juce::ScopedTryLock ownership (handleLock);
    if (!ownership.isLocked() || role != Role::Post || generation != liveCompare.blindPreparation
        || liveCompare.blindStage != BlindStage::active || !liveCompare.authority.permitted()) return std::nullopt;
    const auto command = liveCompare.blind.command();
    if (command.epoch() != epoch || !liveCompare.blind.valid (command)) return std::nullopt;
    const auto* mapping = liveCompare.ring.control();
    const auto* ring = mapping != nullptr ? mapping->ring() : nullptr;
    if (ring == nullptr) return std::nullopt;
    const auto gain = readGainSnapshot (liveCompare);
    if (!gain.coherent || !gain.retained || gain.limited || !currentBlindReceipt (liveCompare)
        || !gain.identity.same (gainIdentity (*ring, liveCompare.authority.ticket()))
        || !std::isfinite (gain.pre) || gain.pre <= 0 || !std::isfinite (gain.post) || gain.post <= 0
        || !std::isfinite (gain.ceiling) || gain.ceiling <= 0) return std::nullopt;
    if (gain.revision != liveCompare.gainRevision.load (std::memory_order_acquire)
        || gain.revision != liveCompare.blindGainRevision.load (std::memory_order_acquire)
        || !gain.identity.same (gainIdentity (*ring, liveCompare.authority.ticket()))
        || !liveCompare.authority.permitted() || !liveCompare.blind.valid (command)
        || liveCompare.blind.command().epoch() != epoch) return std::nullopt;
    return gain;
}
