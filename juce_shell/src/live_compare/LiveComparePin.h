#pragma once

#include "LiveCompareSession.h"

#include <cstdint>
#include <vector>

namespace hypha::live_compare
{
enum class PinFailure : std::uint8_t
{
    none,
    notProven,   // K is not valid for the latest block
    tooShort,    // less contiguous, proven history than the window
    notOneRange, // a loop wrap, seek or stop lies inside the window
    overwritten  // PRE or POST moved past the window while it was copied
};

// The latest window of a live session, fixed: POST's input and the PRE that the clock rules map to
// it, as one project range that the DAW can play again. Interleaved, `channels` wide.
struct PinnedWindow
{
    PinFailure failure = PinFailure::notProven;
    std::int64_t projectStart = 0, frames = 0;
    int channels = 0;
    std::vector<float> post, pre;
    bool ok() const noexcept { return failure == PinFailure::none; }
};

// Non-RT (message thread). The window must lie inside the contiguous history, one project stretch
// (no loop wrap, seek or stop) and PRE's current run; nothing is padded, joined or moved.
PinnedWindow pinLatest (const Ring&, const PostRenderer&, std::int64_t frames, int channels);
}
