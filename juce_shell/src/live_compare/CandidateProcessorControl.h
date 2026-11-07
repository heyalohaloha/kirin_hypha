// Included once by PluginProcessorLiveCompare.cpp. Non-RT operations stay on the message thread.
juce::String KirinHyphaProcessorBase::liveComparePreInstance() const
{
    const auto& selected = liveCompare.candidate.selectedPre;
    return selected.empty() ? pairedPreInstanceId() : juce::String (selected);
}

hypha::live_compare::StartResult KirinHyphaProcessorBase::startCandidateCompare (
    const juce::String& pre2, bool placementConfirmed, std::optional<std::uint64_t> pressedTicket)
{
    using namespace hypha::live_compare;
    const juce::ScopedLock lock (automaticPair.lock);
    const auto permission = pressedTicket.value_or (liveCompare.authority.ticket());
    if (role != Role::Post) return StartResult::notPost;
    if (! placementConfirmed || pre2.isEmpty()) return StartResult::notReady;
    if (candidateReturning()) return StartResult::returnPending;
    if (candidateMeasurementIsolated() || isRecording() || keepPhase() != KIRIN_KEEP_PHASE_IDLE)
        return StartResult::comparisonBusy;
    const auto pre1 = pairedPreInstanceId();
    if (pre1.isEmpty()) return StartResult::noPair;
    if (pre1 == pre2 || pre2 == instanceId()) return StartResult::notReady;
    // Resolve both live identities before granting replacement; this does not prove placement.
    auto observer = std::make_unique<CandidateReturnObserver>();
    if (! observer->prepare (pairKeyForPreInstance (pre1.toStdString()),
                            static_cast<std::uint32_t> (preparedFormat.sampleRate)))
        return StartResult::preUnavailable;
    const auto& original = observer->source;
    if ((original.ring()->header.source.load (std::memory_order_acquire) & ringSourceMultiMono) != 0)
        return StartResult::preMultiMono;
    auto& state = liveCompare.candidate;
    state.selectedPre = pre2.toStdString(); state.originalPre = pre1.toStdString();
    const auto result = startLiveCompare (permission);
    if (result != StartResult::started) { state.selectedPre.clear(); state.originalPre.clear(); return result; }
    auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr)
    { stopLiveCompare(); state.selectedPre.clear(); state.originalPre.clear(); return StartResult::notReady; }
    std::uint64_t scope = 0;
    bool admitted = false;
    {
        const juce::ScopedLock handleGuard (handleLock);
        admitted = hyphaHandle && kirin_hypha_begin_local_blind (hyphaHandle, &scope);
    }
    if (! admitted)
    {
        stopLiveCompare(); state.selectedPre.clear(); state.originalPre.clear();
        return StartResult::comparisonBusy;
    }
    auto& transfer = mapping->ring()->header.candidate;
    const auto token = pairKeyForPreInstance (juce::Uuid().toString().toStdString()) | 1u;
    if (liveCompare.authority.ticket() != permission || ! liveCompare.authority.permitted())
    {
        releaseLocalBlindProductScope (scope);
        stopLiveCompare(); state.selectedPre.clear(); state.originalPre.clear();
        return StartResult::notReady;
    }
    state.returnObserver.retire();
    if (!state.returnObserver.collect() || !state.returnObserver.publish (std::move (observer)))
    {
        releaseLocalBlindProductScope (scope);
        stopLiveCompare(); state.selectedPre.clear(); state.originalPre.clear(); return StartResult::notReady;
    }
    state.scope = scope; // existing project exclusion, reused by BLIND without declaring a trial
    state.isolated.store (true, std::memory_order_release);
    state.returning.store (false, std::memory_order_release);
    state.quietFrames.store (0, std::memory_order_release);
    state.token.store (token, std::memory_order_release);
    if (! transfer.begin (token, original.key(), original.ring()->header.timing.ownerA.load(),
            original.ring()->header.timing.ownerB.load(), callbackWallNanos()))
    {
        state.scope = 0; state.token.store (0); state.isolated.store (false);
        state.returnObserver.retire(); state.returnObserver.collect();
        releaseLocalBlindProductScope (scope);
        stopLiveCompare(); state.selectedPre.clear(); state.originalPre.clear();
        return StartResult::comparisonBusy;
    }
    if (liveCompare.authority.ticket() != permission || ! liveCompare.authority.permitted())
    { stopLiveCompare(); return StartResult::notReady; }
    const auto track = state.trackHash.load (std::memory_order_acquire);
    const auto differs = [track] (const Ring& ring)
    {
        const auto other = ring.header.candidate.track.load (std::memory_order_acquire);
        return track != 0 && other != 0 && track != other;
    };
    state.trackMismatch = differs (*mapping->ring()) || differs (*original.ring());
    liveCompare.preparation.service (mapping->key(), mapping->rate(), liveCompare.authority.ticket());
    return StartResult::started;
}

