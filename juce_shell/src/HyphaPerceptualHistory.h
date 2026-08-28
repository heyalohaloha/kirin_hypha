#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hypha::perceptual_history
{
    constexpr size_t historyCapacity = 60u;
    constexpr double historySeconds = 6.0;

    struct Sample
    {
        int64_t endpoint = 0;
        double pre = 0.0;
        double post = 0.0;
        double delta = 0.0;
        bool continuesPrevious = false;
    };

    enum class AppendResult
    {
        appended,
        duplicateIgnored,
        gapAppended,
        timelineReset,
        rejected,
    };

    class History final
    {
    public:
        AppendResult append (int64_t endpoint,
                             uint32_t sampleRate,
                             uint32_t apertureSamples,
                             double pre,
                             double post,
                             double delta) noexcept
        {
            const int64_t cadence = static_cast<int64_t> (sampleRate / 10u);
            if (sampleRate < 8'000u || sampleRate % 10u != 0u || cadence <= 0
                || apertureSamples != static_cast<uint32_t> (cadence)
                || endpoint % cadence != 0
                || ! std::isfinite (pre) || ! std::isfinite (post)
                || ! std::isfinite (delta) || pre < 0.0 || post < 0.0)
                return AppendResult::rejected;

            bool continuous = false;
            bool timelineReset = false;
            if (count > 0u)
            {
                const auto& newest = sampleAt (count - 1u);
                if (sampleRate == currentSampleRate && endpoint == newest.endpoint)
                    return AppendResult::duplicateIgnored;
                if (sampleRate != currentSampleRate || endpoint < newest.endpoint)
                {
                    clear();
                    timelineReset = true;
                }
                else
                    continuous = endpoint - newest.endpoint == cadence;
            }
            currentSampleRate = sampleRate;
            const Sample next { endpoint, pre, post, delta, continuous };
            if (count < historyCapacity)
            {
                values[(start + count) % historyCapacity] = next;
                ++count;
            }
            else
            {
                values[start] = next;
                start = (start + 1u) % historyCapacity;
            }
            if (timelineReset)
                return AppendResult::timelineReset;
            return continuous || count == 1u ? AppendResult::appended
                                             : AppendResult::gapAppended;
        }

        void clear() noexcept
        {
            start = 0u;
            count = 0u;
            currentSampleRate = 0u;
        }

        bool empty() const noexcept { return count == 0u; }
        size_t size() const noexcept { return count; }
        uint32_t sampleRate() const noexcept { return currentSampleRate; }

        const Sample& sampleAt (size_t logicalIndex) const noexcept
        {
            return values[(start + logicalIndex) % historyCapacity];
        }

        double ageSecondsAt (size_t logicalIndex) const noexcept
        {
            if (count == 0u || currentSampleRate == 0u)
                return 0.0;
            return static_cast<double> (
                sampleAt (count - 1u).endpoint - sampleAt (logicalIndex).endpoint)
                / static_cast<double> (currentSampleRate);
        }

    private:
        std::array<Sample, historyCapacity> values {};
        size_t start = 0u;
        size_t count = 0u;
        uint32_t currentSampleRate = 0u;
    };

    static_assert (sizeof (History) < 4u * 1024u);
}
