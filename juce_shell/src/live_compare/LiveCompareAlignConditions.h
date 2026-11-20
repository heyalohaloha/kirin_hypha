#pragma once
#include "LiveCompareRing.h"

namespace hypha::live_compare
{
// The last valid host identities survive gaps and repeated observations. This is only an
// invalidation witness, never a PCM or timing permission.
class AlignmentConditions
{
    struct Side
    {
        bool presentationKnown = false, chainKnown = false;
        std::uint8_t presentationSource = 0;
        std::uint32_t presentation = 0, delay = 0;
        std::uint64_t chainA = 0, chainB = 0;
        bool observe (const BlockClock& block) noexcept
        {
            bool changed = false;
            if (block.outputPresentationValid)
            {
                changed = changed || (presentationKnown && (presentationSource != block.presentationSource
                    || presentation != block.outputPresentationSamples));
                presentationKnown = true; presentationSource = block.presentationSource;
                presentation = block.outputPresentationSamples;
            }
            if ((block.runtimeChainA != 0 || block.runtimeChainB != 0) && !block.runtimeExpired)
            {
                changed = changed || (chainKnown && (chainA != block.runtimeChainA
                    || chainB != block.runtimeChainB || delay != block.maximumDelaySamples));
                chainKnown = true; chainA = block.runtimeChainA; chainB = block.runtimeChainB;
                delay = block.maximumDelaySamples;
            }
            return changed;
        }
    };
public:
    bool observe (std::uint64_t token, const Ring& ring, const BlockClock& post,
                  bool compensationChanged) noexcept
    {
        if (token != session) { *this = {}; session = token; }
        bool changed = compensationChanged;
        if (post.playing && post.clockValid) changed = postSide.observe (post) || changed;
        TimingSnapshot peer;
        if (readTiming (ring.header.timing, peer) && peer.active)
        {
            if (ownerKnown && (ownerA != peer.ownerA || ownerB != peer.ownerB)) changed = true;
            ownerKnown = true; ownerA = peer.ownerA; ownerB = peer.ownerB;
            changed = preSide.observe (peer.block) || changed;
        }
        return changed;
    }
    // The conditions a kept A/B correction was confirmed under (INV-LC28): equal or not, nothing more.
    std::uint64_t fingerprint() const noexcept
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash] (std::uint64_t value) noexcept { hash = (hash ^ value) * 1099511628211ull; };
        for (const auto* side : { &preSide, &postSide })
        {
            mix (side->presentationKnown); mix (side->presentationSource); mix (side->presentation);
            mix (side->chainKnown); mix (side->chainA); mix (side->chainB); mix (side->delay);
        }
        return hash;
    }
private:
    std::uint64_t session = 0, ownerA = 0, ownerB = 0;
    bool ownerKnown = false;
    Side preSide, postSide;
};
}
