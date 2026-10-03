#pragma once

// H9: 状態の帯。全ての段階が 聴ける／準備中／できない のどれか 1 つに入り、準備中とできないには進め方か
// 直し方が 1 つ書いてある。聴いているあいだは合わせ方（追従・固定・上限で停止・原音量）を言う。
#include "ReferenceGuideContractTest.h"

#include "../src/HyphaLanguage.h"
#include "../src/HyphaReferenceStatusModel.h"
#include "../src/HyphaReferencePreparationWatch.h"

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

    // H6: 準備中を終わらない状態にしない。上限（Kirin OS の応答 5 秒、確認・読み込み・準備 10 秒、位置合わせは
    // 再生 30 秒ぶん、A の音量は再生 10 秒ぶん）を超えたら「できない」と理由・直し方。段階・役が変われば数え直す。
    reference_ui::PreparationWatch watch;
    require (watch.observe (2, Step::preparing, false, true, true, 100.0).isEmpty()
                 && watch.observe (2, Step::preparing, false, true, true, 109.0).isEmpty()
                 && watch.observe (2, Step::preparing, false, true, true, 110.5) == "NOT PREPARED IN 10 S / OPEN THE SOURCE IN KIRIN OS",
             "preparing over 10 s becomes unavailable with its fix");
    require (watch.observe (2, Step::loadingAudio, false, true, true, 111.0).isEmpty(), "a new step starts a new count");
    require (watch.observe (3, Step::loadingAudio, false, true, true, 125.0).isEmpty(), "another role starts a new count");
    for (double now = 200.0; now <= 260.0; now += 0.5)  // 位置合わせ：再生していない間は数えない
        require (watch.observe (1, Step::aligning, false, true, now > 230.0, now).isEmpty(),
                 "aligning counts only the time the DAW plays");
    require (watch.observe (1, Step::aligning, false, true, true, 261.0) == "NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN",
             "30 s of play without a match becomes unavailable");
    require (watch.observe (2, Step::waitingForKirinOs, false, false, false, 300.0).isEmpty()
                 && watch.observe (2, Step::waitingForKirinOs, false, false, false, 320.0).isEmpty()
                 && watch.observe (2, Step::waitingForKirinOs, false, true, false, 326.0) == "KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS",
             "a running Kirin OS that does not answer in 5 s is reported (a closed one already says open it)");
    require (watch.observe (2, Step::playDaw, false, true, false, 400.0).isEmpty()
                 && watch.observe (2, Step::playDaw, false, true, false, 900.0).isEmpty(),
             "waiting for the user's own action has no time limit");
    for (const auto* reason : { "A LEVEL NOT MEASURED IN 10 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
                                "KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS", "SOURCE NOT VERIFIED IN 10 S / CHECK THE SOURCE IN KIRIN OS",
                                "AUDIO NOT LOADED IN 10 S / PLAY FROM ANOTHER POSITION", "NOT PREPARED IN 10 S / OPEN THE SOURCE IN KIRIN OS",
                                "NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN" })
        require (i18n::translate (juce::String ("V: ") + reason, i18n::Language::japanese) != juce::String ("V: ") + reason
                     && ! i18n::translate (juce::String ("V: ") + reason, i18n::Language::japanese).containsIgnoreCase ("NOT"),
                 "every overdue reason reads in Japanese with its fix");
    auto overdue = named ("ready");
    overdue.separateComparisons = true;
    overdue.comparisonSlot = 1;
    overdue.versionStep = Step::aligning;
    overdue.preparationOverdue = "NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN";
    const auto overdueLine = reference_ui::referenceStatusLine (overdue);
    require (overdueLine.kind == StatusKind::unable && overdueLine.text == "V: NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN",
             "an overdue wait turns the status line unavailable with the role's reason and fix");
}
}
