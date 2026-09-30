#include "LiveComparePin.h"
#include "LiveCompareWindows.h"

#include <algorithm>

namespace hypha::live_compare
{
namespace
{
std::vector<float> narrowed (std::vector<float> stereo, int channels)
{
    if (channels == 2)
        return stereo;
    std::vector<float> mono (stereo.size() / 2);
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = stereo[i * 2];
    return mono;
}
}

PinnedWindow pinLatest (const Ring& ring, const PostRenderer& renderer, std::int64_t frames, int channels)
{
    PinnedWindow pin;
    const auto view = renderer.historyView();
    if (view.samples == nullptr || ! view.kValid || frames <= 0 || (channels != 1 && channels != 2))
        return pin;
    if (std::min (view.end - view.start, view.frames - 1) < frames)
    {
        pin.failure = PinFailure::tooShort;
        return pin;
    }
    const auto start = view.end - frames;
    PostRenderer::ProjectView project;
    if (! renderer.projectView (project) || ! project.known || project.runStart > start)
    {
        pin.failure = PinFailure::notOneRange;
        return pin;
    }
    // A stretch that restarts after this read starts after the window, which stays in the old one.
    std::vector<float> post (static_cast<std::size_t> (frames) * 2), pre (post.size());
    copyPostHistory (view, start, frames, post);
    if (! renderer.historyStillValid (view, start)
        || ! copyPreRing (ring, start - view.k, frames, pre, view.preRun)
        || ! renderer.historyStillValid (view, start))
    {
        pin.failure = PinFailure::overwritten;
        return pin;
    }
    pin.failure = PinFailure::none;
    pin.projectStart = start + project.offset;
    pin.frames = frames;
    pin.channels = channels;
    pin.post = narrowed (std::move (post), channels);
    pin.pre = narrowed (std::move (pre), channels);
    return pin;
}
}