void KirinHyphaProcessorBase::endCandidateCompare() noexcept
{
    auto& state = liveCompare.candidate;
    const auto token = state.token.load (std::memory_order_acquire);
    if (token == 0 || state.returning.exchange (true, std::memory_order_acq_rel)) return;
    if (auto* mapping = liveCompare.ring.control(); mapping != nullptr && mapping->ring() != nullptr)
    {
        auto& transfer = mapping->ring()->header.candidate;
        transfer.released.store (token, std::memory_order_release);
        transfer.refreshLease (token, 0);
    }
    state.quietFrames.store (0, std::memory_order_release);
}

void KirinHyphaProcessorBase::stopCandidateReceiver()
{
    auto& slot = liveCompare.candidate.receiver;
    if (!slot.withdraw()) return;
    if (role == Role::Pre)
        if (auto* own = liveCompare.ring.control(); own && own->ring())
        {
            auto& transfer = own->ring()->header.candidate;
            if (auto* receiver = slot.control())
                transfer.acknowledgeReturn (receiver->token, receiver->lastOutput, receiver->lastGeneration);
            else if (!slot.hasStorage())
            {
                const auto token = transfer.owner.load (std::memory_order_acquire);
                if (token != 0 && !transfer.withdrawn (token))
                    transfer.acknowledgeReturn (token, {});
            }
        }
    slot.retire();
    slot.collect();
}

void KirinHyphaProcessorBase::serviceCandidateCompare()
{
    using namespace hypha::live_compare;
    auto& state = liveCompare.candidate;
    if (role == Role::Post)
    {
        state.returnObserver.collect();
        if (state.token.load() == 0) return;
        if (state.originalPre != pairedPreInstanceId().toStdString() || ! liveCompare.authority.permitted()) endCandidateCompare();
        if (! state.returning.load())
            if (const auto* mapping = liveCompare.ring.control(); mapping && mapping->ring())
            {
                const auto& request = mapping->ring()->header.candidate;
                if (request.owner.load() != state.token.load() || request.released.load() == state.token.load())
                    stopLiveCompare (RecoveryReason::preUnavailable);
            }
        // Rebind only the drain observer after reprepare. Its new timing/marker starts empty;
        // the old PRE2 mapping remains solely as the withdrawn request's receipt.
        if (state.returning.load())
        {
            const auto rate = static_cast<std::uint32_t> (preparedFormat.sampleRate);
            const auto key = pairKeyForPreInstance (state.originalPre);
            const auto* observer = state.returnObserver.control();
            if (observer && !observer->source.ring()->matches (key, rate))
            {
                // Withdrawal is final: a marker already passed stays passed when its PRE goes.
                const bool passed = state.quietContext.load (std::memory_order_acquire) == state.returnContext.load()
                    && state.quietFrames.load() > 0;
                if (! passed) state.returnContext.fetch_add (1, std::memory_order_acq_rel);
                state.returnObserver.retire(); state.returnObserver.collect();
            }
            if (!state.returnObserver.hasStorage())
            {
                auto replacement = std::make_unique<CandidateReturnObserver>();
                if (replacement->prepare (key, rate)) state.returnObserver.publish (std::move (replacement));
            }
        }
        const auto* held = liveCompare.ring.control();
        const bool untouched = held && held->ring()
            && held->ring()->header.candidate.returnedUntouched (state.token.load());
        // Only a withdrawn, untouched request skips the downstream-history quarantine.
        if (state.returning.load() && (untouched
            || (state.quietContext.load (std::memory_order_acquire) == state.returnContext.load()
                && state.quietFrames.load() >= static_cast<std::uint64_t> (preparedFormat.sampleRate * 4.0))))
        {
            if (state.scope != 0 && ! releaseLocalBlindProductScope (state.scope)) return;
            if (liveCompare.blindScope == state.scope) liveCompare.blindScope = 0;
            state.scope = 0;
            state.returnObserver.retire(); state.returnObserver.collect();
            state.isolated.store (false, std::memory_order_release);
            state.token.store (0, std::memory_order_release);
            state.returning.store (false, std::memory_order_release);
            state.selectedPre.clear(); state.originalPre.clear(); state.trackMismatch = false;
        }
        return;
    }
    // A host may re-prepare on its own thread, under handleLock, while this timer runs. The
    // publication slot only excludes RT readers, so its non-RT owners share that one lock.
    const juce::ScopedTryLock ownership (handleLock);
    if (! ownership.isLocked()) return; // the prepare owns the receiver; the next tick continues
    auto* own = liveCompare.ring.control();
    if (own == nullptr || own->ring() == nullptr) { stopCandidateReceiver(); return; }
    auto& transfer = own->ring()->header.candidate;
    transfer.track.store (state.trackHash.load(), std::memory_order_release);
    const auto token = transfer.owner.load (std::memory_order_acquire);
    const auto source = transfer.source.load (std::memory_order_acquire);
    auto* current = state.receiver.control();
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (afterCandidateReceiverReadForTest) afterCandidateReceiverReadForTest();
   #endif
    // A restored PRE2 or a replaced PRE1 ends the old permission. A short clock/PCM gap does not.
    if (current != nullptr && current->token == token && (current->permission != liveCompare.authority.ticket()
        || ! current->source.ring()->matches (source, own->rate())))
    {
        transfer.released.store (token, std::memory_order_release);
        transfer.refreshLease (token, 0);
    }
    std::uint64_t leaseUntil = 0;
    const auto lease = transfer.readLease (token, callbackWallNanos(), leaseUntil);
    if (lease == CandidateTransfer::Lease::updating && current != nullptr && current->token == token
        && current->permission == liveCompare.authority.ticket()
        && current->source.key() == source && ! liveCompare.authority.restoring()
        && ! isRecording() && keepPhase() == KIRIN_KEEP_PHASE_IDLE
        && state.hostActive.load() && transfer.released.load() != token) return;
    const bool desired = transfer.released.load (std::memory_order_acquire) != token && lease == CandidateTransfer::Lease::valid && source != 0 && source != own->key()
        && state.hostActive.load() && ! liveCompare.authority.restoring() && ! isRecording() && keepPhase() == KIRIN_KEEP_PHASE_IDLE;
    if (current != nullptr && desired && current->token == token && current->source.key() == source
        && current->permission == liveCompare.authority.ticket() && current->source.ring()->matches (source, own->rate())
        && current->completion.command() == 0 && state.receiver.hasPublishedRealtime()) return;
    // A previous block's receipt cannot retire an in-flight first fade. Reuse LIVE's END
    // command/receipt and keep the receiver published until RT has completed that command.
    if (current != nullptr)
    {
        const bool completed = current->requestReturn() || !state.receiver.hasPublishedRealtime();
        const auto last = own->ring()->header.timing.wallNanos.load (std::memory_order_acquire);
        const auto now = callbackWallNanos();
        const bool inactive = !state.hostActive.load (std::memory_order_acquire)
            || (last != 0 && now > last && now - last > CandidateTransfer::leaseNanos);
        if (!completed && !inactive) return;
        // Withdrawal prevents a late callback from entering; existing readers must exit before
        // the final output position is read or a non-RT return receipt is published.
        if (!state.receiver.withdraw()) return;
        transfer.acknowledgeReturn (current->token, current->lastOutput, current->lastGeneration);
    }
    else if (!desired && !state.receiver.hasStorage())
    {
        const auto ended = token != 0 ? token : transfer.released.load (std::memory_order_acquire);
        TimingSnapshot last;
        if (ended != 0 && !transfer.withdrawn (ended))
        {
            readTiming (own->ring()->header.timing, last);
            transfer.acknowledgeReturn (ended, last.block, last.generation);
        }
    }
    state.receiver.retire();
    if (! state.receiver.collect()) return;
    if (! desired && transfer.released.load (std::memory_order_acquire) == token
        && ! transfer.replacing.load (std::memory_order_acquire))
    {
        auto expired = token;
        transfer.owner.compare_exchange_strong (expired, 0, std::memory_order_acq_rel);
    }
    if (! desired) return;
    auto receiver = std::make_unique<CandidateReceiver>();
    if (receiver->prepare (source, token, liveCompare.authority.ticket(), own->rate(), juce::jmax (getBlockSize(), 16384),
        transfer.sourceOwnerA.load(), transfer.sourceOwnerB.load()))
    {
        transfer.revokeReturn (token);
        state.receiver.publish (std::move (receiver));
    }
    else if (const auto* input = receiver->source.ring(); input != nullptr
        && (input->header.timing.ownerA.load() != transfer.sourceOwnerA.load()
            || input->header.timing.ownerB.load() != transfer.sourceOwnerB.load()))
    {
        // A different owner is conclusive even before the first receiver was prepared.
        // A missing mapping alone remains a transient wait, with the real input passing through.
        transfer.released.store (token, std::memory_order_release);
        transfer.refreshLease (token, 0);
    }
}

