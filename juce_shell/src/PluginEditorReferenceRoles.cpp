#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"

// H10: A／B／C／V の B（REF）。B を押すと B の画面にして B を鳴らす。B SET と曲は B の画面で選ぶ。
using namespace hypha::reference_ui::runtime_view;

void KirinHyphaEditor::wireReferenceRoles()
{
    referenceView.onOpenLarge = [this] (int slot) { openReferenceLarge (slot); };
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
    // H12: C の MATCH をもう一度。合わせられないときは理由を言う（R-28）。
    referenceView.onMatch = [this]
    {
        using Result = hypha::reference_audition::RematchResult;
        switch (processorRef.rematchReferenceCheck())
        {
            case Result::matched: break;
            case Result::notPlaying: showToast ("C is not playing. Press C to play it matched."); break;
            case Result::original: showToast ("This Check plays at its original level."); break;
            case Result::levelUnavailable: showToast ("Play A for the length of the Cue, then MATCH again."); break;
            case Result::ceilingExceeded: showToast ("MATCH exceeds the safe level. The current gain is kept."); break;
        }
    };
}

void KirinHyphaEditor::openReferenceLarge (int slot)
{
    // H10: 300% 未満で C・V を押したら 300% に広げてその役の画面を開く。鳴らすのはもう一度押したとき。
    setSize (900, 600);
    showToast (slot == 1 ? "V opened at 300%. Press V to listen." : "C opened at 300%. Press C to listen.");
}

void KirinHyphaEditor::applyReferenceRoles (hypha::reference_ui::State& state,
                                            const hypha::reference_audition::Snapshot& runtime)
{
    const auto& slot = runtime.referenceSelection ? *runtime.referenceSelection : runtime;
    state.referenceReady = processorRef.heartbeatLive() && runtime.referenceReady;
    const auto& version = runtime.versionSelection ? *runtime.versionSelection : runtime;
    const auto& check = runtime.checkSelection ? *runtime.checkSelection : runtime;
    const auto& audibleRole = runtime.audibleComparisonSlot == 1 ? version : runtime.audibleComparisonSlot == 3 ? slot : check;
    // H9: 聴いている役の合わせ方（追従か固定か）。Blind には追従を持ち込まない。
    state.tracking = runtime.bSelected && runtime.blindPhase == hypha::reference_audition::BlindPhase::inactive
        ? audibleRole.tracking : hypha::reference_audition::TrackingState::none;
    state.referenceArmable = runtime.referenceArmable;
    const auto count = juce::String (static_cast<int> (runtime.songSets.size()));
    for (const auto& set : runtime.songSets)
    {
        state.songSets.push_back ({ set.id, set.name + "   " + juce::String (set.rank) + " / " + count });
        if (set.id == runtime.selectedSongSetId)
            for (size_t index = 0; index < set.songs.size(); ++index)
            {
                const auto& song = set.songs[index];
                state.songs.push_back ({ song.id, song.label + (song.requiresPreparation ? "   PREPARING" : "") });
                hypha::reference_ui::SongFact fact;
                fact.prepared = ! song.requiresPreparation;
                if (index < set.facts.size())
                {
                    const auto& source = set.facts[index];
                    fact.lufsI = source.lufsI;
                    fact.centersHz = source.spectrumCentersHz;
                    fact.medianDb = source.spectrumMedianDb;
                }
                state.songFacts.push_back (std::move (fact));
            }
    }
    state.songSetId = runtime.selectedSongSetId;
    state.songId = runtime.selectedSongId;
    state.referenceStep = ! runtime.libraryReceived ? hypha::reference_ui::SourceStep::waitingForKirinOs
        : runtime.songSets.empty() ? hypha::reference_ui::SourceStep::chooseSource
        : slotStep (slot, state.aAvailable);
    // H12: 同じ定義・同じ区間・同じ音量で比べる値（A の窓は観測スレッド、C の Cue の値は C の役から）。
    const auto& checkRole = runtime.checkSelection ? *runtime.checkSelection : runtime;
    if (runtime.visualTimeline) state.aKirin = runtime.visualTimeline->aKirin;
    state.cueKirin = checkRole.cueSpectrum;
    state.cueLoudness = checkRole.cueLevelAvailable ? checkRole.cueIntegratedLoudness : std::numeric_limits<double>::quiet_NaN();
    state.cueStartSeconds = checkRole.cueStartSeconds;
    state.cueEndSeconds = checkRole.cueEndSeconds;
    state.sourceDurationSeconds = checkRole.sourceDurationSeconds;
    state.cueLoops = checkRole.cueLoops;
    state.cuePlayheadSeconds = runtime.comparisonSlot == 2 ? runtime.cuePlayheadSeconds : std::numeric_limits<double>::quiet_NaN();
    state.aWindowLoudness = processorRef.referenceWindowLoudness (runtime.comparisonSlot);
    rankCheckSets (state, checkRole.checkSetRanks);
    // H6: 見ている役の待ちが上限を超えたら、状態の行で「できない」と理由・直し方を出す。
    const auto viewedStep = runtime.comparisonSlot == 1 ? state.versionStep
                          : runtime.comparisonSlot == 3 ? state.referenceStep : state.checkStep;
    const bool measuringA = state.pendingAudition.stage == hypha::reference_audition::PendingAuditionView::Stage::level;
    state.preparationOverdue = referencePreparationWatch.observe (
        measuringA ? state.pendingAudition.slot : runtime.comparisonSlot, viewedStep, measuringA, state.osOnline,
        state.transportPlaying, juce::Time::getMillisecondCounterHiRes() / 1000.0);
    // B の曲は Kirin OS の Preset ではないので、Preset を開く・表示を準備する・Genre を編集する操作は出さない
    // （直し方は曲の側から。H9）。
    if (runtime.comparisonSlot == 3) state.actionText.clear();
}
#endif
