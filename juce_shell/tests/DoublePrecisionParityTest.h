#pragma once
#include "TestProcessorStorage.h"
#include "LiveTimingFixtureAccess.h"
#include "reference_rt_probe.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <new>
namespace hypha::tests::precision
{
using Processor = KirinHyphaProcessorBase;
struct InterleaveTag { using Type = std::vector<float> Processor::*; friend Type field (InterleaveTag); };
struct ScratchTag { using Type = std::vector<float> Processor::*; friend Type field (ScratchTag); };
struct FramesTag { using Type = int Processor::*; friend Type field (FramesTag); };
template <class Tag, typename Tag::Type Member> struct Access
{ friend typename Tag::Type field (Tag) { return Member; } };
template struct Access<InterleaveTag, &Processor::interleaveScratch>;
template struct Access<ScratchTag, &Processor::precisionScratch>;
template struct Access<FramesTag, &Processor::precisionScratchFrames>;
inline void check (bool ok, const char* message) { if (! ok) throw std::runtime_error (message); }
struct Host final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo info; info.setIsPlaying (true); info.setTimeInSamples (position); return info; }
    std::int64_t position = 0;
};
inline std::uint64_t dropped (Processor& processor)
{ return LiveTimingFixtureAccess::oversizedDrops (processor); }
inline void configure (Processor& processor, int channels, Host& host, bool doublePrecision)
{
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
    check (processor.setBusesLayout (layout), "precision fixture layout"); processor.setPlayHead (&host);
    processor.setProcessingPrecision (doublePrecision ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
    prepareFixtureProcessor (processor, 48000, 128);
}
inline void verifyCase (Processor::Role role, int channels)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor single (role);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor dual (role); Host host;
    check (dual.supportsDoublePrecisionProcessing(), "processor advertises double precision");
    configure (single, channels, host, false); configure (dual, channels, host, true);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> emptyFloat (channels, 0); juce::AudioBuffer<double> emptyDouble (channels, 0);
    const auto floatBefore = LiveTimingFixtureAccess::heartbeat (single), doubleBefore = LiveTimingFixtureAccess::heartbeat (dual);
    single.processBlock (emptyFloat, midi); dual.processBlock (emptyDouble, midi);
    check (LiveTimingFixtureAccess::heartbeat (single) - floatBefore == 1
        && LiveTimingFixtureAccess::heartbeat (dual) - doubleBefore == 1, "zero-frame calls advance heartbeat in both precisions");
    for (bool extraPrecision : {false, true})
    {
        juce::AudioBuffer<float> a (channels, 128); juce::AudioBuffer<double> b (channels, 128);
        std::vector<double> expected;
        for (int ch = 0; ch < channels; ++ch)
            for (int f = 0; f < 128; ++f)
            {
                const float sample = 0.125f + static_cast<float> (ch * 128 + f) / 4096.0f;
                a.setSample (ch, f, sample);
                const double precise = extraPrecision ? std::nextafter (static_cast<double> (sample), 1.0) : sample;
                b.setSample (ch, f, precise); expected.push_back (precise);
            }
        single.processBlock (a, midi);
        beginReferenceRtProbe(); dual.processBlock (b, midi); const auto heap = endReferenceRtProbe();
        check (heap == 0, "double callback has no C++ heap operations");
        const auto& measuredSingle = single.*field (InterleaveTag {});
        const auto& measuredDouble = dual.*field (InterleaveTag {});
        check (single.hasLiveInput() && dual.hasLiveInput(), "both precisions observe real active input");
        for (int ch = 0; ch < channels; ++ch)
            for (int f = 0; f < 128; ++f)
            {
                const auto at = static_cast<size_t> (f * channels + ch);
                const auto sample = a.getSample (ch, f);
                check (std::memcmp (&measuredSingle[at], &sample, sizeof (float)) == 0
                    && std::memcmp (&measuredDouble[at], &sample, sizeof (float)) == 0,
                    "both precisions feed identical float measurement samples");
                check (std::memcmp (b.getReadPointer (ch) + f, &expected[static_cast<size_t> (ch * 128 + f)], sizeof (double)) == 0,
                       "normal double A preserves every input bit");
            }
        check (dual.getLatencySamples() == 0, "double A reports zero latency"); host.position += 128;
    }
    juce::AudioBuffer<float> silent (channels, 128); silent.clear();
    juce::AudioBuffer<double> tiny (channels, 128);
    const double tinyValue = std::ldexp (1.0, -160);
    for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 128; ++f) tiny.setSample (ch, f, tinyValue);
    single.processBlock (silent, midi); dual.processBlock (tiny, midi);
    check (single.hasLiveInput() && dual.hasLiveInput(), "both precisions retain the same active Watch rest");
    host.position = 0; // A new pass clears the prior active rest in the existing Watch contract.
    single.processBlock (silent, midi); dual.processBlock (tiny, midi);
    check (! single.hasLiveInput() && ! dual.hasLiveInput(), "a fresh silent pass is inactive in both precisions");
    for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 128; ++f)
        check (std::memcmp (tiny.getReadPointer (ch) + f, &tinyValue, sizeof (double)) == 0, "sub-float A still preserves double bits");
    juce::AudioBuffer<double> special (channels, 128);
    const std::uint64_t patterns[] {0, 0x8000000000000000ULL, 0x7ff8123456789abcULL};
    for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 128; ++f)
        std::memcpy (special.getWritePointer (ch) + f, &patterns[f % 3], sizeof (double));
    beginReferenceRtProbe(); dual.processBlock (special, midi);
    check (endReferenceRtProbe() == 0, "special double samples allocate no C++ heap");
    for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < 128; ++f)
        check (std::memcmp (special.getReadPointer (ch) + f, &patterns[f % 3], sizeof (double)) == 0,
               "signed zero and NaN payload retain their original double bits");
    const auto fallback = [&] (int frames, bool missingScratch) {
        juce::AudioBuffer<double> audio (channels, frames);
        const double value = std::nextafter (0.125, 1.0);
        for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < frames; ++f) audio.setSample (ch, f, value);
        // Deliberately exercise an unavailable prepared view; this is a white-box fault fixture.
        if (missingScratch) LiveTimingFixtureAccess::setPrecisionScratchFramesForTest (dual, 0);
        const auto before = dropped (dual);
        beginReferenceRtProbe(); dual.processBlock (audio, midi); const auto heap = endReferenceRtProbe();
        check (heap == 0 && dropped (dual) - before == static_cast<std::uint64_t> (frames * channels), "unprepared or oversized double view records the exact drop without RT allocation");
        for (int ch = 0; ch < channels; ++ch) for (int f = 0; f < frames; ++f)
            check (std::memcmp (audio.getReadPointer (ch) + f, &value, sizeof (double)) == 0, "fallback preserves A bits");
    };
    fallback (262145, false); fallback (128, true);
    configure (dual, channels, host, true);
    check (! (dual.*field (ScratchTag {})).empty(), "reprepare allocates double scratch before callbacks");
    configure (dual, channels, host, false);
    // Ownership release is intentional: single precision must retain no allocated double workspace.
    check ((dual.*field (ScratchTag {})).capacity() == 0 && dual.*field (FramesTag {}) == 0, "single-precision reprepare releases double scratch");
    single.releaseResources(); dual.releaseResources();
}
inline void verifyDoublePrecisionParity()
{
    beginReferenceRtProbe();
    auto* positive = ::operator new (64);
    static_cast<volatile char*> (positive)[0] = 1;
    const auto allocations = endReferenceRtProbe();
    beginReferenceRtProbe(); ::operator delete (positive);
    const auto frees = endReferenceRtProbe();
    check (allocations > 0 && frees > 0, "positive control observes actual C++ allocation and free");
    for (auto role : {Processor::Role::Pre, Processor::Role::Post})
        for (int channels : {1, 2}) verifyCase (role, channels);
    std::cout << "double precision: PRE/POST, mono/stereo, measurement parity, bit identity, silence, drop, reprepare, RT heap PASS\n";
}
}
