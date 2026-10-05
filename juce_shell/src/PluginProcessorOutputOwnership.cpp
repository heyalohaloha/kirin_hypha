#include "PluginProcessor.h"

// POST の出力を取る活動の入口は、どれも outputDecision（OutputOwnership.h の表）で決める。状態は、Reference の外は
// processor が、Reference の中（Blind の枠・VERSION BLIND・下げた A・鳴っている役）は controller が集める。
using namespace hypha::output_owner;

States KirinHyphaProcessorBase::externalOutputStates() const
{
    States states = 0;
    if (! stereoWorkflowsSupported()) states |= bit (State::layout);
    if (isNonRealtime()) states |= bit (State::offline);
    if (bypassParam != nullptr && bypassParam->get()) states |= bit (State::bypass);
    if (role == Role::Post)
    {
        states |= liveStates ({ liveCompare.sessionActive.load (std::memory_order_acquire) && liveCompare.authority.permitted(),
                                liveCompare.authority.restoring() || liveCompare.authority.generation() != liveCompare.restoreServiced,
                                liveCompare.completion.pending(), liveCompare.blindScope != 0,
                                liveCompare.postActual.load (std::memory_order_acquire),
                                liveCompare.postTarget.load (std::memory_order_acquire) });
        using Phase = hypha::local_blind::ProductSessionPhase;
        const auto local = localBlindProductSession.view().phase;
        if (local != Phase::idle && local != Phase::returned && local != Phase::failed) states |= bit (State::localBlind);
    }
    if (isRecording() || keepPhase() != KIRIN_KEEP_PHASE_IDLE) states |= bit (State::recordKeep);
    return states;
}

States KirinHyphaProcessorBase::outputStates() const
{
    auto states = externalOutputStates();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController != nullptr) states |= referenceAuditionController->ownOutputStates();
   #endif
    return states;
}

Decision KirinHyphaProcessorBase::outputDecision (Activity activity) const
{
    return decide (activity, outputStates());
}

hypha::live_compare::StartResult KirinHyphaProcessorBase::liveCompareAdmission (bool reuseSession) const noexcept
{
    return hypha::live_compare::startResultFor (outputDecision (reuseSession ? Activity::liveBlind : Activity::liveCompare));
}
