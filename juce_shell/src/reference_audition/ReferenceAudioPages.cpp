#include "ReferenceVisualAudio.h"
#include "ReferenceAudioPages.h"
#include "ReferenceTestCommitObserver.h"

#include "ReferenceRuntimeV2Source.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
    static_assert (std::atomic<std::uint8_t>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
    namespace
    {
        bool sameRevision (const RuntimeFileRevision& a, const RuntimeFileRevision& b)
        {
            return a.deviceId == b.deviceId && a.fileId == b.fileId && a.sizeBytes == b.sizeBytes
                && a.modifiedTimeNs == b.modifiedTimeNs && a.changedTimeNs == b.changedTimeNs;
        }
    }

    AudioPages::AudioPages (ServiceMode mode)
        : juce::Thread ("Reference audio refill"), serviceMode (mode)
    {
        formats.registerBasicFormats();
    }

    AudioPages::~AudioPages()
    {
        // Join outside readerLock; the refill may currently own it while decoding.
        signalThreadShouldExit();
        notify();
        stopThread (-1);
        close();
    }

    void AudioPages::run()
    {
        while (!threadShouldExit())
        {
            serviceOnce();
            wait (openState.load (std::memory_order_acquire) ? 10 : -1);
        }
    }

    juce::String AudioPages::open (const SourceReceipt& receipt,
                                   double hostSampleRate, int hostChannels)
    {
        const juce::ScopedLock lock (readerLock);
        if (! receipt.valid())
            return "source_receipt_rejected";
        auto nextReader = std::unique_ptr<juce::AudioFormatReader> (
            formats.createReaderFor (juce::File (receipt.filePath)));
        if (nextReader == nullptr)
            return "source_decode_failed";
        verifySource = {}; verifyReaderIdentity = {}; verifySourceRejection = {};
        sourceAuthority.reset(); sourceInvalidation = nullptr; initialSourceInvalidation = 0;
        sourceRejected.store (false, std::memory_order_release);
        sourceVerified.store (true, std::memory_order_release);
        readerIdentityValid.store (true, std::memory_order_release);
        return installReader (std::move (nextReader), hostSampleRate, hostChannels, false);
    }

    juce::String AudioPages::open (const RuntimeSource& source,
                                   double hostSampleRate, int hostChannels,
                                   bool sampleRateConversionApproved,
                                   std::function<bool (RuntimeFileRevision&)> sourceCheck,
                                   std::unique_ptr<juce::InputStream> input,
                                   std::function<bool()> readerCheck,
                                   std::function<bool()> rejectionCheck,
                                   std::shared_ptr<void> authority,
                                   const std::atomic<std::uint64_t>* invalidation,
                                   std::uint64_t initialInvalidation)
    {
        const juce::ScopedLock lock (readerLock);
        if ((invalidation != nullptr) != static_cast<bool> (authority)) return "runtime_format_invalid";
        auto nextReader = std::unique_ptr<juce::AudioFormatReader> (input
            ? formats.createReaderFor (std::move (input))
            : formats.createReaderFor (juce::File (source.absolutePath)));
        if (nextReader == nullptr)
            return "reference_source_decode_failed";
        verifySource = std::move (sourceCheck); verifyReaderIdentity = std::move (readerCheck);
        verifySourceRejection = std::move (rejectionCheck);
        sourceAuthority = std::move (authority); sourceInvalidation = invalidation;
        initialSourceInvalidation = initialInvalidation;
        sourceRejected.store (false, std::memory_order_release);
        readerIdentityValid.store (!verifyReaderIdentity || verifyReaderIdentity(), std::memory_order_release);
        RuntimeFileRevision revision;
        const bool verified = !verifySource || verifySource (revision);
        const bool rejected = verifySourceRejection && verifySourceRejection();
        if (rejected) sourceRejected.store (true, std::memory_order_release);
        sourceVerified.store (verified && !rejected, std::memory_order_release);
        return installReader (std::move (nextReader), hostSampleRate, hostChannels,
                              sampleRateConversionApproved);
    }

    juce::String AudioPages::installReaderForTest (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels, bool sampleRateConversionApproved)
    {
        const juce::ScopedLock lock (readerLock);
        verifySource = {}; verifyReaderIdentity = {}; verifySourceRejection = {};
        sourceAuthority.reset(); sourceInvalidation = nullptr; initialSourceInvalidation = 0;
        sourceRejected.store (false, std::memory_order_release);
        sourceVerified.store (true, std::memory_order_release);
        readerIdentityValid.store (true, std::memory_order_release);
        return installReader (std::move (nextReader), hostSampleRate, hostChannels,
                              sampleRateConversionApproved);
    }

    juce::String AudioPages::installReader (
        std::unique_ptr<juce::AudioFormatReader> nextReader,
        double hostSampleRate, int hostChannels, bool sampleRateConversionApproved)
    {
        const juce::ScopedLock lock (readerLock);
        openState.store (false, std::memory_order_release);
        if (! retirePages())
            return "runtime_busy";
        reader.reset();
        requiredReadFailures.store (0, std::memory_order_release);
        sourceLength = 0;
        sourceChannels = 0;
        fileChannels = 0;
        setPinnedCue (0, 0, false);
        if (nextReader == nullptr || ! std::isfinite (hostSampleRate) || hostSampleRate <= 0.0
            || (hostChannels != 1 && hostChannels != 2))
            return "runtime_format_invalid";
        const bool rateDiffers = std::abs (nextReader->sampleRate - hostSampleRate) > 0.001;
        if (rateDiffers && ! sampleRateConversionApproved)
            return "sample_rate_unsupported";
        // A mono file on a stereo track plays its one channel on both sides (the reader copies it,
        // off the Audio Thread). Stereo on a mono track would need a downmix that changes the
        // sound, so it stays refused.
        const auto readerChannels = static_cast<int> (nextReader->numChannels);
        if (readerChannels != hostChannels && ! (readerChannels == 1 && hostChannels == 2))
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
        fileChannels = readerChannels;
        sourceSampleRate = nextReader->sampleRate;
        outputSampleRate = hostSampleRate;
        sampleRateConversion = rateDiffers;
        sourceLength = static_cast<std::int64_t> (std::ceil (
            static_cast<double> (nextReader->lengthInSamples) * hostSampleRate
            / nextReader->sampleRate));
        reader = std::move (nextReader);
        activeGeneration.fetch_add (1, std::memory_order_acq_rel);
        requestedPosition.store (0, std::memory_order_release);
        serviceOnce();
        if (serviceMode == ServiceMode::background && !isThreadRunning()
            && !startThread (juce::Thread::Priority::normal))
        {
            close();
            return "runtime_busy";
        }
        openState.store (true, std::memory_order_release);
        notify();
        return {};
    }

    void AudioPages::close()
    {
        const juce::ScopedLock lock (readerLock);
        openState.store (false, std::memory_order_release);
        verifySource = {}; verifyReaderIdentity = {}; verifySourceRejection = {};
        sourceAuthority.reset(); sourceInvalidation = nullptr; initialSourceInvalidation = 0;
        sourceRejected.store (false, std::memory_order_release);
        sourceVerified.store (true, std::memory_order_release);
        readerIdentityValid.store (true, std::memory_order_release);
        activeGeneration.fetch_add (1, std::memory_order_acq_rel);
        requiredReadFailures.store (0, std::memory_order_release);
        if (retirePages())
        {
            reader.reset();
            conversionInput.setSize (0, 0);
            sourceLength = 0;
            sourceChannels = 0;
            fileChannels = 0;
            sourceSampleRate = 0.0;
            outputSampleRate = 0.0;
            sampleRateConversion = false;
            setPinnedCue (0, 0, false);
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
        {
            const juce::ScopedLock lock (cueLock);
            pinnedCueStart = cueStart;
            pinnedCueEnd = cueEnd;
            pinnedCueLoops = loopEnabled && cueStart >= 0 && cueEnd > cueStart;
        }
        notify();
    }

    void AudioPages::service()
    {
        // Controllers keep draining A capture and processing commands even when
        // B's reader is stalled. Only explicit manual-test mode decodes here.
        if (serviceMode == ServiceMode::background) notify();
        else serviceOnce();
    }

    void AudioPages::serviceOnce()
    {
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "page_service_reader_lock");
        const juce::ScopedLock lock (readerLock);
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "page_service_reader_acquired");
        const auto framesPerPage = pageFrames.load (std::memory_order_acquire);
        if (reader == nullptr || framesPerPage <= 0)
            return;
        const bool readerValid = !verifyReaderIdentity || verifyReaderIdentity();
        readerIdentityValid.store (readerValid, std::memory_order_release);
        RuntimeFileRevision revision;
        const bool verified = readerValid && (!verifySource || verifySource (revision));
        const bool rejected = verifySourceRejection && verifySourceRejection();
        if (rejected) sourceRejected.store (true, std::memory_order_release);
        sourceVerified.store (verified && !rejected, std::memory_order_release);
        if (!verified || rejected)
        {
            if (!rejected) { requiredReadFailures.store (0, std::memory_order_release); failedPageStart.store (-1, std::memory_order_release); }
            return;
        }
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
        {
            const juce::ScopedLock lockCue (cueLock);
            if (pinnedCueLoops)
            {
                protect ((pinnedCueStart / framesPerPage) * framesPerPage);
                protect (((pinnedCueEnd - 1) / framesPerPage) * framesPerPage);
            }
        }
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "page_fill");
        for (size_t index = 0; index < protectedCount; ++index)
            fill (protectedStarts[index], protectedStarts, protectedCount, generation);
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "page_service_done");
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
        RuntimeFileRevision before;
        if (verifySource && !verifySource (before))
        {
            sourceVerified.store (false, std::memory_order_release);
            if (verifySourceRejection && verifySourceRejection()) sourceRejected.store (true, std::memory_order_release);
            return false;
        }

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
            struct Replacement { Page* page; std::int64_t distance; };
            std::array<Replacement, pageCount> replacements {};
            size_t count = 0;
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
                // Keep pages near the playhead and pinned loop edges. Replace
                // stale or distant pages before those needed on the next wrap.
                auto distance = std::numeric_limits<std::int64_t>::max();
                if (page.generation.load (std::memory_order_relaxed) == generation)
                    for (size_t index = 0; index < protectedCount; ++index)
                        distance = std::min (distance, std::abs (existingStart - protectedStarts[index]));
                replacements[count++] = { &page, distance };
            }
            std::sort (replacements.begin(), replacements.begin() + count,
                       [] (const auto& a, const auto& b) { return a.distance > b.distance; });
            for (size_t index = 0; index < count; ++index)
            {
                auto& page = *replacements[index].page;
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
        const bool stillReaderValid = !verifyReaderIdentity || verifyReaderIdentity();
        readerIdentityValid.store (stillReaderValid, std::memory_order_release);
        RuntimeFileRevision after;
        const bool sameVerified = stillReaderValid && (!verifySource
            || (verifySource (after) && sameRevision (before, after)));
        const bool rejected = verifySourceRejection && verifySourceRejection();
        if (rejected) sourceRejected.store (true, std::memory_order_release);
        const bool stillVerified = sameVerified && !rejected;
        sourceVerified.store (stillVerified, std::memory_order_release);
        // Capacity contention is retryable. Only actual failed reads of the
        // currently requested page count as repeated decoder failure.
        // A successful prefetch also retires earlier failure evidence for that page.
        if (stillVerified && readOk && generation == activeGeneration.load (std::memory_order_acquire)
            && failedPageStart.load (std::memory_order_acquire) == pageStart)
            requiredReadFailures.store (0, std::memory_order_release);
        const auto requested = juce::jmax<std::int64_t> (0, requestedPosition.load (std::memory_order_acquire));
        if (stillVerified && generation == activeGeneration.load (std::memory_order_acquire)
            && pageStart == (requested / framesPerPage) * framesPerPage)
        {
            if (failedPageStart.load (std::memory_order_relaxed) != pageStart)
            {
                requiredReadFailures.store (0, std::memory_order_release);
                failedPageStart.store (pageStart, std::memory_order_release);
            }
            requiredReadFailures.store (readOk ? 0u
                : std::min (3u, requiredReadFailures.load (std::memory_order_relaxed) + 1u), std::memory_order_release);
        }
        if (! readOk || !stillVerified || generation != activeGeneration.load (std::memory_order_acquire))
        {
            selected->publicationSequence.fetch_add (1, std::memory_order_release);
            selected->state.store (empty, std::memory_order_release);
            return false;
        }
        if (verifySourceRejection && verifySourceRejection())
        {
            sourceRejected.store (true, std::memory_order_release);
            sourceVerified.store (false, std::memory_order_release);
            selected->publicationSequence.fetch_add (1, std::memory_order_release);
            selected->state.store (empty, std::memory_order_release);
            return false;
        }
        selected->authority = sourceAuthority;
        selected->invalidation = sourceInvalidation;
        selected->initialInvalidation = initialSourceInvalidation;
        selected->start.store (pageStart, std::memory_order_relaxed);
        selected->generation.store (generation, std::memory_order_relaxed);
        selected->publicationSequence.fetch_add (1, std::memory_order_release);
        selected->state.store (ready, std::memory_order_release);
        KIRIN_REFERENCE_COMMIT_FOR_TEST();
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
