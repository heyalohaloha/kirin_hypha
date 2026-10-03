#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"

// H10: A／B／C／V の B（REF）。B を押すと B の画面にして B を鳴らす。B SET と曲は B の画面で選ぶ。
using namespace hypha::reference_ui::runtime_view;

void KirinHyphaEditor::wireReferenceRoles()
{
    referenceView.onSelectRef = [this]
    {
        if (liveCompareHoldBlocksAudition()) return;
        processorRef.selectReferenceVisualSlot (3);
        if (processorRef.selectReferenceRef()) return;
        const auto latest = processorRef.referenceAuditionSnapshot();
        const auto& slot = latest.referenceSelection ? *latest.referenceSelection : latest;
        const auto failure = matchFailureText (slot.matchFailure);
        const auto step = slotStep (slot, latest.transportPlaying);
        showToast (failure.isNotEmpty() ? failure : step == hypha::reference_ui::SourceStep::ready
            ? "B could not switch at this playhead. A remains live; retry when B is ready."
            : "B: " + hypha::reference_ui::stepText (step));
    };
    referenceView.onSelectSong = [this] (const juce::String& id)
    { if (! processorRef.selectReferenceSong (id)) showToast ("Song selection was not changed"); };
    referenceView.onSelectSongSet = [this] (const juce::String& id)
    { if (! processorRef.selectReferenceSongSet (id)) showToast ("B set selection was not changed"); };
}

void KirinHyphaEditor::applyReferenceRoles (hypha::reference_ui::State& state,
                                            const hypha::reference_audition::Snapshot& runtime)
{
    const auto& slot = runtime.referenceSelection ? *runtime.referenceSelection : runtime;
    state.referenceReady = processorRef.heartbeatLive() && runtime.referenceReady;
    state.referenceArmable = runtime.referenceArmable;
    const auto count = juce::String (static_cast<int> (runtime.songSets.size()));
    for (const auto& set : runtime.songSets)
    {
        state.songSets.push_back ({ set.id, set.name + "   " + juce::String (set.rank) + " / " + count });
        if (set.id == runtime.selectedSongSetId)
            for (const auto& song : set.songs)
                state.songs.push_back ({ song.id, song.label + (song.requiresPreparation ? "   PREPARING" : "") });
    }
    state.songSetId = runtime.selectedSongSetId;
    state.songId = runtime.selectedSongId;
    state.referenceStep = ! runtime.libraryReceived ? hypha::reference_ui::SourceStep::waitingForKirinOs
        : runtime.songSets.empty() ? hypha::reference_ui::SourceStep::chooseSource
        : slotStep (slot, state.aAvailable);
    // B の曲は Kirin OS の Preset ではないので、Preset を開く・表示を準備する・Genre を編集する操作は出さない
    // （直し方は曲の側から。H9）。
    if (runtime.comparisonSlot == 3) state.actionText.clear();
}
#endif
