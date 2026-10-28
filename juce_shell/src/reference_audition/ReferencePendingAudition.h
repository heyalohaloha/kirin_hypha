#pragma once
#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
// A transient, explicit next-play intent. Never saved with the DAW session or library choices.
struct PendingAuditionView
{
    static constexpr double pageNoticeSeconds = 3.0;
    enum class Stage { none, play, checking, level, sourceChanged, safetyChanged, startFailed,
                       sourceLevelUnavailable, ceilingExceeded };
    int slot = 0;
    Stage stage = Stage::none;
    juce::String reasonCode {}; // Preserve the reason at the stop, even if the next snapshot is ready.
    bool waiting() const noexcept
    {
        return slot != 0 && (stage == Stage::play || stage == Stage::checking || stage == Stage::level);
    }
};
}
