#include "ReferenceLibrarySongs.h"
#include "ReferenceSourceRanges.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>

namespace hypha::reference_audition
{
juce::String referenceSongEntryId (const juce::String& songSetId, const juce::String& candidateId)
{
    const auto key = "hypha-song:" + songSetId + ":" + candidateId;
    const auto hash = juce::SHA256 (key.toRawUTF8(), key.getNumBytesAsUTF8()).toHexString();
    return hash.substring (0, 8) + "-" + hash.substring (8, 12) + "-4" + hash.substring (13, 16)
        + "-8" + hash.substring (17, 20) + "-" + hash.substring (20, 32);
}

namespace
{
RuntimeSongFacts readSongFacts (const juce::File& root, const RuntimeLibrarySets& sets, const RuntimeCandidate& song)
{
    RuntimeSongFacts facts;
    if (! song.prepared) return facts;
    const auto receipt = std::find_if (sets.sourceRanges.begin(), sets.sourceRanges.end(), [&] (const auto& entry) {
        return entry.sourceArtifactSha256 == song.sourceArtifact.sha256;
    });
    const auto cue = std::find_if (song.cues.begin(), song.cues.end(), [&] (const auto& item) { return item.cueId == song.defaultCueId; });
    RuntimeSourceRanges ranges;
    if (receipt == sets.sourceRanges.end() || cue == song.cues.end()
        || ! readReferenceSourceRanges (root, receipt->rangesArtifact, ranges) || cue->sampleRateHz != ranges.sampleRateHz)
        return facts;
    // どの部分か（凡例）は、Cue の値の行がまだ無くても Cue の範囲と自動区間から決まる。
    facts.part = cuePartOf (ranges, cue->startSample, cue->endSample, cue->label);
    facts.partStartSeconds = static_cast<double> (cue->startSample) / static_cast<double> (ranges.sampleRateHz);
    facts.partEndSeconds = static_cast<double> (cue->endSample) / static_cast<double> (ranges.sampleRateHz);
    const auto* range = ranges.find (cue->startSample, cue->endSample);
    if (range == nullptr) return facts;
    if (range->lufsIMilliLu) facts.lufsI = static_cast<double> (*range->lufsIMilliLu) / 1000.0;
    if (range->maxTruePeakMilliDbtp) facts.maxTruePeak = static_cast<double> (*range->maxTruePeakMilliDbtp) / 1000.0;
    if (range->spectrumMedianMilliDbfs.size() == ranges.spectrumBandCentersHz.size())
    {
        facts.spectrumCentersHz = ranges.spectrumBandCentersHz;
        for (const auto value : range->spectrumMedianMilliDbfs) facts.spectrumMedianDb.push_back (static_cast<float> (value) / 1000.0f);
    }
    return facts;
}
}

void applyLibrarySongEntries (const juce::File& root, RuntimeWorkspace& workspace)
{
    auto& presets = workspace.presets;
    presets.erase (std::remove_if (presets.begin(), presets.end(), [] (const auto& preset) { return preset.songEntry; }),
                   presets.end());
    if (! workspace.librarySets) return;
    for (const auto& set : workspace.librarySets->songSets)
        for (const auto& song : set.songs)
        {
            const auto id = referenceSongEntryId (set.songSetId, song.candidateId);
            RuntimePreset entry;
            entry.songEntry = true;
            // 記録を書かない（受け取りの path・大きさが無い）。ID は Preset の ID の形にそろえる。
            entry.sourcePresetArtifact.presetId = id;
            entry.sourcePresetArtifact.revisionId = id;
            entry.name = song.displayName.substring (0, 80);
            RuntimeCheck check;
            check.checkId = id;
            check.label = "REF";
            check.mode = "audition_with_facts";
            check.viewBindings = { "spectrum_full" };
            check.comparisonMode = "loudness_match";
            check.candidates.push_back (song);
            entry.checks.push_back (std::move (check));
            presets.push_back (std::move (entry));
            workspace.librarySets->songFacts[id] = readSongFacts (root, *workspace.librarySets, song);
        }
}
}
