#pragma once

namespace hypha::reference_audition
{
// A transient, explicit next-play intent. Never saved with the DAW session or library choices.
struct PendingAuditionView
{
    enum class Stage { none, play, checking, approval, level, sourceChanged, safetyChanged, startFailed };
    int slot = 0;
    Stage stage = Stage::none;
    bool waiting() const noexcept
    {
        return slot != 0 && (stage == Stage::play || stage == Stage::checking
            || stage == Stage::approval || stage == Stage::level);
    }
};
}
