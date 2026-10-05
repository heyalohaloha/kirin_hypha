#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include "HyphaReferenceRuntimeView.h"
#include "HyphaReferencePendingUI.h"

// A／B／C／V の B（REF）。B を押すと B の画面にして B を鳴らす。B SET と曲は B の画面で選ぶ。
using namespace hypha::reference_ui::runtime_view;

void KirinHyphaEditor::wireReferenceRoles()
{
    referenceView.onOpenLarge = [this] (int slot) { openReferenceLarge (slot); };
    referenceView.onSelectRef = [this]
    {
        if (outputRefused (hypha::output_owner::Activity::audition)) return;
        processorRef.selectReferenceVisualSlot (3);
        if (processorRef.selectReferenceRef()) return;
        const auto latest = processorRef.referenceAuditionSnapshot();
        const auto& slot = latest.referenceSelection ? *latest.referenceSelection : latest;
        if (offerReferenceLowerA (3, slot)) return;  // 上限超え：A を下げて合わせる承認を出す
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
    // C の MATCH をもう一度。合わせられないときは理由を言う（R-28）。
    referenceView.onMatch = [this]
    {
        using Result = hypha::reference_audition::RematchResult;
        switch (processorRef.rematchReferenceCheck())
        {
            case Result::matched: break;
            case Result::notPlaying: showToast ("C is not playing. Press C to play it matched."); break;
            case Result::original: showToast ("This Check plays at its original level."); break;
            case Result::peakMatch: showToast ("This Check matches True Peak when C starts. Press A, then C."); break;
            case Result::levelUnavailable: showToast ("Play A for the Cue length (up to 30 s), then MATCH again."); break;
            case Result::ceilingExceeded:  // 承認すれば A を下げて合わせ直す（C は今の gain のまま鳴っている）
            {
                const auto latest = processorRef.referenceAuditionSnapshot();
                if (! offerReferenceLowerA (2, latest.checkSelection ? *latest.checkSelection : latest))
                    showToast ("MATCH exceeds the safe level. The current gain is kept.");
                break;
            }
        }
    };
}

void KirinHyphaEditor::openReferenceLarge (int slot)
{
    // 300% 未満で C・V を押したら 300% に広げてその役の画面を開く。鳴らすのはもう一度押したとき。
    // まだ鳴らせない（準備中・選んでいない）ときは「押せば鳴る」と言わない（理由は開いた画面の状態の行）。
    const auto& state = referenceView.state();
    const bool playable = slot == 1 ? hypha::reference_ui::canHearVersion (state) || hypha::reference_ui::canQueueSource (state, true)
                                    : hypha::reference_ui::canHearCheck (state) || hypha::reference_ui::canQueueSource (state, false);
    setSize (900, 600);
    if (slot == 1) showToast (playable ? "V opened at 300%. Press V to listen." : "V opened at 300%.");
    else showToast (playable ? "C opened at 300%. Press C to listen." : "C opened at 300%.");
}

void KirinHyphaEditor::applyReferenceRoles (hypha::reference_ui::State& state,
                                            const hypha::reference_audition::Snapshot& runtime)
{
    const auto& slot = runtime.referenceSelection ? *runtime.referenceSelection : runtime;
    state.referenceReady = processorRef.heartbeatLive() && runtime.referenceReady;
    const auto& version = runtime.versionSelection ? *runtime.versionSelection : runtime;
    const auto& check = runtime.checkSelection ? *runtime.checkSelection : runtime;
    const auto& audibleRole = runtime.audibleComparisonSlot == 1 ? version : runtime.audibleComparisonSlot == 3 ? slot : check;
    // 聴いている役の合わせ方（追従か固定か）。Blind には追従を持ち込まない。
    state.tracking = runtime.bSelected && runtime.blindPhase == hypha::reference_audition::BlindPhase::inactive
        ? audibleRole.tracking : hypha::reference_audition::TrackingState::none;
    state.referenceArmable = runtime.referenceArmable;
    // Kirin OS の準備の状態（曲の選択の ID の最後が候補の ID）。
    const auto preparationOf = [&runtime] (const juce::String& selectionId) {
        return runtime.libraryPreparation != nullptr
            ? runtime.libraryPreparation->find (selectionId.fromLastOccurrenceOf ("/", false, false))
            : hypha::reference_audition::RuntimeSongPreparation {};
    };
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
                fact.preparation = preparationOf (song.id);
                if (index < set.facts.size())
                {
                    const auto& source = set.facts[index];
                    fact.lufsI = source.lufsI;
                    fact.centersHz = source.spectrumCentersHz;
                    fact.medianDb = source.spectrumMedianDb;
                    fact.part = source.part;
                    fact.partStartSeconds = source.partStartSeconds;
                    fact.partEndSeconds = source.partEndSeconds;
                }
                state.songFacts.push_back (std::move (fact));
            }
    }
    state.songSetId = runtime.selectedSongSetId;
    state.songId = runtime.selectedSongId;
    state.songSetsIssue = runtime.songSetsIssue;
    // Kirin OS のセットの一部を読めなかったら一度だけ知らせる（R-28：Kirin OS で選んだものが黙って消えない）。
    // 全部を読めないときは B の画面の状態の行が言う。
    if (runtime.songSetsIssue.isNotEmpty() && ! runtime.songSets.empty() && referenceSetsIssueShown != runtime.songSetsIssue)
        showToast ("Some Kirin OS sets were not read. Update Kirin OS and Hypha.");
    referenceSetsIssueShown = runtime.songSetsIssue;
    state.referenceStep = ! runtime.libraryReceived ? hypha::reference_ui::SourceStep::waitingForKirinOs
        : runtime.songSets.empty() ? hypha::reference_ui::SourceStep::chooseSource
        : slotStep (slot, state.aAvailable);
    // 同じ定義・同じ区間・同じ音量で比べる値（A の窓は観測スレッド、C の Cue の値は C の役から）。
    const auto& checkRole = runtime.checkSelection ? *runtime.checkSelection : runtime;
    if (runtime.visualTimeline) state.aKirin = runtime.visualTimeline->aKirin;
    state.cueKirin = checkRole.cueSpectrum;
    state.cueMeasurement = checkRole.detailedMeasurement;
    state.cuePart = checkRole.cuePart;
    state.cueLoudness = checkRole.cueLevelAvailable ? checkRole.cueIntegratedLoudness : std::numeric_limits<double>::quiet_NaN();
    // Cue の値がまだ無い（Kirin OS が区間をまだ測っていない）あいだは、MATCH と同じく C の曲全体の値で比べる。
    // Kirin OS の曲全体のスペクトルは Cue の値と同じ定義（2026-10-04：FREQ の線と曲全体を、音量を合わせずに
    // 重ねていた）。4 帯域は曲全体には無いので「—」。
    if (! state.cueKirin && checkRole.detailedMeasurement && checkRole.detailedMeasurement->spectrum
        && std::isfinite (checkRole.sourceIntegratedLoudness) && checkRole.sourceIntegratedLoudness < 0.0)
    {
        const auto& whole = *checkRole.detailedMeasurement->spectrum;
        auto window = std::make_shared<hypha::reference_audition::KirinSpectrumWindow>();
        window->centersHz = whole.bandCentersHz;
        const auto toDb = [] (const std::vector<std::int64_t>& values)
        {
            std::vector<float> result;
            for (const auto value : values) result.push_back (static_cast<float> (static_cast<double> (value) / 1000.0));
            return result;
        };
        window->p10Db = toDb (whole.p10Millidbfs);
        window->medianDb = toDb (whole.medianMillidbfs);
        window->p90Db = toDb (whole.p90Millidbfs);
        window->frames = window->wantedFrames = 1;
        if (window->medianDb.size() == window->centersHz.size() && window->p10Db.size() == window->centersHz.size()
            && window->p90Db.size() == window->centersHz.size() && ! window->centersHz.empty())
        {
            state.cueKirin = std::move (window);
            state.cueLoudness = checkRole.sourceIntegratedLoudness;
            state.cuePart = hypha::reference_audition::CuePart::whole;
        }
    }
    state.cueStartSeconds = checkRole.cueStartSeconds;
    state.cueEndSeconds = checkRole.cueEndSeconds;
    state.sourceDurationSeconds = checkRole.sourceDurationSeconds;
    state.cueLoops = checkRole.cueLoops;
    state.cuePlayheadSeconds = runtime.comparisonSlot == 2 ? runtime.cuePlayheadSeconds : std::numeric_limits<double>::quiet_NaN();
    const auto aWindow = processorRef.referenceWindowLoudness (runtime.comparisonSlot);
    state.aWindowLoudness = aWindow.loudness;
    state.aWindowBlocks = aWindow.blocks;
    state.aWindowNeededBlocks = aWindow.neededBlocks;
    rankCheckSets (state, checkRole.checkSetRanks);
    // V の自動特定（Reference を開いているあいだ、どの画面でも 3 秒ごと）。Kirin OS の「同じ曲」以上で一致率の
    // 最も高い Version に AUTO と一致率を添え、V を選んでいなければ（または AUTO の選んだものより明らかに合えば）
    // V の選択だけを替える（鳴っている B・C は止めない。V が鳴っている・待っているあいだは替えない）。
    // 利用者が選んだ Version は替えない。
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs >= referenceIdentifyAtMs)
    {
        referenceVersionIdentity = processorRef.identifyReferenceVersion();
        referenceIdentifyAtMs = nowMs + 3000.0;
        if (const auto next = referenceAutoChooser.next (referenceVersionIdentity, runtime.selectedVersionId, runtime.versionAuto);
            next.isNotEmpty())
            processorRef.selectReferenceVersion (next, true);
    }
    for (auto& option : state.versions)
        if (option.id == referenceVersionIdentity.autoId)
            option.label += "   AUTO " + juce::String (referenceVersionIdentity.autoAgreement, 2);
    // 待っている役（押した後の待ちがあればその役、無ければ見ている役）の待ちが上限を超えたら、状態の行で
    // 「できない」と理由・直し方を出す。
    const bool pendingWaiting = state.pendingAudition.waiting();
    const int watchedSlot = pendingWaiting ? state.pendingAudition.slot : runtime.comparisonSlot;
    const auto watchedStep = watchedSlot == 1 ? state.versionStep : watchedSlot == 3 ? state.referenceStep : state.checkStep;
    state.rolePreparation = runtime.comparisonSlot == 3 ? preparationOf (runtime.selectedSongId)
                          : runtime.comparisonSlot == 2 ? preparationOf (check.candidateId)
                          : hypha::reference_audition::RuntimeSongPreparation {};
    const bool measuringA = pendingWaiting && state.pendingAudition.stage == hypha::reference_audition::PendingAuditionView::Stage::level;
    state.preparationOverdue = referencePreparationWatch.observe (
        watchedSlot, watchedStep, measuringA, state.osOnline, state.transportPlaying, juce::Time::getMillisecondCounterHiRes() / 1000.0);
    // B の曲は Kirin OS の Preset ではないので、Preset を開く・表示を準備する・Genre を編集する操作は出さない
    // （直し方は曲の側から）。
    if (runtime.comparisonSlot == 3) { state.actionText.clear(); state.action = {}; }
    applyReferenceLowerA (state, runtime);  // 上限超えの承認（B の画面でも出す）と、下げている量
}
#endif
