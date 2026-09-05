#include "ReferenceAudioPages.h"

#include "ReferenceRuntimeV2Source.h"

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
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        if (reader == nullptr || framesPerPage <= 0)
            return;
        const auto position = juce::jmax<std::int64_t> (
            0, requestedPosition.load (std::memory_order_acquire));
        const auto current = (position / framesPerPage) * framesPerPage;
        fill (current);
        fill (current + framesPerPage);
        if (current >= framesPerPage)
            fill (current - framesPerPage);
    }

    bool AudioPages::fill (std::int64_t pageStart)
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
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
        const bool readOk = sampleRateConversion
            ? fillConverted (*selected, pageStart)
            : reader->read (&selected->audio, 0, framesPerPage,
                            pageStart, true, channels > 1);
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

    bool AudioPages::fillConverted (Page& page, std::int64_t pageStart)
    {
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
        if (reader == nullptr || sourceSampleRate <= 0.0 || outputSampleRate <= 0.0)
            return false;
        constexpr int halfTaps = 24;
        const double ratio = sourceSampleRate / outputSampleRate;
        const double firstPosition = static_cast<double> (pageStart) * ratio;
        const double lastPosition = static_cast<double> (pageStart + framesPerPage - 1) * ratio;
        const auto rawStart = static_cast<std::int64_t> (std::floor (firstPosition)) - halfTaps;
        const auto rawEnd = static_cast<std::int64_t> (std::ceil (lastPosition)) + halfTaps + 1;
        const auto readStart = juce::jmax<std::int64_t> (0, rawStart);
        const auto readEnd = juce::jmin<std::int64_t> (reader->lengthInSamples, rawEnd);
        const int readFrames = static_cast<int> (juce::jmax<std::int64_t> (0, readEnd - readStart));
        conversionInput.setSize (channels, juce::jmax (1, readFrames), false, false, true);
        conversionInput.clear();
        if (readFrames > 0 && ! reader->read (&conversionInput, 0, readFrames,
                                              readStart, true, channels > 1))
            return false;

        const double cutoff = juce::jmin (1.0, outputSampleRate / sourceSampleRate) * 0.94;
        for (int outputFrame = 0; outputFrame < framesPerPage; ++outputFrame)
        {
            const double position = static_cast<double> (pageStart + outputFrame) * ratio;
            const auto center = static_cast<std::int64_t> (std::floor (position));
            for (int channel = 0; channel < channels; ++channel)
            {
                double weighted = 0.0;
                double weightSum = 0.0;
                for (int tap = -halfTaps + 1; tap <= halfTaps; ++tap)
                {
                    const auto sourceFrame = center + tap;
                    if (sourceFrame < 0 || sourceFrame >= reader->lengthInSamples)
                        continue;
                    const double distance = position - static_cast<double> (sourceFrame);
                    const double scaled = juce::MathConstants<double>::pi * cutoff * distance;
                    const double sinc = std::abs (scaled) < 1.0e-12
                        ? cutoff : cutoff * std::sin (scaled) / scaled;
                    const double normalizedDistance = distance / static_cast<double> (halfTaps);
                    const double window = std::abs (normalizedDistance) >= 1.0
                        ? 0.0
                        : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi
                                                * normalizedDistance)
                               + 0.08 * std::cos (juce::MathConstants<double>::twoPi
                                                 * normalizedDistance);
                    const double weight = sinc * window;
                    const auto inputOffset = sourceFrame - readStart;
                    if (inputOffset >= 0 && inputOffset < readFrames)
                    {
                        weighted += conversionInput.getSample (
                            channel, static_cast<int> (inputOffset)) * weight;
                        weightSum += weight;
                    }
                }
                page.audio.setSample (channel, outputFrame,
                                      static_cast<float> (weightSum == 0.0
                                          ? 0.0 : weighted / weightSum));
            }
        }
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
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto sourceFrames = sourceLength.load (std::memory_order_acquire);
        if (! openState.load (std::memory_order_acquire) || sourcePosition < 0
            || frames < 0 || framesPerPage <= 0 || sourcePosition >= sourceFrames)
            return false;
        const auto tailFrames = static_cast<std::int64_t> (juce::jmax (0, frames - 1));
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
        const int frames = destination.getNumSamples();
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        const auto channels = sourceChannels.load (std::memory_order_acquire);
        if (! std::isfinite (linearGain) || linearGain < 0.0f
            || framesPerPage <= 0 || ! readyAt (sourcePosition, frames))
            return false;
        request (sourcePosition);
        const auto generation = activeGeneration.load (std::memory_order_acquire);
        const auto firstStart = (sourcePosition / framesPerPage) * framesPerPage;
        const auto lastPosition = sourcePosition
            + static_cast<std::int64_t> (juce::jmax (0, frames - 1));
        const auto lastStart = (lastPosition / framesPerPage) * framesPerPage;
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
            || destination.getNumChannels() != channels)
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
            const int count = juce::jmin (frames - destinationOffset, framesPerPage - pageOffset);
            for (int channel = 0; channel < channels; ++channel)
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
