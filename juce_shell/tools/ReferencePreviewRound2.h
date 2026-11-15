#pragma once

// Synthetic B examples for the Reference layout comparison; no audio is loaded.
inline void previewRound2 (hypha::reference_ui::State& state)
{
    using namespace hypha;
    const auto scenario = juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_B", {});
    if (scenario.isEmpty()) return;
    state.comparisonSlot = 3;
    state.bSelected = false;
    state.audibleComparisonSlot = 0;
    state.action = {};
    state.actionText.clear();
    state.songSets = { { "set-1", "Reference Set" }, { "set-2", "Alternative" } };
    state.songSetId = "set-1";
    state.songs = { { "song-1", "Song 1" }, { "song-2", "Song 2" } };
    state.songId = "song-1";
    for (int i = 0; i < 2; ++i)
    {
        reference_ui::SongFact fact;
        fact.lufsI = -12.0 - i;
        fact.prepared = fact.loaded = true;
        fact.centersHz = state.cueKirin->centersHz;
        fact.medianDb = state.cueKirin->medianDb;
        for (size_t j = 0; j < fact.medianDb.size(); ++j)
            fact.medianDb[j] += static_cast<float> (i * 1.5 + std::sin (static_cast<double> (j) * 0.11 + i));
        state.songFacts.push_back (std::move (fact));
    }
    for (int i = 0; i < 3; ++i)
    {
        reference_audition::MeasuredTarget target;
        target.id = "target-" + juce::String (i + 1);
        target.name = "Target " + juce::String (i + 1);
        target.spectrum = *state.cueKirin;
        target.integrated = -12.0 - i;
        target.duration = 90.0;
        target.maximumTruePeak = -1.0;
        for (size_t j = 0; j < target.spectrum.medianDb.size(); ++j)
        {
            const auto shift = static_cast<float> (i * -1.2 + 2.5 * std::sin (static_cast<double> (j) * 0.12 + i));
            target.spectrum.medianDb[j] += shift;
            target.spectrum.p10Db[j] += shift;
            target.spectrum.p90Db[j] += shift;
        }
        state.targets.collection.items.push_back (std::move (target));
    }
    state.overlay.all = scenario != "one";
    state.overlay.used = state.overlay.all;
    state.overlay.lines = { { "song-1", 0 }, { "song-2", 1 } };
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_NEW_TARGETS", {}) == "1")
        for (int i = 0; i < 3; ++i) state.overlay.lines.push_back ({ "target-" + juce::String (i + 1), i + 2 });
    if (scenario == "one") state.targets.collection.selected = "target-2";
}
