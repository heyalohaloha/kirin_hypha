// Included once by PluginProcessorLiveCompare.cpp.
// MATCH on/off (INV-LC13, 2026-11-12). Message thread. Off returns PRE to its own level (50 ms ramp
// while PRE sounds) and POST to the level it had when this LISTEN began, rising as END and RETURN do;
// AUTO rests and BLIND does not count the MATCH. On restores what was on: the same gains, the same
// ceiling and TP LIMIT, and the same approval, lowering POST again over 50 ms when MATCH lowered it.
// A new MATCH, END, a new LISTEN, a restore and a pair change forget it.
bool KirinHyphaProcessorBase::setLiveCompareMatchOn (bool on)
{
    using namespace hypha::live_compare;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)
        || ! liveCompare.authority.permitted() || liveCompare.completion.pending()
        || liveCompare.blind.command().active() || liveCompare.blindStage != BlindStage::idle
        || on != liveCompare.matchOff.load (std::memory_order_acquire))
        return false;
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0)
        return false;
    const auto identity = gainIdentity (*mapping->ring(), liveCompare.authority.ticket());
    const auto gains = readGainSnapshot (liveCompare);
    auto& memory = liveCompare.matchMemory;
    if (on ? ! memory.identity.same (identity)
           : ! gains.coherent || ! gains.retained || ! gains.identity.same (identity))
        return false;
    GainUpdate update (liveCompare.gainRevision, gains.revision);
    if (! update)
        return false; // another writer landed since the snapshot: press again
    if (! on)
    {
        memory = { gains.pre, gains.post, gains.ceiling, gains.limited,
                   liveCompare.matched.load (std::memory_order_acquire),
                   liveCompare.matchRun.load (std::memory_order_acquire),
                   liveCompare.matchGeneration.load (std::memory_order_acquire), identity };
        liveCompare.matched.store (false, std::memory_order_release);
        liveCompare.matchRetained.store (false, std::memory_order_release);
        liveCompare.matchLimited.store (false, std::memory_order_release);
        liveCompare.gain.store (1.0f, std::memory_order_release);
        liveCompare.postTarget.store (std::max (gains.post, liveCompare.listenBasePost), std::memory_order_release);
        liveCompare.matchOff.store (true, std::memory_order_release);
        return true;
    }
    liveCompare.gainApproval.bind (identity);
    liveCompare.ceilingLinear.store (memory.ceiling, std::memory_order_release);
    liveCompare.gain.store (memory.pre, std::memory_order_release);
    liveCompare.postTarget.store (std::min (memory.post, liveCompare.postTarget.load (std::memory_order_acquire)),
                                  std::memory_order_release);
    liveCompare.matchRun.store (memory.run, std::memory_order_release);
    liveCompare.matchGeneration.store (memory.generation, std::memory_order_release);
    liveCompare.matchLimited.store (memory.limited, std::memory_order_release);
    liveCompare.matched.store (memory.matched, std::memory_order_release);
    liveCompare.matchRetained.store (true, std::memory_order_release);
    liveCompare.matchOff.store (false, std::memory_order_release);
    return true;
}
