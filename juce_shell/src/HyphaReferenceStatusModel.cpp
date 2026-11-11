#include "HyphaReferenceStatusModel.h"

#include "HyphaReferenceComponent.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceRuntimeView.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaReferenceStages.h"
#include "HyphaTheme.h"

#include <cmath>

namespace hypha::reference_ui
{
// 「C PAGE · A PLAYING」：見ている画面と、鳴っている役（0 は A）。区切りの「·」で 1 つの区切りのまま（狭い行でも切れない）。
static juce::String pagePlaying (int page, int heard)
{
    return juce::String (roleLetter (page)) + " PAGE" + juce::String::fromUTF8 (u8" \u00b7 ") + roleLetter (heard) + " PLAYING";
}

// 鳴っていた役を Kirin OS の公開が変えた（1）・外した（2）ので A へ戻したことを言う行。
juce::String publicationReturnLine (int change, int slot)
{
    const auto letter = juce::String (roleLetter (slot));
    return change == 1 ? letter + (slot == 1 ? ": KIRIN OS CHANGED THE VERSION" : ": KIRIN OS CHANGED THE SECTION")
                             + " / PRESS " + letter + " TO HEAR IT"
                       : letter + ": REMOVED IN KIRIN OS / CHOOSE AGAIN";
}

static StatusLine composeStatusLine (const State& state, bool returnInFooter)
{
    using Tracking = reference_audition::TrackingState;
    // Blind の間は今の文のまま（どれが鳴っているかを言わない。追従も持ち込まない）。
    if (isBlindSession (state.blindPhase))
        return { state.blindPhase == BlindPhase::invalidated
                         || state.status == stageLine (SourceStep::sourceChanged, state.comparisonSlot)
                         || state.status == stageLine (SourceStep::sourceFormatChanged, state.comparisonSlot)
                         || state.status == stageLine (SourceStep::sourceUnopenable, state.comparisonSlot) ? StatusKind::unable
                     : state.blindPhase == BlindPhase::starting || ! state.transportPlaying
                         || state.status == "SOURCE UNAVAILABLE / RETRYING" ? StatusKind::waiting
                     : StatusKind::ready,
                 state.status };
    const auto& formatCode = state.comparisonSlot == 1 ? state.versionRejectionCode
        : state.comparisonSlot == 3 ? state.referenceRejectionCode : state.checkRejectionCode;
    if (! state.targetSelected && state.readiness == Readiness::rejected
        && runtime_view::formatUnreadableCode (formatCode))
        return {StatusKind::unable, runtime_view::rejectedStatus (formatCode)};
    if (state.bSelected)
    {
        const auto step = state.audibleComparisonSlot == 1 ? state.versionStep
            : state.audibleComparisonSlot == 3 ? state.referenceStep : state.checkStep;
        if (step == SourceStep::retryingSource || step == SourceStep::sourceUnavailable
            || step == SourceStep::outsideVersion || step == SourceStep::sourceChanged
            || step == SourceStep::sourceFormatChanged || step == SourceStep::sourceUnopenable)
            return { kindOf (step), stageLine (step, state.audibleComparisonSlot) };
        if (state.status == stageLine (SourceStep::sourceChanged, state.comparisonSlot)
            || state.status == stageLine (SourceStep::sourceFormatChanged, state.comparisonSlot)
            || state.status == stageLine (SourceStep::sourceUnopenable, state.comparisonSlot))
            return { StatusKind::unable, state.status };
        if (state.status == "NEXT SOURCE UNAVAILABLE / RETRYING"
            || state.status == stageLine (SourceStep::cacheSpaceUnavailable, state.comparisonSlot)
            || state.status == stageLine (SourceStep::cacheWriteFailed, state.comparisonSlot))
            return { StatusKind::waiting, state.status };
        // 選んだ役の音がまだ届かず A が鳴っている（ページの不足・再生位置の読み込み）：聴けるの札を出さず、読み込みを言う。
        if (state.status.startsWith ("LOADING ")) return { StatusKind::waiting, state.status };
        // 聴いているあいだは、何を鳴らしているか（B・C は Kirin OS の決めた区間とループ。A の位置は追わない）と、音量を
        // どう合わせているか（追従か固定か）を言う。区間が役の名前を言うので、音量の札は「LEVEL ...」だけ。PRE の Δ は止まっている。
        const auto role = state.separateComparisons ? juce::String (roleLetter (state.audibleComparisonSlot)) : juce::String ("B");
        const auto section = state.separateComparisons ? playingSection (state) : juce::String();
        const auto tag = section.isNotEmpty() ? juce::String() : role + " ";
        const bool rangeLimited = state.tracking == Tracking::following && std::abs (state.rangeShortfallDb) > 0.5 + 1.0e-9;
        const auto range = tag + juce::String (std::abs (state.rangeShortfallDb), 1)
            + (state.rangeShortfallDb > 0.0 ? " DB UNDER A (FOLLOW LIMIT)" : " DB OVER A (FOLLOW LIMIT)");
        // 区間が無い（V、区間の分からない役）ときは今までの短い札（300% の足元で Blind のボタンの横に入る長さ）。
        const auto level = state.originalAudition ? tag + "ORIGINAL LEVEL"
            : rangeLimited ? range
            : state.tracking == Tracking::following ? (section.isNotEmpty() ? juce::String ("LEVEL FOLLOWS A (LAST 10 S)") : role + " FOLLOWING A (LAST 10 S)")
            : state.tracking == Tracking::fixed && state.matchProvisional && state.aWindowNeededBlocks > 0
                ? (section.isNotEmpty() ? juce::String ("LEVEL MATCHING A ") : role + " MATCHING A ") + juce::String (state.aWindowBlocks / 10)
                    + " OF " + juce::String (state.aWindowNeededBlocks / 10) + " S"
            : state.tracking == Tracking::fixed ? (section.isNotEmpty() ? juce::String ("LEVEL MATCHED AND FIXED") : role + " MATCHED AND FIXED")
            : section.isNotEmpty() ? juce::String() : role + " AUDITION";
        const auto how = section.isEmpty() ? level : level.isEmpty() ? section : section + "  /  " + level;
        // 承認して A を下げているなら、その量も言う（足元の RETURN で戻すまで下がったまま）。足元に RETURN（+x dB）が
        // 出ていれば言わない（2 度言うと 300% の足元で切れた。2026-10-05）。
        const auto lowered = state.heldAttenuationDb < -0.05 && ! returnInFooter
            ? "  /  A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB" : juce::String {};
        // ピークの上限で届かず、上限まで上げて鳴らしている（0.5 dB を超えれば A を下げるボタンが出る）。
        const auto under = ! rangeLimited && state.peakShortfallDb > 0.05
            ? "  /  " + juce::String (state.peakShortfallDb, 1) + " DB UNDER A (PEAK LIMIT)" : juce::String {};
        // 大事な順（入りきらなければ後ろから区切りごとに省く、HyphaReferenceStatusRow.cpp）：合わせ方、A との差、
        // 下げた A、PRE の Δ。A を下げるボタンの横では行が狭いので、ボタンが直す差だけを言う。
        if (state.action.kind == ActionKind::lowerAAndPlay && state.peakShortfallDb > 0.05)
            return { StatusKind::ready, juce::String (std::max (state.peakShortfallDb, state.rangeShortfallDb), 1) + " DB UNDER A" };
        // B の画面は、Kirin OS の新しい公開を読めず前の公開で鳴らしている理由も言い続ける（2026-11-11）。
        const auto storage = (state.status == "LOW DISK SPACE / REFERENCE AVAILABLE"
            || state.status == "TEMPORARY STORAGE UNAVAILABLE / REFERENCE AVAILABLE" ? "  /  " + state.status : juce::String {})
            + (state.comparisonSlot == 3 && state.publicationIssue.isNotEmpty() ? "  /  " + state.publicationIssue : juce::String {});
        // V の Version を替えている間は、前の Version が鳴ったまま新しい Version の位置を合わせる。どちらが鳴っているかを
        // 言う。追従・ピークの上限で届かない差と保存先の知らせはそれより先に言う（入りきらなければ後ろから省く）。
        if (state.audibleComparisonSlot == 1 && state.versionPlaying.isNotEmpty() && state.versionPositioning.isNotEmpty())
        {
            const auto positioning = "V plays " + state.versionPlaying + "; positioning " + state.versionPositioning;
            const auto warnings = (rangeLimited ? "  /  " + range : juce::String {}) + under + storage;
            return { StatusKind::waiting,
                     (warnings.isEmpty() ? positioning : warnings.substring (5) + "  /  " + positioning) + lowered };
        }
        return { StatusKind::ready, how + under + lowered + storage + juce::String (juce::CharPointer_UTF8 ("  /  PRE \xce\x94 PAUSED")) };
    }
    if (state.kirinOsRequest && state.action.kind != ActionKind::none)
        return { StatusKind::unable, state.status };
    if (const auto pending = pendingAuditionText (state); pending.isNotEmpty())
    {
        // 自動で進む処理は、長く待っていても待機として表示する。
        if (state.pendingAudition.waiting() && state.pendingAudition.reasonCode == "reference_page_wait_slow")
            return { StatusKind::waiting, pending };
        if (state.pendingAudition.waiting() && state.preparationOverdue.isNotEmpty())
            return { StatusKind::waiting, juce::String (roleLetter (state.pendingAudition.slot)) + ": " + state.preparationOverdue };
        return { state.pendingAudition.waiting() ? StatusKind::waiting : StatusKind::unable, pending };
    }
    if (state.osAccess == os_access::State::unowned) return { StatusKind::unable, state.status };
    if (state.targetSelected)
    {
        const auto lowered = state.heldAttenuationDb < -0.05 && ! returnInFooter
            ? " / A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB" : juce::String {};
        return { StatusKind::ready, "DATA ONLY" + lowered };
    }
    if (! state.separateComparisons)
        return { state.readiness == Readiness::rejected ? StatusKind::unable
                 : state.readiness == Readiness::ready && state.auditionBuffered ? StatusKind::ready : StatusKind::waiting,
                 state.status };
    // B・C・V のページ：見ている役の段階を、どの役も同じ段階の表から引く（HyphaReferenceStages.h）。
    const auto slot = state.comparisonSlot;
    const auto step = slot == 1 ? state.versionStep : slot == 3 ? state.referenceStep : state.checkStep;
    const auto letter = juce::String (roleLetter (slot));
    // Kirin OS の公開が鳴っていた区間を変えた・外したので A に戻した（2026-11-11）：理由と、どうすれば聴けるかを言う。
    // 変えた後の区間・Version が鳴らせない（確かめられない・読めない）なら「押せば聴ける」と言わず、下の理由と直し方を出す。
    if (state.publicationReturn == 2
        || (state.publicationReturn == 1 && kindOf (step) != StatusKind::unable && state.readiness != Readiness::rejected))
        return { StatusKind::waiting, publicationReturnLine (state.publicationReturn, slot) };
    // B の画面：Kirin OS の新しい公開を読めず前のまま、または曲を外した（名前と直し方）。B は前の公開で鳴らせる。
    if (slot == 3 && state.publicationIssue.isNotEmpty()) return { StatusKind::unable, state.publicationIssue };
    // 見ている役の曲を Kirin OS が準備しているあいだは、Kirin OS の言う理由と進み具合を出す（確かめられない
    // 曲は「できない」と直し方）。Kirin OS が進めているので、待ちの上限より先に言う。
    if (preparationFailed (state.rolePreparation) || (step != SourceStep::ready && step != SourceStep::playDaw))
        if (const auto line = preparationLine (state.rolePreparation); line.isNotEmpty())
            return { preparationFailed (state.rolePreparation) ? StatusKind::unable : StatusKind::waiting, letter + ": " + line };
    if (state.readiness == Readiness::rejected && (step == SourceStep::waitingForKirinOs || step == SourceStep::attention))
        return { StatusKind::unable, state.status };
    const auto kind = kindOf (step);
    // 位置合わせの 30 秒超えは、一時的な「一致未確認」の段でも同じ文を出したまま待つ（数秒ごとに入れ替えない）。
    if ((kind == StatusKind::waiting || step == SourceStep::noMatchingPassage) && state.preparationOverdue.isNotEmpty())
        return { StatusKind::waiting, letter + ": " + state.preparationOverdue };
    // Kirin OS へ頼んだこと（Preset・曲の準備、Kirin OS で開く、Blind の承認）の途中は、その文（直し方はボタン）。
    if (state.kirinOsRequest) return { kind, state.status };
    // C の Cue の値がまだ無い：曲全体で比べている、または曲全体のスペクトルを出さないことを、理由と直し方で言う
    // （2026-10-06：黙って曲全体の値を使い、手がかりは凡例の「C WHOLE」だけだった）。
    if (slot == 2 && (step == SourceStep::ready || step == SourceStep::playDaw))
        if (const auto substitute = cueSubstituteLine (state.cueSubstitute); substitute.isNotEmpty())
            return { kind, substitute };
    // 文の無い理由は、状態の行にそのコードを出す（2026-10-18：B のページは案内を出さず、コードが見えなかった）。
    const auto& code = slot == 1 ? state.versionRejectionCode : slot == 3 ? state.referenceRejectionCode : state.checkRejectionCode;
    if (step == SourceStep::attention && code.isNotEmpty())
        return { kind, stageLine (step, slot).replace ("SEE THE REASON CODE", "REASON CODE " + code) };
    // 鳴らせるが鳴っていない役の画面（A が鳴っている）：どの画面で何が鳴っているかと、押せば鳴ることを言う。
    if (step == SourceStep::ready) return { kind, pagePlaying (slot, 0) + " / PRESS " + letter };
    return { kind, stageLine (step, slot) };
}

// 承認して A を下げているあいだは「A は今の音のまま」と言わず、下げた量を言う（足元の RETURN で戻すまで）。
StatusLine referenceStatusLine (const State& state, bool returnInFooter)
{
    auto line = composeStatusLine (state, returnInFooter);
    // 見ている画面と鳴っている役が違う（2026-11-11）：どの画面で何が鳴っているかを先に言う。鳴らしている役の音が
    // 届いていない間（読み込み中・ページの不足）は A が鳴る。文の頭からは推し量らない（保存先の知らせが文を替える）。
    if (state.separateComparisons && state.bSelected && ! isBlindSession (state.blindPhase)
        && state.audibleComparisonSlot >= 1 && state.audibleComparisonSlot != state.comparisonSlot)
        line.text = pagePlaying (state.comparisonSlot, state.audibleHeard ? state.audibleComparisonSlot : 0)
            + "  /  " + line.text;
    if (state.heldAttenuationDb < -0.05 && ! returnInFooter)
        line.text = line.text.replace (pagePlaying (state.comparisonSlot, 0),
                                       pagePlaying (state.comparisonSlot, 0).replace ("A PLAYING", "A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB"));
    if (state.heldAttenuationDb < -0.05)
    {
        if (returnInFooter)  // 足元の RETURN が下げた量を言う
            for (const auto* live : { "  /  A REMAINS LIVE", " / A REMAINS LIVE" })
                line.text = line.text.replace (live, "");
        line.text = line.text.replace ("A REMAINS LIVE", "A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB");
    }
    return line;
}

bool preparationFailed (const reference_audition::RuntimeSongPreparation& preparation) noexcept
{
    return preparation.reason == "source_unavailable" || preparation.reason == "analysis_failed";
}

juce::String preparationWord (const reference_audition::RuntimeSongPreparation& preparation)
{
    if (preparation.reason == "source_unavailable") return "NOT FOUND";
    if (preparation.reason == "analysis_failed") return "ANALYSIS FAILED";
    if (preparation.state != "pending") return {};
    if (preparation.step == "resolving") return "CHECKING";
    if (preparation.step == "measuring") return "MEASURING";
    if (preparation.phase == "waiting") return "WAITING";
    if (preparation.step == "queued") return preparation.ahead > 0 ? juce::String (preparation.ahead) + " AHEAD" : juce::String ("NEXT");
    return {};
}

juce::String preparationLine (const reference_audition::RuntimeSongPreparation& preparation)
{
    if (preparation.reason == "analysis_failed")
        return "KIRIN OS COULD NOT ANALYZE THE FILE / RETRY IN KIRIN OS";
    if (preparation.reason == "source_unavailable")
        return preparation.retry == "automatic" ? "KIRIN OS CANNOT FIND THE FILE / IT RETRIES ONCE SOON"
                                                : "KIRIN OS CANNOT FIND THE FILE / RETRY IN KIRIN OS";
    if (preparation.state != "pending") return {};
    if (preparation.step == "resolving") return "KIRIN OS IS CHECKING THE FILE";
    if (preparation.step == "measuring") return "KIRIN OS IS MEASURING THE SONG";
    if (preparation.phase == "waiting") return "KIRIN OS WAITS FOR ANOTHER MEASUREMENT";
    if (preparation.step != "queued") return {};
    return preparation.ahead == 0 ? juce::String ("KIRIN OS PREPARES THIS SONG NEXT")
         : preparation.ahead == 1 ? juce::String ("KIRIN OS PREPARES 1 SONG FIRST")
         : "KIRIN OS PREPARES " + juce::String (preparation.ahead) + " SONGS FIRST";
}

// 2026-10-04：状態の行を足元へ移すと、合わせ方を 2 度言う読み（「V +0.0 dB / FOLLOWING」）が状態の文を切っていた。
juce::String gainReadout (const State& state)
{
    const auto side = state.separateComparisons ? juce::String (roleLetter (state.audibleComparisonSlot)) : juce::String ("B");
    return side + " " + gainText (displayGainDb (state, state.appliedGainDb)) + " dB"
         + (state.gainLimited ? "  /  MATCH UNAVAILABLE" : "");
}

juce::Colour statusColour (StatusKind kind) noexcept
{
    return kind == StatusKind::ready ? COL_SPECTRUM_DELTA : kind == StatusKind::waiting ? COL_FLORA : COL_MUTED;
}

void paintStatusDot (juce::Graphics& g, juce::Rectangle<int> line, StatusKind kind)
{
    const juce::Graphics::ScopedSaveState saved (g);  // 点の色を後に描く文字へ残さない
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ static_cast<float> (line.getX()) + 7.0f,
                                                                         static_cast<float> (line.getCentreY()) });
    g.setColour (statusColour (kind).withAlpha (0.25f));
    g.fillEllipse (dot.expanded (2.5f));
    g.setColour (statusColour (kind));
    g.fillEllipse (dot);
}
}
