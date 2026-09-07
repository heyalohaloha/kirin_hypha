#include "ReferenceAudioPages.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
   #if defined(KIRIN_REFERENCE_AUDIO_PAGES_TEST_HOOK)
    namespace
    {
        std::atomic<AudioPages::AcquireOwnershipHook> acquireOwnershipHook { nullptr };
    }

    void AudioPages::setAcquireOwnershipHookForTest (AcquireOwnershipHook hook) noexcept
    {
        acquireOwnershipHook.store (hook, std::memory_order_release);
    }
   #endif

    bool AudioPages::containsReady (std::int64_t pageStart,
                                    std::uint64_t expectedGeneration) const noexcept
    {
        for (auto& page : pages)
        {
            const auto before = page.publicationSequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0)
                continue;

            const auto state = page.state.load (std::memory_order_acquire);
            if (state != ready && state != inUse)
                continue;

            const auto observedStart = page.start.load (std::memory_order_relaxed);
            const auto observedGeneration = page.generation.load (std::memory_order_relaxed);
            const auto after = page.publicationSequence.load (std::memory_order_acquire);
            if (before == after && observedStart == pageStart
                && observedGeneration == expectedGeneration)
                return true;
        }
        return false;
    }

    AudioPages::Page* AudioPages::acquire (
        std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept
    {
        for (auto& page : pages)
        {
            if (page.start.load (std::memory_order_relaxed) != pageStart
                || page.generation.load (std::memory_order_relaxed) != expectedGeneration)
                continue;
            std::uint8_t expected = ready;
            if (! page.state.compare_exchange_strong (
                    expected, inUse, std::memory_order_acq_rel))
                continue;
           #if defined(KIRIN_REFERENCE_AUDIO_PAGES_TEST_HOOK)
            if (const auto hook = acquireOwnershipHook.load (std::memory_order_acquire))
                hook (const_cast<AudioPages&> (*this));
           #endif
            if (page.start.load (std::memory_order_relaxed) == pageStart
                && page.generation.load (std::memory_order_relaxed) == expectedGeneration)
                return &page;
            page.state.store (ready, std::memory_order_release);
        }
        return nullptr;
    }

    void AudioPages::release (Page* page) const noexcept
    {
        if (page != nullptr)
            page->state.store (ready, std::memory_order_release);
    }

    bool AudioPages::readyAt (std::int64_t sourcePosition, int frames) const noexcept
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto sourceFrames = sourceLength.load (std::memory_order_acquire);
        if (! openState.load (std::memory_order_acquire) || sourcePosition < 0
            || frames < 0 || framesPerPage <= 0 || sourcePosition >= sourceFrames)
            return false;
        if (frames == 0)
            return true;
        const auto tailFrames = static_cast<std::int64_t> (frames - 1);
        if (tailFrames > sourceFrames - 1 - sourcePosition)
            return false;
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        const auto first = (sourcePosition / framesPerPage) * framesPerPage;
        const auto lastPosition = sourcePosition + tailFrames;
        const auto last = (lastPosition / framesPerPage) * framesPerPage;
        return containsReady (first, generation)
            && (first == last || containsReady (last, generation));
    }

    bool AudioPages::render (juce::AudioBuffer<float>& destination,
                             std::int64_t sourcePosition, float linearGain) noexcept
    {
        return renderRegion (destination, sourcePosition, 0,
                             sourceLength.load (std::memory_order_acquire),
                             false, linearGain);
    }

    bool AudioPages::renderCue (juce::AudioBuffer<float>& destination,
                                std::int64_t sourcePosition,
                                std::int64_t cueStart, std::int64_t cueEnd,
                                bool loopEnabled, float linearGain) noexcept
    {
        return renderRegion (destination, sourcePosition, cueStart, cueEnd,
                             loopEnabled, linearGain);
    }

    bool AudioPages::renderRegion (juce::AudioBuffer<float>& destination,
                                   std::int64_t sourcePosition,
                                   std::int64_t regionStart, std::int64_t regionEnd,
                                   bool loopEnabled, float linearGain) noexcept
    {
        const int frames = destination.getNumSamples();
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto sourceFrames = sourceLength.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
        if (! openState.load (std::memory_order_acquire)
            || ! std::isfinite (linearGain) || linearGain < 0.0f
            || framesPerPage <= 0 || frames > framesPerPage
            || destination.getNumChannels() != channels
            || regionStart < 0 || regionEnd <= regionStart || regionEnd > sourceFrames
            || sourcePosition < regionStart || sourcePosition >= regionEnd)
            return false;
        if (frames == 0)
            return true;

        std::array<std::int64_t, pageCount> requiredStarts {};
        size_t requiredCount = 0;
        int remaining = frames;
        auto position = sourcePosition;
        while (remaining > 0)
        {
            const auto pageStart = (position / framesPerPage) * framesPerPage;
            bool alreadyRequired = false;
            for (size_t index = 0; index < requiredCount; ++index)
                alreadyRequired = alreadyRequired || requiredStarts[index] == pageStart;
            if (! alreadyRequired)
            {
                if (requiredCount == requiredStarts.size())
                    return false;
                requiredStarts[requiredCount++] = pageStart;
            }
            const auto untilPageEnd = framesPerPage - (position - pageStart);
            const auto untilRegionEnd = regionEnd - position;
            const auto count = static_cast<int> (std::min<std::int64_t> (
                remaining, std::min (untilPageEnd, untilRegionEnd)));
            if (count <= 0)
                return false;
            remaining -= count;
            position += count;
            if (remaining > 0 && position == regionEnd)
            {
                if (! loopEnabled)
                    return false;
                position = regionStart;
            }
        }

        request (sourcePosition);
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        std::array<Page*, pageCount> acquired {};
        size_t acquiredCount = 0;
        const auto releaseAll = [&]() noexcept
        {
            while (acquiredCount > 0)
                release (acquired[--acquiredCount]);
        };
        for (size_t index = 0; index < requiredCount; ++index)
        {
            auto* page = acquire (requiredStarts[index], generation);
            if (page == nullptr)
            {
                releaseAll();
                return false;
            }
            acquired[acquiredCount++] = page;
        }
        if (generation != activeGeneration.load (std::memory_order_acquire)
            || ! openState.load (std::memory_order_acquire))
        {
            releaseAll();
            return false;
        }

        remaining = frames;
        position = sourcePosition;
        int destinationOffset = 0;
        while (remaining > 0)
        {
            const auto pageStart = (position / framesPerPage) * framesPerPage;
            Page* page = nullptr;
            for (size_t index = 0; index < acquiredCount; ++index)
                if (acquired[index]->start.load (std::memory_order_relaxed) == pageStart)
                    page = acquired[index];
            if (page == nullptr)
            {
                releaseAll();
                return false;
            }
            const auto pageOffset = static_cast<int> (position - pageStart);
            const auto count = static_cast<int> (std::min<std::int64_t> (
                remaining, std::min<std::int64_t> (
                    framesPerPage - pageOffset, regionEnd - position)));
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto* input = page->audio.getReadPointer (channel, pageOffset);
                auto* output = destination.getWritePointer (channel, destinationOffset);
                for (int sample = 0; sample < count; ++sample)
                    output[sample] = input[sample] * linearGain;
            }
            remaining -= count;
            destinationOffset += count;
            position += count;
            if (remaining > 0 && position == regionEnd)
                position = regionStart;
        }
        releaseAll();
        return true;
    }
}
