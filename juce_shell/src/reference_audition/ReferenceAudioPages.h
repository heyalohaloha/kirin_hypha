#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    struct RuntimeSource;

    class AudioPages final
    {
    public:
        AudioPages();
        ~AudioPages();

        juce::String open (const SourceReceipt&, double hostSampleRate, int hostChannels);
        juce::String open (const RuntimeSource&, double hostSampleRate, int hostChannels,
                           bool sampleRateConversionApproved);
        juce::String installReaderForTest (std::unique_ptr<juce::AudioFormatReader>,
                                           double hostSampleRate, int hostChannels,
                                           bool sampleRateConversionApproved = false);
        void close();

        void request (std::int64_t sourcePosition) noexcept;
        void setPinnedCue (std::int64_t cueStart, std::int64_t cueEnd,
                           bool loopEnabled) noexcept;
        void service();
        bool readyAt (std::int64_t sourcePosition, int frames) const noexcept;
        bool render (juce::AudioBuffer<float>& destination,
                     std::int64_t sourcePosition, float linearGain) noexcept;
        bool renderCue (juce::AudioBuffer<float>& destination,
                        std::int64_t sourcePosition,
                        std::int64_t cueStart, std::int64_t cueEnd,
                        bool loopEnabled, float linearGain) noexcept;

       #if defined(KIRIN_REFERENCE_AUDIO_PAGES_TEST_HOOK)
        using AcquireOwnershipHook = void (*) (AudioPages&);
        static void setAcquireOwnershipHookForTest (AcquireOwnershipHook) noexcept;
       #endif

        bool sourceOpen() const noexcept { return openState.load (std::memory_order_acquire); }
        std::int64_t lengthInSamples() const noexcept
        {
            return sourceLength.load (std::memory_order_acquire);
        }
        int cachedPageFrames() const noexcept
        {
            return pageFrames.load (std::memory_order_acquire);
        }

    private:
        enum PageState : std::uint8_t { empty, loading, ready, inUse };
        static constexpr size_t pageCount = 6;

        struct Page
        {
            std::atomic<std::uint8_t> state { empty };
            juce::AudioBuffer<float> audio;
            std::atomic<std::int64_t> start { 0 };
            std::atomic<std::uint64_t> generation { 0 };
        };

        juce::String installReader (std::unique_ptr<juce::AudioFormatReader>,
                                    double hostSampleRate, int hostChannels,
                                    bool sampleRateConversionApproved);
        bool fill (std::int64_t pageStart,
                   const std::array<std::int64_t, pageCount>& protectedStarts,
                   size_t protectedCount, std::uint64_t generation);
        bool fillConverted (Page&, std::int64_t pageStart);
        Page* acquire (std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept;
        bool containsReady (std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept;
        void release (Page*) const noexcept;
        bool renderRegion (juce::AudioBuffer<float>& destination,
                           std::int64_t sourcePosition,
                           std::int64_t regionStart, std::int64_t regionEnd,
                           bool loopEnabled, float linearGain) noexcept;
        bool retirePages();

        mutable std::array<Page, pageCount> pages;
        juce::AudioFormatManager formats;
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::AudioBuffer<float> conversionInput;
        std::atomic<bool> openState { false };
        std::atomic<std::int64_t> requestedPosition { 0 };
        std::atomic<std::uint64_t> activeGeneration { 0 };
        std::atomic<std::int64_t> sourceLength { 0 };
        std::atomic<int> sourceChannels { 0 };
        std::atomic<int> pageFrames { 0 };
        double sourceSampleRate = 0.0;
        double outputSampleRate = 0.0;
        bool sampleRateConversion = false;
        std::int64_t pinnedCueStart = 0;
        std::int64_t pinnedCueEnd = 0;
        bool pinnedCueLoops = false;
    };
}
