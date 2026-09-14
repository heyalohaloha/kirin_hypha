#include "ReferenceVisualAudio.h"
#include "ReferenceAudioPages.h"

#include "ReferenceRuntimeV2Source.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_audition
{
    static_assert (std::atomic<std::uint8_t>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);

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
        return installReader (std::move (nextReader), hostSampleRate, hostChannels, false);
    }

    juce::String AudioPages::open (const RuntimeSource& source,
                                   double hostSampleRate, int hostChannels,
                                   bool sampleRateConversionApproved)
    {
        auto nextReader = std::unique_ptr<juce::AudioFormatReader> (
            formats.createReaderFor (juce::File (source.absolutePath)));
        if (nextReader == nullptr)
            return "reference_source_decode_failed";
        return installReader (std::move (nextReader), hostSampleRate, hostChannels,
                              sampleRateConversionApproved);
    }

    juce::String AudioPages::installReaderForTest (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels, bool sampleRateConversionApproved)
    {
        return installReader (std::move (nextReader), hostSampleRate, hostChannels,
                              sampleRateConversionApproved);
    }

    juce::String AudioPages::installReader (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels, bool sampleRateConversionApproved)
    {
        openState.store (false, std::memory_order_release);
        if (! retirePages())
            return "runtime_busy";
        reader.reset();
        sourceLength = 0;
        sourceChannels = 0;
        pinnedCueStart = 0;
        pinnedCueEnd = 0;
        pinnedCueLoops = false;
        if (nextReader == nullptr || ! std::isfinite (hostSampleRate) || hostSampleRate <= 0.0
            || (hostChannels != 1 && hostChannels != 2))
            return "runtime_format_invalid";
        const bool rateDiffers = std::abs (nextReader->sampleRate - hostSampleRate) > 0.001;
        if (rateDiffers && ! sampleRateConversionApproved)
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
        sourceSampleRate = nextReader->sampleRate;
        outputSampleRate = hostSampleRate;
        sampleRateConversion = rateDiffers;
        sourceLength = static_cast<std::int64_t> (std::ceil (
            static_cast<double> (nextReader->lengthInSamples) * hostSampleRate
            / nextReader->sampleRate));
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
            conversionInput.setSize (0, 0);
            sourceLength = 0;
            sourceChannels = 0;
            sourceSampleRate = 0.0;
            outputSampleRate = 0.0;
            sampleRateConversion = false;
            pinnedCueStart = 0;
            pinnedCueEnd = 0;
            pinnedCueLoops = false;
        }
    }

    bool AudioPages::retirePages()
    {
        for (auto& page : pages)
        {
            bool retired = false;
            for (int attempt = 0; attempt < 200 && ! retired; ++attempt)
            {
                auto state = page.state.load (std::memory_order_acquire);
                if (state == empty)
                {
                    retired = true;
                    break;
                }
                if (state == ready
                    && page.state.compare_exchange_strong (
                        state, empty, std::memory_order_acq_rel))
                {
                    retired = true;
                    break;
                }
                juce::Thread::sleep (1);
            }
            if (! retired)
                return false;
        }
        return true;
    }

    void AudioPages::request (std::int64_t sourcePosition) noexcept
    {
        if (sourcePosition >= 0)
            requestedPosition.store (sourcePosition, std::memory_order_release);
    }

    void AudioPages::setPinnedCue (std::int64_t cueStart,
                                   std::int64_t cueEnd,
                                   bool loopEnabled) noexcept
    {
        pinnedCueStart = cueStart;
        pinnedCueEnd = cueEnd;
        pinnedCueLoops = loopEnabled && cueStart >= 0 && cueEnd > cueStart;
    }

    void AudioPages::service()
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        if (reader == nullptr || framesPerPage <= 0)
            return;
        const auto position = juce::jmax<std::int64_t> (
            0, requestedPosition.load (std::memory_order_acquire));
        const auto current = (position / framesPerPage) * framesPerPage;
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        const auto length = sourceLength.load (std::memory_order_acquire);
        std::array<std::int64_t, pageCount> protectedStarts {};
        size_t protectedCount = 0;
        const auto protect = [&] (std::int64_t start)
        {
            if (start < 0 || start >= length)
                return;
            for (size_t index = 0; index < protectedCount; ++index)
                if (protectedStarts[index] == start)
                    return;
            if (protectedCount < protectedStarts.size())
                protectedStarts[protectedCount++] = start;
        };
        protect (current);
        protect (current + framesPerPage);
        protect (current - framesPerPage);
        if (pinnedCueLoops)
        {
            protect ((pinnedCueStart / framesPerPage) * framesPerPage);
            protect (((pinnedCueEnd - 1) / framesPerPage) * framesPerPage);
        }
        for (size_t index = 0; index < protectedCount; ++index)
            fill (protectedStarts[index], protectedStarts, protectedCount, generation);
    }

    bool AudioPages::fill (
        std::int64_t pageStart,
        const std::array<std::int64_t, pageCount>& protectedStarts,
        size_t protectedCount, std::uint64_t generation)
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
        const auto length = sourceLength.load (std::memory_order_acquire);
        if (pageStart < 0 || pageStart >= length)
            return false;
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
                const auto existingStart = page.start.load (std::memory_order_relaxed);
                bool protectedPage = false;
                if (page.generation.load (std::memory_order_relaxed) == generation)
                    for (size_t index = 0; index < protectedCount; ++index)
                        protectedPage = protectedPage
                            || existingStart == protectedStarts[index];
                if (protectedPage)
                    continue;

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

        selected->publicationSequence.fetch_add (1, std::memory_order_acq_rel);
        selected->audio.clear();
        const bool readOk = sampleRateConversion
            ? fillConverted (*selected, pageStart)
            : reader->read (&selected->audio, 0, framesPerPage,
                            pageStart, true, channels > 1);
        if (! readOk || generation != activeGeneration.load (std::memory_order_acquire))
        {
            selected->publicationSequence.fetch_add (1, std::memory_order_release);
            selected->state.store (empty, std::memory_order_release);
            return false;
        }
        selected->start.store (pageStart, std::memory_order_relaxed);
        selected->generation.store (generation, std::memory_order_relaxed);
        selected->publicationSequence.fetch_add (1, std::memory_order_release);
        selected->state.store (ready, std::memory_order_release);
        return true;
    }

    bool AudioPages::fillConverted (Page& page, std::int64_t pageStart)
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
        if (reader == nullptr || sourceSampleRate <= 0.0 || outputSampleRate <= 0.0)
            return false;
        return readReferenceVisualAudio (*reader, pageStart, framesPerPage,
            static_cast<int> (outputSampleRate), channels, page.audio, conversionInput);
    }

}
