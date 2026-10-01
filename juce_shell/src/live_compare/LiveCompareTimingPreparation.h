#pragma once
#include "../local_blind/RtPublicationSlot.h"
#include "LiveCompareSharedRing.h"
#include <atomic>
#include <memory>

namespace hypha::live_compare
{
// Clock preparation has no selection, MATCH, demand, renderer, or output permission. Mapping
// and retirement are message-thread work; the Audio Thread only observes a published peer.
class TimingPreparation
{
    struct Peer
    {
        SharedRingMapping mapping;
        TimingObserver observer;
        std::uint64_t authority = 0;
    };
public:
    void service (std::uint64_t pairKey, std::uint32_t rate, std::uint64_t authority)
    {
        const auto* current = peers.control();
        if (current != nullptr && current->authority == authority && current->mapping.key() == pairKey
            && current->mapping.rate() == rate && current->mapping.ring() != nullptr
            && current->mapping.ring()->header.ownerClosed.load (std::memory_order_acquire) == 0) return;
        peers.retire();
        if (! peers.collect() || pairKey == 0 || rate == 0 || (authority & 0xffffffffu) != 0) return;
        auto peer = std::make_unique<Peer>();
        if (! peer->mapping.open (pairKey, rate, false)
            || (peer->mapping.ring()->header.source.load (std::memory_order_acquire) & ringSourceMultiMono) != 0) return;
        peer->authority = authority;
        peers.publish (std::move (peer));
    }

    void retire() noexcept
    {
        initialRequested.store (false, std::memory_order_release);
        peers.retire();
        peers.collect();
    }

    TimingEvidence observe (const BlockClock& block, std::uint32_t rate,
                            std::uint64_t authority, bool eligible) noexcept
    {
        TimingEvidence evidence;
        peers.withRealtime ([&] (Peer& peer)
        {
            auto* ring = peer.mapping.ring();
            TimingSnapshot snapshot;
            if (! eligible || peer.authority != authority || ring == nullptr
                || ! ring->matches (peer.mapping.key(), rate)
                || ring->header.ownerClosed.load (std::memory_order_acquire) != 0)
            {
                peer.observer.reset();
                return;
            }
            if (! readTiming (ring->header.timing, snapshot))
            {
                evidence = peer.observer.unavailable (block, rate, ringCapacityFrames);
                return;
            }
            evidence = peer.observer.observe (snapshot, block, rate, ringCapacityFrames);
        });
        return evidence;
    }

    // Only a new, explicit session requests adoption. Losing a live proof does NOT request a
    // new adoption or resurrect a revoked Blind. The renderer checks the actual mapping too.
    std::atomic<bool> initialRequested { false };
private:
    local_blind::RtPublicationSlot<Peer> peers;
};
}
