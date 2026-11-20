#pragma once
#include "ReferenceAudioPages.h"
#include "ReferenceMetricScan.h"
#include <algorithm>
#include <limits>

namespace hypha::reference_audition::metrics
{
// Job-local only: never borrow an audition bank or call this on the audio/UI thread.
// The owner opens manual Pages at this rate/channel layout and never reopens them.
class PageReader final
{
public:
    PageReader (AudioPages& pagesIn, std::uint32_t rateIn, std::uint32_t channelsIn,
                const std::atomic<bool>& cancelIn)
        : pages (pagesIn), rate (rateIn), channels (channelsIn), cancelled (cancelIn),
          generation (pages.openGeneration()) {}

    ReadState read (std::uint64_t position, float* output, std::size_t count)
    {
        if (output == nullptr || rate < 8000 || rate > 384000 || rate % 10 != 0
            || (channels != 1 && channels != 2) || count != (rate / 10) * channels
            || position > static_cast<std::uint64_t> (std::numeric_limits<std::int64_t>::max()))
            return ReadState::failed;
        const auto start = static_cast<std::int64_t> (position);
        const auto length = pages.lengthInSamples();
        const auto frames = static_cast<std::int64_t> (count / channels);
        if (length <= 0 || start >= length || frames > length - start || invalid())
            return ReadState::failed;
        // A full aperture can straddle pages (notably 800-frame apertures at 8 kHz).
        // Smaller reads also stay below every supported page capacity.
        for (std::int64_t offset = 0; offset < frames;)
        {
            if (invalid()) return ReadState::failed;
            const auto chunk = static_cast<int> (std::min<std::int64_t> (256, frames - offset));
            buffer.setSize (static_cast<int> (channels), chunk, false, false, true);
            auto state = AudioPages::RenderResult::waitingForPages;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (invalid()) return ReadState::failed;
                pages.request (start + offset);
                pages.service();
                if (invalid() || pages.hasPersistentReadFailure (start + offset))
                    return ReadState::failed;
                state = pages.renderResult (buffer, start + offset, 1.0f);
                if (state != AudioPages::RenderResult::waitingForPages) break;
            }
            if (invalid() || state == AudioPages::RenderResult::invalid) return ReadState::failed;
            if (state != AudioPages::RenderResult::rendered || pages.sourceUnavailable())
                return ReadState::waiting;
            for (int f = 0; f < chunk; ++f)
                for (std::uint32_t c = 0; c < channels; ++c)
                    output[(static_cast<std::size_t> (offset) + static_cast<std::size_t> (f)) * channels + c]
                        = buffer.getSample (static_cast<int> (c), f);
            offset += chunk;
        }
        return invalid() ? ReadState::failed : ReadState::rendered;
    }
private:
    bool invalid() const noexcept
    { return cancelled.load (std::memory_order_acquire) || pages.sourceInvalidated()
          || pages.openGeneration() != generation; }
    AudioPages& pages;
    const std::uint32_t rate, channels;
    const std::atomic<bool>& cancelled;
    const std::uint64_t generation;
    juce::AudioBuffer<float> buffer;
};
}
