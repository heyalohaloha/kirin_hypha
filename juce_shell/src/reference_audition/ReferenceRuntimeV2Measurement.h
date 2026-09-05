#pragma once

#include <map>
#include <optional>

#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    using RuntimeNullableIntegerSeries = std::vector<std::optional<std::int64_t>>;

    struct RuntimeMeasurementWaveform
    {
        std::int64_t framesPerBin = 0;
        std::vector<std::vector<std::int64_t>> samplePeakMillidbfs;
        std::vector<std::vector<std::int64_t>> rmsMillidbfs;
    };

    struct RuntimeMeasurementSpectrum
    {
        std::vector<double> bandCentersHz;
        std::vector<std::int64_t> p10Millidbfs;
        std::vector<std::int64_t> medianMillidbfs;
        std::vector<std::int64_t> p90Millidbfs;
    };

    struct RuntimeMeasurementTimeline
    {
        std::int64_t hopSamples = 0;
        std::map<std::string, RuntimeNullableIntegerSeries> series;
    };

    struct RuntimeMeasurementTransient
    {
        std::int64_t hopSamples = 0;
        std::vector<std::int64_t> onsetStrengthQ15;
    };

    struct RuntimeDetailedMeasurement
    {
        juce::String sourceFileSha256;
        juce::String sourcePcmSha256;
        RuntimeAudioFacts audio;
        std::optional<RuntimeMeasurementWaveform> waveform;
        std::optional<RuntimeMeasurementSpectrum> spectrum;
        std::optional<RuntimeMeasurementTimeline> loudness;
        std::optional<RuntimeMeasurementTimeline> dynamics;
        std::optional<RuntimeMeasurementTransient> transient;
        std::optional<RuntimeMeasurementTimeline> stereo;
    };

    struct RuntimeMeasurementLoadResult
    {
        std::shared_ptr<const RuntimeDetailedMeasurement> measurement;
        juce::String rejectionCode;

        bool accepted() const noexcept { return measurement != nullptr; }
    };

    class RuntimeV2MeasurementRepository final
    {
    public:
        explicit RuntimeV2MeasurementRepository (juce::File transportRootIn);
        RuntimeMeasurementLoadResult load (const RuntimeSource&) const;

    private:
        const juce::File root;
    };
}
