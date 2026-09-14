#pragma once
#include <cmath>
#include "ReferenceACaptureModel.h"
#include <limits>
#include "ReferenceRuntimeV2Measurement.h"
#include "kirin_hypha_reference_visual_ffi.h"
namespace hypha::reference_audition
{
struct VisualBinding
{
    std::shared_ptr<const RuntimeSource> source;
    std::shared_ptr<const RuntimeDetailedMeasurement> overview;
    juce::String key;
    std::int64_t hostRate = 0, hostAnchor = 0, sourceAnchor = 0, hostPosition = -1;
    int channels = 0;
    bool aligned = false, hidden = false, hostPositionValid = false;
    bool mapPosition (std::int64_t host, std::int64_t& output) const noexcept
    {
        constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 4;
        for (const auto value : {host, hostAnchor, sourceAnchor}) if (value < -limit || value > limit) return false;
        output = host - hostAnchor + sourceAnchor; return true;
    }
    double gainDb = 0.0;
    bool matched = false;
    std::shared_ptr<const ACaptureReceipt> captureEvidence;
};
struct VisualPairBin
{
    KirinReferenceVisualBin a {}, b {};
    std::uint64_t pass = 0;
};
struct VisualTimeline
{
    VisualBinding binding;
    std::shared_ptr<const ACaptureData> capture;
    std::vector<std::uint8_t> revisited;
    std::vector<VisualPairBin> bins;
    std::int64_t hop = 0;
    std::uint64_t pass = 0, revision = 0;
    bool observing = false;
    static std::int64_t outputSample (std::int64_t source, std::int64_t sourceRate, std::int64_t hostRate) noexcept
    {
        if (source < 0 || sourceRate < 8000 || sourceRate > 768000 || hostRate < 8000 || hostRate > 768000) return -1;
        const auto whole = source/sourceRate, remainder = source%sourceRate;
        const auto fraction = (remainder*hostRate+sourceRate-1)/sourceRate;
        if (whole > (std::numeric_limits<std::int64_t>::max()-fraction)/hostRate) return -1;
        return whole*hostRate+fraction;
    }
    std::int64_t boundary (size_t index) const noexcept
    {
        if (!binding.source || hop < 1 || index > size_t(std::numeric_limits<std::int64_t>::max()/hop)) return -1;
        return outputSample (std::int64_t(index)*hop, binding.source->audio.sampleRateHz, binding.hostRate);
    }
    double endpoint(size_t i) const noexcept
    { return capture ? double(capture->bins[i].offset+capture->bins[i].value.frames)/capture->rate
        : binding.source ? double(std::min(std::int64_t(i+1)*hop,binding.source->audio.totalSampleFrames))/binding.source->audio.sampleRateHz : 0; }
    double duration() const noexcept
    { return capture ? capture->duration() : binding.source ? double (binding.source->audio.totalSampleFrames) / binding.source->audio.sampleRateHz : 0.0; }
};
}
