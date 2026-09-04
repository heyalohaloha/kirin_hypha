#include "ReferenceAudioPages.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
    static_assert (std::atomic<std::uint8_t>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);

    AudioPages::AudioPages()
    {
        formats.registerBasicFormats();
    }

    AudioPages::~AudioPages()
    {
        close();
    }

    juce::String AudioPages::open (const SourceReceipt& receipt,
                                   double hostSampleRate, int hostChannels)
    {
        if (! receipt.valid())
            return "source_receipt_rejected";
        auto nextReader = std::unique_ptr<juce::AudioFormatReader> (
            formats.createReaderFor (juce::File (receipt.filePath)));
        if (nextReader == nullptr)
            return "source_decode_failed";
        return installReader (std::move (nextReader), hostSampleRate, hostChannels);
    }

    juce::String AudioPages::installReaderForTest (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels)
    {
        return installReader (std::move (nextReader), hostSampleRate, hostChannels);
    }

    juce::String AudioPages::installReader (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels)
    {
        openState.store (false, std::memory_order_release);
        if (! retirePages())
            return "runtime_busy";
        reader.reset();
        sourceLength = 0;
        sourceChannels = 0;
        if (nextReader == nullptr || ! std::isfinite (hostSampleRate) || hostSampleRate <= 0.0
            || (hostChannels != 1 && hostChannels != 2))
            return "runtime_format_invalid";
        if (std::abs (nextReader->sampleRate - hostSampleRate) > 0.001)
            return "sample_rate_unsupported";
        if (static_cast<int> (nextReader->numChannels) != hostChannels)
            return "channel_layout_unsupported";
        if (nextReader->lengthInSamples <= 0)
            return "source_decode_failed";

        const auto nextPageFrames = juce::jmax (8'192, static_cast<int> (std::ceil (hostSampleRate)));
        for (auto& page : pages)
        {
            if (page.audio.getNumChannels() != hostChannels
                || page.audio.getNumSamples() != nextPageFrames)
                page.audio.setSize (hostChannels, nextPageFrames, false, true, false);
            page.audio.clear();
            page.state.store (empty, std::memory_order_release);
        }
        pageFrames = nextPageFrames;
        sourceChannels = hostChannels;
        sourceLength = nextReader->lengthInSamples;
        reader = std::move (nextReader);
        activeGeneration.fetch_add (1, std::memory_order_acq_rel);
        requestedPosition.store (0, std::memory_order_release);
        service();
        openState.store (true, std::memory_order_release);
        return {};
    }

    void AudioPages::close()
    {
        openState.store (false, std::memory_order_release);
        activeGeneration.fetch_add (1, std::memory_order_acq_rel);
        if (retirePages())
        {
            reader.reset();
            sourceLength = 0;
            sourceChannels = 0;
        }
    }

    bool AudioPages::retirePages()
    {
        for (auto& page : pages)
        {
            for (int attempt = 0; attempt < 200
                 && page.state.load (std::memory_order_acquire) == inUse; ++attempt)
                juce::Thread::sleep (1);
            if (page.state.load (std::memory_order_acquire) == inUse)
                return false;
            page.state.store (empty, std::memory_order_release);
        }
        return true;
    }

    void AudioPages::request (std::int64_t sourcePosition) noexcept
    {
        if (sourcePosition >= 0)
            requestedPosition.store (sourcePosition, std::memory_order_release);
    }

    void AudioPages::service()
    {
        if (reader == nullptr || pageFrames <= 0)
            return;
        const auto position = juce::jmax<std::int64_t> (
            0, requestedPosition.load (std::memory_order_acquire));
        const auto current = (position / pageFrames) * pageFrames;
        fill (current);
        fill (current + pageFrames);
        if (current >= pageFrames)
            fill (current - pageFrames);
    }

    bool AudioPages::fill (std::int64_t pageStart)
    {
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        if (containsReady (pageStart, generation))
            return true;

        Page* selected = nullptr;
        for (auto& page : pages)
        {
            std::uint8_t expected = empty;
            if (page.state.compare_exchange_strong (expected, loading,
                                                    std::memory_order_acq_rel))
            {
                selected = &page;
                break;
            }
        }
        if (selected == nullptr)
        {
            for (auto& page : pages)
            {
                std::uint8_t expected = ready;
                if (page.state.compare_exchange_strong (expected, loading,
                                                        std::memory_order_acq_rel))
                {
                    selected = &page;
                    break;
                }
            }
        }
        if (selected == nullptr)
            return false;

        selected->audio.clear();
        const bool readOk = reader->read (&selected->audio, 0, pageFrames,
                                          pageStart, true, sourceChannels > 1);
        if (! readOk || generation != activeGeneration.load (std::memory_order_acquire))
        {
            selected->state.store (empty, std::memory_order_release);
            return false;
        }
        selected->start.store (pageStart, std::memory_order_relaxed);
        selected->generation.store (generation, std::memory_order_relaxed);
        selected->state.store (ready, std::memory_order_release);
        return true;
    }

    bool AudioPages::containsReady (std::int64_t pageStart,
                                    std::uint64_t expectedGeneration) const noexcept
    {
        for (const auto& page : pages)
        {
            const auto state = page.state.load (std::memory_order_acquire);
            if ((state == ready || state == inUse)
                && page.start.load (std::memory_order_relaxed) == pageStart
                && page.generation.load (std::memory_order_relaxed) == expectedGeneration)
                return true;
        }
        return false;
    }

    AudioPages::Page* AudioPages::acquire (
        std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept
    {
        for (auto& page : pages)
        {
            std::uint8_t expected = ready;
            if (page.start.load (std::memory_order_relaxed) == pageStart
                && page.generation.load (std::memory_order_relaxed) == expectedGeneration
                && page.state.compare_exchange_strong (expected, inUse,
                                                       std::memory_order_acq_rel))
                return &page;
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
        if (! openState.load (std::memory_order_acquire) || sourcePosition < 0
            || frames < 0 || pageFrames <= 0)
            return false;
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        const auto first = (sourcePosition / pageFrames) * pageFrames;
        const auto lastPosition = sourcePosition + juce::jmax (0, frames - 1);
        const auto last = (lastPosition / pageFrames) * pageFrames;
        return containsReady (first, generation)
            && (first == last || containsReady (last, generation));
    }

    bool AudioPages::render (juce::AudioBuffer<float>& destination,
                             std::int64_t sourcePosition, float linearGain) noexcept
    {
        const int frames = destination.getNumSamples();
        if (! std::isfinite (linearGain) || linearGain < 0.0f
            || ! readyAt (sourcePosition, frames))
            return false;
        request (sourcePosition);
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        const auto firstStart = (sourcePosition / pageFrames) * pageFrames;
        const auto lastPosition = sourcePosition + juce::jmax (0, frames - 1);
        const auto lastStart = (lastPosition / pageFrames) * pageFrames;
        auto* first = acquire (firstStart, generation);
        if (first == nullptr)
            return false;
        auto* last = first;
        if (lastStart != firstStart)
        {
            last = acquire (lastStart, generation);
            if (last == nullptr)
            {
                release (first);
                return false;
            }
        }
        if (generation != activeGeneration.load (std::memory_order_acquire)
            || destination.getNumChannels() != sourceChannels)
        {
            if (last != first)
                release (last);
            release (first);
            return false;
        }

        int destinationOffset = 0;
        while (destinationOffset < frames)
        {
            auto* page = destinationOffset == 0 ? first : last;
            const auto absolutePosition = sourcePosition + destinationOffset;
            const int pageOffset = static_cast<int> (
                absolutePosition - page->start.load (std::memory_order_relaxed));
            const int count = juce::jmin (frames - destinationOffset, pageFrames - pageOffset);
            for (int channel = 0; channel < sourceChannels; ++channel)
            {
                const auto* input = page->audio.getReadPointer (channel, pageOffset);
                auto* output = destination.getWritePointer (channel, destinationOffset);
                for (int sample = 0; sample < count; ++sample)
                    output[sample] = input[sample] * linearGain;
            }
            destinationOffset += count;
        }
        if (last != first)
            release (last);
        release (first);
        return true;
    }
}
