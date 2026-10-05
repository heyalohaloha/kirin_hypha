#pragma once

// 状態の帯。全ての段階が 聴ける／準備中／できない のどれか 1 つに入り、準備中とできないには進め方か
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
             std::tuple { 1, Tracking::stoppedRange, false, "V FOLLOW STOPPED 6 DB FROM MATCH" },
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
    // 2026-10-03（R-12）：承認して A を下げて鳴らしているあいだは、下げている量も言う。
    playing.audibleComparisonSlot = 3;
    playing.tracking = Tracking::following;
    playing.originalAudition = false;
    playing.heldAttenuationDb = -8.04;
    const auto lowered = reference_ui::referenceStatusLine (playing);
    require (lowered.kind == StatusKind::ready && lowered.text == "B FOLLOWING A (LAST 10 S)  /  A LOWERED 8.0 DB" + pre
                 && i18n::translate (lowered.text, i18n::Language::japanese).contains (juce::CharPointer_UTF8 ("A\xe3\x82\x92" "8.0 dB")),
             "a held attenuation is named while a role plays, in both languages: " + lowered.text);
    // 2026-10-05（300%）：足元の RETURN（+x dB）が下げた量を言っているときは、状態の文では言わない。
    require (reference_ui::referenceStatusLine (playing, true).text == "B FOLLOWING A (LAST 10 S)" + pre,
             "with RETURN in the footer the line does not say the held attenuation twice");
    // A との差（上限で届かない量）は下げた量より先（入りきらなければ後ろの区切りから省く）。
    {
        auto both = playing;
        both.peakShortfallDb = 0.3;
        require (reference_ui::referenceStatusLine (both).text
                     == "B FOLLOWING A (LAST 10 S)  /  0.3 DB UNDER A (PEAK LIMIT)  /  A LOWERED 8.0 DB" + pre,
                 "the line keeps its parts in order of importance");
    }
    playing.heldAttenuationDb = 0.0;
    // A に戻しても A は下がったまま：「A は今の音のまま」と言わず、下げた量を言う（2026-10-04）。
    auto heldReady = named ("ready");
    heldReady.separateComparisons = true;
    heldReady.comparisonSlot = 2;
    heldReady.checkStep = reference_ui::SourceStep::ready;
    heldReady.status = "READY / A REMAINS LIVE";
    heldReady.heldAttenuationDb = -7.24;
    const auto heldLine = reference_ui::referenceStatusLine (heldReady);
    require (heldLine.text == "READY / A LOWERED 7.2 DB"
                 && i18n::translate (heldLine.text, i18n::Language::japanese).contains (juce::CharPointer_UTF8 ("A\xe3\x82\x92" "7.2 dB")),
             "A lowered after a role still says how far, in both languages: " + heldLine.text);
    require (reference_ui::referenceStatusLine (heldReady, true).text == "READY",
             "with RETURN in the footer a held A is not said again");
    // 上限超えの承認を出している役を見ているときは、「できない」と量。直し方はボタンが言う（2026-10-05、2 度言うと
    // 300% の足元で両方切れた）。
    auto offered = named ("ready");
    offered.separateComparisons = true;
    offered.comparisonSlot = 3;
    offered.action = { reference_ui::ActionKind::lowerAAndPlay, { 3, -8.0, "b-song", 1, 1 } };
    offered.status = "B NEEDS A 8.0 DB LOWER";
    const auto offerLine = reference_ui::referenceStatusLine (offered);
    require (offerLine.kind == StatusKind::unable && offerLine.text == offered.status
                 && i18n::translate (offerLine.text, i18n::Language::japanese)
                        == juce::String (juce::CharPointer_UTF8 ("B\xe3\x81\xaf" "A\xe3\x82\x92" "8.0 dB\xe4\xb8\x8b\xe3\x81\x92\xe3\x82\x8b\xe3\x81\xa8\xe5\x90\x88\xe3\x81\x86"))
                 && i18n::translate ("LOWER A 8.0 DB & PLAY B", i18n::Language::japanese) != "LOWER A 8.0 DB & PLAY B",
             "an approval to lower A reads as unavailable with the amount, and its button says the fix, in both languages: "
                 + i18n::translate (offerLine.text, i18n::Language::japanese));
    offered.comparisonSlot = 2;  // ほかの役を見ているとき（画面は承認の文を入れない）は、その役の行のまま
    offered.status = "READY / A REMAINS LIVE";
    require (reference_ui::referenceStatusLine (offered).kind != StatusKind::unable,
             "the approval belongs to the role it was offered for");

    // 2026-10-04：上限で 0.5 dB 以下だけ届かず、上限まで上げて鳴らしているときは、その量を言う。
    {
        auto under = playing;
        under.peakShortfallDb = 0.3;
        const auto line = reference_ui::referenceStatusLine (under).text;
        require (line.contains ("0.3 DB UNDER A (PEAK LIMIT)")
                     && i18n::translate (line, i18n::Language::japanese).contains (juce::String (juce::CharPointer_UTF8 ("\xe3\x83\x94\xe3\x83\xbc\xe3\x82\xaf"))),
                 "a role played at the ceiling short of A says by how much, in both languages");
        under.peakShortfallDb = 0.0;
        require (! reference_ui::referenceStatusLine (under).text.contains ("UNDER A"), "a full match says nothing about it");
    }
    // gain の読みは鳴っている役の値だけ（合わせ方は状態の文が言う。2 度言うと状態の文が切れる）。下げた A は足す。
    playing.originalAudition = false;
    playing.audibleComparisonSlot = 1;
    playing.appliedGainDb = 4.0;
    playing.heldAttenuationDb = -2.5;
    for (const auto tracking : { Tracking::following, Tracking::fixed, Tracking::stoppedRange, Tracking::stoppedCeiling })
    {
        playing.tracking = tracking;
        require (reference_ui::gainReadout (playing) == "V +1.5 dB", "the gain readout is the playing role's gain only");
    }
    playing.gainLimited = true;
    require (reference_ui::gainReadout (playing) == "V +1.5 dB  /  MATCH UNAVAILABLE"
                 && i18n::translate (reference_ui::gainReadout (playing), i18n::Language::japanese).contains ("Gain Match"),
             "a gain that cannot match says so");
    playing.gainLimited = false;
    playing.heldAttenuationDb = 0.0;

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
    // B SET を出しているのに読めない（sets.json の形が違う）ときは、出し方ではなく更新を言う。
    noSet.songSetsIssue = "reference_library_song_set_rejected";
    line = reference_ui::referenceStatusLine (noSet);
    require (line.kind == StatusKind::unable && line.text == "B: B SET NOT READ / UPDATE KIRIN OS AND HYPHA"
                 && i18n::translate (line.text, i18n::Language::japanese) != line.text,
             "a B set Hypha could not read is not mistaken for a missing one");

    // 見ている役の曲を Kirin OS が準備しているあいだは、Kirin OS の言う理由と進み具合。確かめられない
    // 曲は「できない」と直し方。聴ける曲・Kirin OS から届いていない曲は今までどおり。
    {
        auto preparing = noSet;
        preparing.songSetsIssue.clear();
        preparing.songSets = { { "set-1", "Mastering refs   1 / 1" } };
        preparing.referenceStep = Step::preparing;
        preparing.rolePreparation = { "pending", "queued", {}, {}, "working", 2 };
        line = reference_ui::referenceStatusLine (preparing);
        require (line.kind == StatusKind::waiting && line.text == "B: KIRIN OS PREPARES 2 SONGS FIRST"
                     && i18n::translate (line.text, i18n::Language::japanese) != line.text,
                 "a song Kirin OS is preparing says how many come first");
        preparing.rolePreparation = { "pending", {}, "source_unavailable", "manual", "idle", 0 };
        line = reference_ui::referenceStatusLine (preparing);
        require (line.kind == StatusKind::unable && line.text == "B: KIRIN OS CANNOT FIND THE FILE / RETRY IN KIRIN OS"
                     && i18n::translate (line.text, i18n::Language::japanese) != line.text,
                 "a song Kirin OS cannot find says so with its fix");
        preparing.referenceStep = Step::ready;
        require (reference_ui::referenceStatusLine (preparing).text == preparing.status, "a ready song keeps its own line");
        using P = reference_audition::RuntimeSongPreparation;
        require (reference_ui::preparationWord (P { "pending", "queued", {}, {}, "working", 3 }) == "3 AHEAD"
                     && reference_ui::preparationWord (P { "pending", "resolving", {}, {}, "working", 0 }) == "CHECKING"
                     && reference_ui::preparationWord (P { "pending", {}, "source_unavailable", "automatic", "idle", 0 }) == "NOT FOUND"
                     && reference_ui::preparationWord (P { "playable", "queued", {}, {}, "working", 0 }).isEmpty()
                     && reference_ui::preparationWord (P {}).isEmpty()
                     && i18n::translate ("3 AHEAD", i18n::Language::japanese) != "3 AHEAD",
                 "the B list says what Kirin OS is doing for a song that cannot play yet");
    }

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

    // 準備中を終わらない状態にしない。上限（Kirin OS の応答 5 秒、確認・読み込み・準備 10 秒、位置合わせは
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
                 && watch.observe (2, Step::waitingForKirinOs, false, true, false, 326.0).isEmpty()
                 && watch.observe (2, Step::waitingForKirinOs, false, true, false, 331.5) == "KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS",
             "a Kirin OS that does not answer 5 s after it opens is reported (a closed one already says open it)");
    // MATCH に使う A の音量：B・V は再生 10 秒ぶん、C は A の直近が Cue の長さ（最長 30 秒）たまるまで待つので 35 秒ぶん。
    for (double now = 500.0; now <= 520.0; now += 0.5)
        require (watch.observe (2, Step::ready, true, true, true, now).isEmpty(), "C waits for up to 30 s of A");
    for (double now = 520.5; now < 535.5; now += 0.5) watch.observe (2, Step::ready, true, true, true, now);
    require (watch.observe (2, Step::ready, true, true, true, 536.0) == "A LEVEL NOT MEASURED IN 35 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
             "C's A level is overdue after 35 s of play");
    for (double now = 600.0; now <= 610.0; now += 0.5) watch.observe (1, Step::ready, true, true, true, now);
    require (watch.observe (1, Step::ready, true, true, true, 610.5) == "A LEVEL NOT MEASURED IN 10 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
             "V's A level is overdue after 10 s of play");
    require (watch.observe (2, Step::playDaw, false, true, false, 400.0).isEmpty()
                 && watch.observe (2, Step::playDaw, false, true, false, 900.0).isEmpty(),
             "waiting for the user's own action has no time limit");
    for (const auto* reason : { "A LEVEL NOT MEASURED IN 10 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
                                "A LEVEL NOT MEASURED IN 35 S OF PLAY / PLAY A LONGER, THEN SELECT AGAIN",
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
