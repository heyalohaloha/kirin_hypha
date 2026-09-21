#pragma once

#include "kirin_hypha_reference_visual_ffi.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

namespace hypha::reference_audition
{
struct CaptureTonalSummary
{
    std::array<float, 60> p10 {}, median {}, p90 {};
    std::uint64_t validBits = 0;
    std::uint64_t frames = 0;
    int sampleRate = 0, channels = 0;
    juce::String artifactSha256, recoveryKey;
    std::int64_t artifactBytes = 0;

    bool valid() const noexcept;
};

// Worker-only accumulator. It consumes the Capture A worker's existing PCM copy and retains
// one scalar per completed FFT window and owned band. No second audio queue is introduced.
class TonalCapture final
{
public:
    TonalCapture (int sampleRate, int channels);
    ~TonalCapture();
    TonalCapture (const TonalCapture&) = delete;
    TonalCapture& operator= (const TonalCapture&) = delete;

    bool available() const noexcept { return meter != nullptr; }
    bool push (const float* interleaved, size_t sampleCount);
    CaptureTonalSummary finish() const;
    CaptureTonalSummary finishAndStore (const juce::File& transportRoot,
                                        const juce::String& captureId,
                                        const juce::String& baseCaptureSha256,
                                        std::int64_t hostStart) const;
    size_t allocatedBytes() const noexcept;
    size_t projectedArtifactBytes() const noexcept;

private:
    KirinReferenceTonalMeter* meter = nullptr;
    KirinReferenceTonalSnapshot latest {};
    std::array<std::uint64_t, 3> acceptedWindowEnd {};
    std::array<std::uint32_t, 3> fftSize {}, hopSamples {};
    std::array<std::vector<float>, 60> values;
};

void writeCaptureTonalSummary (juce::MemoryOutputStream&, const CaptureTonalSummary&);
bool readCaptureTonalSummary (
    juce::MemoryInputStream&, CaptureTonalSummary&, bool receiptEncoded = true);
}
