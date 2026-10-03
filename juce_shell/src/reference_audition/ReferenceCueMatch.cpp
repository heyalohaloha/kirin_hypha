#include "ReferenceCueMatch.h"
#include "ReferenceLiveWindowLoudness.h"
#include "ReferenceSourceRanges.h"

#include <algorithm>

namespace hypha::reference_audition
{
std::optional<CueLevel> readCueLevel (const juce::File& root, const RuntimeWorkspace& workspace,
                                      const RuntimeCandidate& candidate, const RuntimeCue& cue,
                                      const RuntimeSource& source)
{
    if (! workspace.librarySets) return std::nullopt;
    const RuntimeSourceRangesReceipt* receipt = nullptr;
    for (const auto& entry : workspace.librarySets->sourceRanges)
        if (entry.sourceArtifactSha256 == candidate.sourceArtifact.sha256) receipt = &entry;
    RuntimeSourceRanges ranges;
    if (receipt == nullptr || ! readReferenceSourceRanges (root, receipt->rangesArtifact, ranges)
        || ranges.sha256File != source.sourceFileSha256 || ranges.sha256Pcm != source.sourcePcmSha256
        || ranges.sampleRateHz != source.audio.sampleRateHz
        || ranges.totalSampleFrames != source.audio.totalSampleFrames
        || cue.sampleRateHz != ranges.sampleRateHz)
        return std::nullopt;
    const auto* range = ranges.find (cue.startSample, cue.endSample);
    if (range == nullptr || ! range->lufsIMilliLu) return std::nullopt;
    CueLevel level;
    level.integratedLoudness = static_cast<double> (*range->lufsIMilliLu) / 1000.0;
    if (range->maxTruePeakMilliDbtp) level.maximumTruePeakDbtp = static_cast<double> (*range->maxTruePeakMilliDbtp) / 1000.0;
    return level;
}

int cueWindowBlocks (std::int64_t cueStartSample, std::int64_t cueEndSample, std::int64_t sampleRateHz) noexcept
{
    constexpr std::int64_t historyBlocks = 6'000;  // 10 Hz のメーター履歴の長さ（10 分）
    if (sampleRateHz <= 0 || cueEndSample <= cueStartSample) return liveWindowBlocks;
    const auto blocks = (cueEndSample - cueStartSample) * 10 / sampleRateHz;
    return static_cast<int> (std::clamp<std::int64_t> (blocks, liveWindowBlocks, historyBlocks));
}
}
