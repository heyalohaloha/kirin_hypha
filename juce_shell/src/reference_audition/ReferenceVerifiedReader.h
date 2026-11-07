#pragma once
#include "ReferenceSourceRevisionService.h"

namespace hypha::reference_audition::revision_detail
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
inline std::atomic<void (*) (const RuntimeSource&)> sourceReaderOpenHookForTest { nullptr };
#endif
// Some codecs pad a short input read with zeros and still report success. Keep
// the chunk verdict at the reader boundary, for playback, analysis and Blind.
class VerifiedReader final : public juce::AudioFormatReader
{
public:
    VerifiedReader (juce::AudioFormatManager& formatsIn, std::unique_ptr<juce::AudioFormatReader> reader,
                    std::shared_ptr<VerifiedInput> inputIn, std::shared_ptr<void> ownerIn, const RuntimeSource& sourceIn)
        : AudioFormatReader (nullptr, reader->getFormatName()), formats (formatsIn), decoder (std::move (reader)),
          verifiedInput (std::move (inputIn)), owner (std::move (ownerIn)), source (sourceIn)
    {
        sampleRate = decoder->sampleRate; bitsPerSample = decoder->bitsPerSample;
        lengthInSamples = decoder->lengthInSamples; numChannels = decoder->numChannels;
        usesFloatingPointData = decoder->usesFloatingPointData; metadataValues = decoder->metadataValues;
    }
    bool readSamples (int* const* destination, int channels, int offset, juce::int64 start, int frames) override
    {
        if (needsReopen && !reopen()) { ++failedReads; return false; }
        const bool read = decoder->readSamples (destination, channels, offset, start, frames);
        if (verifiedInput->takeReadFailure())
        { needsReopen = true; ++failedReads; service().requestAudit (source); return false; }
        needsReopen = !read; if (!read) ++failedReads; return read;
    }
    std::uint64_t failedReadCount() const noexcept { return failedReads; }
    juce::AudioChannelSet getChannelLayout() override
    { return decoder ? decoder->getChannelLayout() : AudioFormatReader::getChannelLayout(); }
private:
    bool reopen()
    {
        decoder.reset();
        verifiedInput->takeReadFailure();
        if (!verifiedInput->restart()) { verifiedInput->takeReadFailure(); service().requestAudit (source); return false; }
        decoder.reset (formats.createReaderFor (std::make_unique<SharedInput> (verifiedInput, owner)));
        const bool failed = verifiedInput->takeReadFailure();
        if (failed) service().requestAudit (source);
        if (!decoder || failed || std::abs (decoder->sampleRate - sampleRate) > 0.001 || decoder->numChannels != numChannels
            || decoder->lengthInSamples != lengthInSamples || decoder->bitsPerSample != bitsPerSample
            || decoder->usesFloatingPointData != usesFloatingPointData) return false;
        needsReopen = false; return true;
    }
    juce::AudioFormatManager& formats;
    std::unique_ptr<juce::AudioFormatReader> decoder;
    std::shared_ptr<VerifiedInput> verifiedInput;
    std::shared_ptr<void> owner;
    RuntimeSource source;
    bool needsReopen = false;
    std::uint64_t failedReads = 0;
};
inline std::unique_ptr<juce::AudioFormatReader> sourceReader (juce::AudioFormatManager& formats,
    std::unique_ptr<juce::InputStream> stream, const RuntimeSource& source, juce::String* failure = nullptr)
{
    const auto* shared = dynamic_cast<SharedInput*> (stream.get());
    auto verified = shared ? std::dynamic_pointer_cast<VerifiedInput> (shared->sharedStream()) : nullptr;
    auto owner = shared ? shared->lifetime() : std::shared_ptr<void> {};
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
    if (verified) if (auto hook = sourceReaderOpenHookForTest.load()) hook (source);
#endif
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (std::move (stream)));
    const bool refused = verified && verified->takeReadFailure();
    if (refused) service().requestAudit (source);
    if (!reader || refused)
    {
        if (failure) *failure = refused ? "reference_source_revalidating" : "reference_source_decode_failed";
        return {};
    }
    if (!verified) return reader;
    return std::make_unique<VerifiedReader> (formats, std::move (reader), std::move (verified), std::move (owner), source);
}
inline std::unique_ptr<juce::AudioFormatReader> snapshotReader (juce::AudioFormatManager& formats, const RuntimeSource& source,
                                                             juce::String* failure = nullptr)
{ return sourceReader (formats, snapshotInput (source), source, failure); }
}
