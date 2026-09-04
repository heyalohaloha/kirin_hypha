#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    class AudioPages final
    {
    public:
        AudioPages();
        ~AudioPages();

        juce::String open (const SourceReceipt&, double hostSampleRate, int hostChannels);
        juce::String installReaderForTest (std::unique_ptr<juce::AudioFormatReader>,
                                           double hostSampleRate, int hostChannels);
        void close();

        void request (std::int64_t sourcePosition) noexcept;
        void service();
        bool readyAt (std::int64_t sourcePosition, int frames) const noexcept;
        bool render (juce::AudioBuffer<float>& destination,
                     std::int64_t sourcePosition, float linearGain) noexcept;

        bool sourceOpen() const noexcept { return openState.load (std::memory_order_acquire); }
        std::int64_t lengthInSamples() const noexcept { return sourceLength; }
        int cachedPageFrames() const noexcept { return pageFrames; }

    private:
        enum PageState : std::uint8_t { empty, loading, ready, inUse };

        struct Page
        {
            std::atomic<std::uint8_t> state { empty };
            juce::AudioBuffer<float> audio;
            std::atomic<std::int64_t> start { 0 };
            std::atomic<std::uint64_t> generation { 0 };
        };

        juce::String installReader (std::unique_ptr<juce::AudioFormatReader>,
                                    double hostSampleRate, int hostChannels);
        bool fill (std::int64_t pageStart);
        Page* acquire (std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept;
        bool containsReady (std::int64_t pageStart, std::uint64_t expectedGeneration) const noexcept;
        void release (Page*) const noexcept;
        bool retirePages();

        static constexpr size_t pageCount = 6;
        mutable std::array<Page, pageCount> pages;
        juce::AudioFormatManager formats;
        std::unique_ptr<juce::AudioFormatReader> reader;
        std::atomic<bool> openState { false };
        std::atomic<std::int64_t> requestedPosition { 0 };
        std::atomic<std::uint64_t> activeGeneration { 0 };
        std::int64_t sourceLength = 0;
        int sourceChannels = 0;
        int pageFrames = 0;
    };
}
