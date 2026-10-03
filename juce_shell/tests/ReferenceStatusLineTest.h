#pragma once

// H9: 状態の帯。全ての段階が 聴ける／準備中／できない のどれか 1 つに入り、準備中とできないには進め方か
// 直し方が 1 つ書いてある。聴いているあいだは合わせ方（追従・固定・上限で停止・原音量）を言う。
#include "ReferenceGuideContractTest.h"

#include "../src/HyphaLanguage.h"
#include "../src/HyphaReferenceStatusModel.h"

namespace hypha::tests
{
inline void verifyReferenceStatusLine()
{
    using namespace reference_guide_contract;
    using reference_ui::StatusKind;
    using Step = reference_ui::SourceStep;
    for (auto value = static_cast<int> (Step::ready); value <= static_cast<int> (Step::attention); ++value)
    {
        const auto step = static_cast<Step> (value);
        const auto kind = reference_ui::kindOf (step);
        const auto text = reference_ui::stepText (step);
        require ((step == Step::ready) == (kind == StatusKind::ready),
                 "only a ready step reads as audible: " + text);
        require (step == Step::ready || (text.isNotEmpty() && text != "Ready"
                                         && i18n::translate (text, i18n::Language::japanese) != text),
                 "every waiting or unavailable step says one way forward, in both languages: " + text);
    }
    require (reference_ui::statusColour (StatusKind::ready) != reference_ui::statusColour (StatusKind::waiting)
                 && reference_ui::statusColour (StatusKind::waiting) != reference_ui::statusColour (StatusKind::unable),
             "the three kinds have three colours");

    // 聴いているとき：どの役を、どう合わせて聴いているか。
    using Tracking = reference_audition::TrackingState;
    auto playing = named ("ready");
    playing.bSelected = true;
    playing.separateComparisons = true;
    const auto pre = juce::String (juce::CharPointer_UTF8 ("  /  PRE \xce\x94 PAUSED"));
    for (const auto& [slot, tracking, original, expected] : {
             std::tuple { 1, Tracking::following, false, "V FOLLOWING A (LAST 10 S)" },
             std::tuple { 2, Tracking::fixed, false, "C MATCHED AND FIXED" },
             std::tuple { 3, Tracking::stoppedCeiling, false, "B FOLLOW STOPPED AT THE CEILING" },
             std::tuple { 2, Tracking::none, true, "C ORIGINAL LEVEL" } })
    {
        playing.audibleComparisonSlot = slot;
        playing.tracking = tracking;
        playing.originalAudition = original;
        const auto line = reference_ui::referenceStatusLine (playing);
        require (line.kind == StatusKind::ready && line.text == juce::String (expected) + pre,
                 "an audible role says how its level is held: " + line.text);
        require (i18n::translate (line.text, i18n::Language::japanese) != line.text,
                 "the audible line reads in Japanese: " + line.text);
    }

    // 待っている切替は準備中、止まった切替はできない（選び直すのが直し方）。
    using Stage = reference_audition::PendingAuditionView::Stage;
    auto pending = named ("ready");
    pending.separateComparisons = true;
    pending.pendingAudition.slot = 3;
    pending.pendingAudition.stage = Stage::play;
    auto line = reference_ui::referenceStatusLine (pending);
    require (line.kind == StatusKind::waiting && line.text == "B WAIT / PLAY DAW", "a queued B waits for the DAW");
    pending.pendingAudition.stage = Stage::startFailed;
    line = reference_ui::referenceStatusLine (pending);
    require (line.kind == StatusKind::unable && line.text.endsWith ("SELECT AGAIN"),
             "a stopped switch says to select again");

    // B の画面で B SET が無い：Kirin OS で B SET を出すのが直し方。
    auto noSet = named ("ready");
    noSet.separateComparisons = true;
    noSet.libraryReceived = true;
    noSet.comparisonSlot = 3;
    noSet.songSets.clear();
    line = reference_ui::referenceStatusLine (noSet);
    require (line.kind == StatusKind::unable && line.text == "B: RANK A B SET FOR HYPHA IN KIRIN OS"
                 && i18n::translate (line.text, i18n::Language::japanese) != line.text,
             "a B page with no B set says how to make one");

    // Kirin OS を待つ段階は、Kirin OS が閉じていればできない（開くのが直し方）。
    auto closed = named ("ready");
    closed.separateComparisons = true;
    closed.comparisonSlot = 2;
    closed.checkStep = Step::waitingForKirinOs;
    closed.osOnline = true;
    require (reference_ui::referenceStatusLine (closed).kind == StatusKind::waiting, "a running Kirin OS is waited for");
    closed.osOnline = false;
    require (reference_ui::referenceStatusLine (closed).kind == StatusKind::unable, "a closed Kirin OS has to be opened");

    // Blind の間は今の文のまま：どれが鳴っているかも、追従も言わない。
    auto blind = playing;
    blind.blindPhase = reference_ui::BlindPhase::active;
    blind.transportPlaying = true;
    blind.status = "BLIND / SOURCE IDENTITY HIDDEN";
    line = reference_ui::referenceStatusLine (blind);
    require (line.kind == StatusKind::ready && line.text == blind.status, "Blind keeps its own line");
    blind.blindPhase = reference_ui::BlindPhase::invalidated;
    require (reference_ui::referenceStatusLine (blind).kind == StatusKind::unable, "a stopped Blind cannot be heard");
}
}
