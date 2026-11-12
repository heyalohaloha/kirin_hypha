#include "ReferenceCueMatch.h"
#include "ReferenceLiveWindowLoudness.h"
#include "ReferenceSourceRanges.h"

#include <algorithm>

namespace hypha::reference_audition
{
namespace
{
std::optional<CueLevel> levelOf (const RuntimeSourceRanges& ranges, const RuntimeRangeValues* range, CuePart part)
{
    if (range == nullptr || ! range->lufsIMilliLu) return std::nullopt;
    CueLevel level;
    level.part = part;
    level.integratedLoudness = static_cast<double> (*range->lufsIMilliLu) / 1000.0;
    if (range->maxTruePeakMilliDbtp) level.maximumTruePeakDbtp = static_cast<double> (*range->maxTruePeakMilliDbtp) / 1000.0;
    const auto bands = ranges.spectrumBandCentersHz.size();
    if (range->spectrumFrameCount > 0 && bands == static_cast<size_t> (KirinSpectrumMeter::bandCount)
        && range->spectrumP10MilliDbfs.size() == bands && range->spectrumMedianMilliDbfs.size() == bands
        && range->spectrumP90MilliDbfs.size() == bands)
    {
        auto spectrum = std::make_shared<KirinSpectrumWindow>();
        spectrum->centersHz = ranges.spectrumBandCentersHz;
        spectrum->frames = spectrum->wantedFrames = static_cast<int> (range->spectrumFrameCount);
        const auto toDb = [] (const std::vector<std::int64_t>& values)
        {
            std::vector<float> result;
            for (const auto value : values) result.push_back (static_cast<float> (static_cast<double> (value) / 1000.0));
            return result;
        };
        spectrum->p10Db = toDb (range->spectrumP10MilliDbfs);
        spectrum->medianDb = toDb (range->spectrumMedianMilliDbfs);
        spectrum->p90Db = toDb (range->spectrumP90MilliDbfs);
        if (range->balanceMilliDbfs)
            for (size_t band = 0; band < 4; ++band)
                spectrum->balanceDb[band] = static_cast<double> ((*range->balanceMilliDbfs)[band]) / 1000.0;
        level.spectrum = std::move (spectrum);
    }
    return level;
}
}

std::optional<CueLevel> readCueLevel (const juce::File& root, const RuntimeWorkspace& workspace,
                                      const RuntimeCandidate& candidate, const RuntimeCue& cue,
                                      const RuntimeSource& source)
{
    return readRangeLevels (root, workspace, candidate, cue, source).cue;
}

RangeLevels readRangeLevels (const juce::File& root, const RuntimeWorkspace& workspace, const RuntimeCandidate& candidate,
                             const RuntimeCue& cue, const RuntimeSource& source)
{
    RangeLevels levels;
    if (! workspace.librarySets) return levels;
    const auto verified = readSourceRanges (root, *workspace.librarySets, candidate.sourceArtifact, source);
    if (! verified || cue.sampleRateHz != verified->sampleRateHz) return levels;
    const auto& ranges = *verified;
    levels.cue = levelOf (ranges, ranges.find (cue.startSample, cue.endSample),
                          cuePartOf (ranges, cue.startSample, cue.endSample, cue.label));
    levels.song = levelOf (ranges, ranges.wholeSong(), CuePart::whole);
    return levels;
}

int cueWindowBlocks (std::int64_t cueStartSample, std::int64_t cueEndSample, std::int64_t sampleRateHz) noexcept
{
    return std::max (cueLengthBlocks (cueStartSample, cueEndSample, sampleRateHz), liveWindowBlocks);
}

int cueLengthBlocks (std::int64_t cueStartSample, std::int64_t cueEndSample, std::int64_t sampleRateHz) noexcept
{
    constexpr std::int64_t historyBlocks = 6'000;  // 10 Hz のメーター履歴の長さ（10 分）
    if (sampleRateHz <= 0 || cueEndSample <= cueStartSample) return liveWindowBlocks;
    const auto blocks = (cueEndSample - cueStartSample) * 10 / sampleRateHz;
    return static_cast<int> (std::clamp<std::int64_t> (blocks, 1, historyBlocks));
}
}
