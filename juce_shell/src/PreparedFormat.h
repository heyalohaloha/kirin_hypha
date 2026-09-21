#pragma once

#include "ChannelRoles.h"

#include <cmath>
#include <cstdint>
#include <vector>

/**
    What format the engine was built for, and what to do when the host asks for a different one.

    Two facts have to be kept apart. Whether a format change *can be applied now* is one question;
    whether it *happened* is another. Record owns the engine while it runs, so an incompatible
    re-prepare cannot be applied — but dropping it on the floor means the shell keeps measuring in
    the old format until the host happens to call prepareToPlay again, which it may never do
    (棚卸し §16.4). Holding the request keeps Stop authority with the user (B-334) without
    forgetting what was asked for.
*/
namespace kirin
{

/** The audio format an engine is actually bound to. Empty roles means no engine. */
struct PreparedFormat
{
    double sampleRate = 0.0;
    std::vector<uint8_t> channelRoles;

    /** Same rate and the same channels in the same order.

        Roles rather than a count: ten channels are 7.1.2 or 5.1.4, and eight are 7.1 or 5.1.2.
        A count-only comparison reuses an engine built for a different set of speakers, and every
        channel after the first divergence is then measured as something it is not.
    */
    bool matches (double rate, const std::vector<uint8_t>& roles) const noexcept
    {
        return ! channelRoles.empty() && std::abs (sampleRate - rate) <= 0.001
            && channelRoles == roles;
    }
};

/** What prepareToPlay should do about the format the host just negotiated. */
enum class PrepareAction
{
    reuse,       ///< Same format. Keep the engine and whatever Record is doing with it.
    rebuild,     ///< Different format and the engine is free. Replace it.
    holdForRecord ///< Different format while Record owns the engine. Remember, do not apply.
};

inline PrepareAction decidePrepare (bool hasEngine,
                                    bool isRecording,
                                    double sampleRate,
                                    const std::vector<uint8_t>& roles,
                                    const PreparedFormat& prepared) noexcept
{
    if (hasEngine && prepared.matches (sampleRate, roles))
        return PrepareAction::reuse;
    if (hasEngine && isRecording)
        return PrepareAction::holdForRecord;
    return PrepareAction::rebuild;
}

/** A format change that arrived while Record owned the engine, kept until it can be applied. */
struct HeldFormat
{
    bool held = false;
    double sampleRate = 0.0;
    int maxBlockFrames = 0;

    void hold (double rate, int frames) noexcept
    {
        held = true;
        sampleRate = rate;
        maxBlockFrames = frames;
    }

    void release() noexcept { held = false; }
};

} // namespace kirin
