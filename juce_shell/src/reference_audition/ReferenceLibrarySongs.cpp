#include "ReferenceLibrarySongs.h"

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

void applyLibrarySongEntries (RuntimeWorkspace& workspace)
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
        }
}
}