void KirinHyphaProcessorBase::processCandidateReceiver (hypha::live_compare::Ring& own,
    const hypha::live_compare::BlockClock& block, juce::AudioBuffer<float>& buffer, bool usable, bool offline) noexcept
{
    own.header.candidate.bypassed.store (bypassParam != nullptr && bypassParam->get(), std::memory_order_release);
    liveCompare.candidate.receiver.withRealtime ([&] (hypha::live_compare::CandidateReceiver& receiver)
    {
        receiver.process (own, block, buffer.getArrayOfWritePointers(), buffer.getNumChannels(), usable,
            ! liveCompare.authority.restoring() && receiver.permission == liveCompare.authority.ticket(),
            liveCompare.compensationOff.load (std::memory_order_acquire), liveCompare.compensationRevision.load (std::memory_order_acquire), offline);
    });
}

bool KirinHyphaProcessorBase::candidateBypassed() const noexcept
{
    const auto* mapping = liveCompare.ring.control();
    return candidateCompareActive() && mapping && mapping->ring()
        && mapping->ring()->header.candidate.bypassed.load (std::memory_order_acquire);
}

bool KirinHyphaProcessorBase::candidateHostInactive() const noexcept
{
    const auto* mapping = liveCompare.ring.control();
    return candidateCompareActive() && mapping && mapping->ring()
        && mapping->ring()->header.candidate.hostInactive.load (std::memory_order_acquire);
}
