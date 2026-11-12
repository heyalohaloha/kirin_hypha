// Included once by PluginProcessorLiveCompare.cpp.
hypha::live_compare::Status KirinHyphaProcessorBase::liveCompareStatus() const noexcept
{
    hypha::live_compare::Status status;
    status.finishing = liveCompare.completion.pending();
    status.prolongedWait = liveCompare.waitNotice.prolonged (hypha::live_compare::callbackWallNanos());
    status.signalWait = liveCompare.namedMatchPending
        && (liveCompare.namedMatchWaiting == hypha::live_compare::MatchFailure::tooShort
            || liveCompare.namedMatchWaiting == hypha::live_compare::MatchFailure::notEnoughSignal)
        && hypha::live_compare::guidanceDue (liveCompare.namedMatchSince, hypha::live_compare::callbackWallNanos());
    status.postActual = liveCompare.postActual.load (std::memory_order_acquire);
    status.sessionGeneration = liveCompare.sessionGeneration.load (std::memory_order_acquire);
    const auto permission = liveCompare.authority.ticket();
    const bool permitted = liveCompare.authority.permitted();
    const auto gains = hypha::live_compare::readGainSnapshot (liveCompare);
    status.matched = permitted && gains.coherent && gains.retained
        && liveCompare.matched.load (std::memory_order_acquire)
        && liveCompare.matchGeneration.load (std::memory_order_acquire) == status.sessionGeneration
        && liveCompare.matchRun.load (std::memory_order_acquire) == liveCompare.playbackRun.load (std::memory_order_acquire);
    status.matchHeld = permitted && gains.coherent && gains.retained && ! status.matched;
    status.matchLimited = gains.limited;
    status.matchReady = permitted && hypha::live_compare::currentGainReceipt (liveCompare, gains);
    // Logical ownership ends at END; the independent ring lease outlives it until RT receipt.
    status.active = ! status.finishing && permitted && liveCompare.sessionActive.load (std::memory_order_acquire);
    const auto selection = liveCompare.selection.command();
    status.preSelected = permitted && selection.pre();
    status.preAudible = permitted && liveCompare.preAudible.load (std::memory_order_acquire);
    status.preWaiting = permitted && liveCompare.preWaiting.load (std::memory_order_acquire);
    status.interrupted = selection.reason() != hypha::live_compare::RecoveryReason::none;
    status.verdict = static_cast<hypha::live_compare::Verdict> (liveCompare.verdict.load (std::memory_order_acquire));
    status.matchReady = status.matchReady && status.verdict == hypha::live_compare::Verdict::accepted;
    status.gain = gains.pre;
    status.postTarget = gains.coherent ? gains.post : status.postActual;
    status.contentHeld = liveCompare.contentHold.held();
    if (status.contentHeld)
        status.contentJumpLagFrames = liveCompare.contentJumpLagFrames.load (std::memory_order_relaxed);
    status.compensationOff = liveCompare.compensationOff.load (std::memory_order_acquire);
    using Reason = hypha::live_compare::RecoveryReason;
    status.reason = liveCompare.blindStage == hypha::live_compare::BlindStage::idle
        ? selection.reason() : liveCompare.blind.view().reason;
    if (status.reason == Reason::none && liveCompare.blindStage != hypha::live_compare::BlindStage::idle)
        status.reason = liveCompare.blindPreparationReason;
    if (! permitted && status.reason == Reason::none)
        status.reason = liveCompare.authority.generation() == liveCompare.pairRevocationGeneration.load (std::memory_order_acquire)
            ? Reason::pairChanged : Reason::restored;
    status.observation = liveCompare.observationReason.load (std::memory_order_acquire);
    status.timingUnavailable = (status.observation == Reason::loopTooShort || status.observation == Reason::loopClockUnavailable
            || status.observation == Reason::reaperDelayUnverified || status.observation == Reason::reaperLoopTooShort);
    status.loopBlock = analysisPairingLoopBlock();
    status.reaperRestriction = nativeHostContext.reaper.restriction();
    // A missing audio clock on a host that has never supplied a continuous clock or an
    // output-latency report is this host's ability, not a transient: stop/play cannot fix it.
    status.clockUnavailable = status.observation == Reason::clockMissing
        && ! liveCompare.clockSeen.load (std::memory_order_acquire)
        && ! liveCompare.outputLatencySeen.load (std::memory_order_acquire);
    status.timingReentryPending = liveCompare.timingReentryPending.load (std::memory_order_acquire);
    std::atomic_thread_fence (std::memory_order_acquire);
    if (gains.revision != liveCompare.gainRevision.load (std::memory_order_acquire))
        status.matched = status.matchReady = status.matchHeld = false;
    if (permission != liveCompare.authority.ticket() || ! liveCompare.authority.permitted())
        status.active = status.preSelected = status.preAudible = status.matched = status.matchReady = status.matchHeld = false;
    return status;
}
