// Included once by PluginProcessorLiveCompare.cpp.
// MATCH on/off (INV-LC13, 2026-11-12). Message thread. Off returns PRE to its own level (50 ms ramp
// while PRE sounds) and POST to the level it had when this LISTEN began, rising as END and RETURN do;
// AUTO rests and BLIND does not count the MATCH. On restores what was on: the same gains, ceiling, TP
// LIMIT and approval, lowering POST again over 50 ms when MATCH lowered it. PRE may have changed while
// MATCH was off, so a kept boost is checked against PRE's true peak now (the latest window, as MATCH and
// AUTO measure it) and capped at the approved ceiling; the result says how much was left out. Without
// AUTO (following) a capped boost reads TP LIMIT. On is current for this playback, so AUTO resumes.
// A new MATCH, END, a new LISTEN, a restore and a pair change forget it.
hypha::live_compare::MatchToggle KirinHyphaProcessorBase::setLiveCompareMatchOn (bool on, bool following)
{
    using namespace hypha::live_compare;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)
        || ! liveCompare.authority.permitted() || liveCompare.completion.pending()
        || liveCompare.blind.command().active() || liveCompare.blindStage != BlindStage::idle
        || on != liveCompare.matchOff.load (std::memory_order_acquire))
        return {};
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0)
        return {};
    const auto identity = gainIdentity (*mapping->ring(), liveCompare.authority.ticket());
    auto& memory = liveCompare.matchMemory;
    const auto decibels = [] (float linear) { return 20.0 * std::log10 (static_cast<double> (linear)); };
    MatchToggle result;
    if (on)
    {
        if (! memory.identity.same (identity)) return {};
        const auto now = measureLiveCompare(); // before the lease: it reads only the history
        if (now.ok() && std::isfinite (now.prePeakDbtp)) memory.peakDbtp = std::max (memory.peakDbtp, now.prePeakDbtp);
        const double kept = decibels (memory.pre);
        result.gainDb = ceilingCappedGainDb (kept, decibels (memory.ceiling), memory.peakDbtp);
        result.shortDb = kept - result.gainDb;
        result.peakDbtp = memory.peakDbtp;
    }
    const auto gains = readGainSnapshot (liveCompare);
    if (! on && (! gains.coherent || ! gains.retained || ! gains.identity.same (identity)))
        return {};
    GainUpdate update (liveCompare.gainRevision, gains.revision);
    if (! update)
        return {}; // another writer landed since the snapshot: press again
    result.turned = true;
    if (! on)
    {
        memory = { gains.pre, gains.post, gains.ceiling, gains.limited, liveCompare.matchPeakDbtp, identity };
        liveCompare.matched.store (false, std::memory_order_release);
        liveCompare.matchRetained.store (false, std::memory_order_release);
        liveCompare.matchLimited.store (false, std::memory_order_release);
        liveCompare.gain.store (1.0f, std::memory_order_release);
        liveCompare.postTarget.store (std::max (gains.post, liveCompare.listenBasePost), std::memory_order_release);
        liveCompare.matchOff.store (true, std::memory_order_release);
        return result;
    }
    liveCompare.matchPeakDbtp = memory.peakDbtp;
    liveCompare.gainApproval.bind (identity);
    liveCompare.ceilingLinear.store (memory.ceiling, std::memory_order_release);
    liveCompare.gain.store (result.shortDb > 0.0 ? static_cast<float> (std::pow (10.0, result.gainDb / 20.0)) : memory.pre,
                            std::memory_order_release);
    liveCompare.postTarget.store (std::min (memory.post, gains.post), std::memory_order_release);
    liveCompare.matchRun.store (liveCompare.playbackRun.load (std::memory_order_acquire), std::memory_order_release);
    liveCompare.matchGeneration.store (liveCompare.sessionGeneration.load (std::memory_order_acquire), std::memory_order_release);
    liveCompare.matchLimited.store (memory.limited || (! following && result.shortDb > 0.0), std::memory_order_release);
    liveCompare.matched.store (true, std::memory_order_release);
    liveCompare.matchRetained.store (true, std::memory_order_release);
    liveCompare.matchOff.store (false, std::memory_order_release);
    return result;
}
