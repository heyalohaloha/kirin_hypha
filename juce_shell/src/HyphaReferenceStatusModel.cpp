#include "HyphaReferenceStatusModel.h"

#include "HyphaReferenceComponent.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaTheme.h"

namespace hypha::reference_ui
{
StatusKind kindOf (SourceStep step) noexcept
{
    switch (step)
    {
        case SourceStep::ready: return StatusKind::ready;
        case SourceStep::waitingForKirinOs:
        case SourceStep::playDaw:
        case SourceStep::aligning:
        case SourceStep::playAnotherPassage:
        case SourceStep::verifyingSource:
        case SourceStep::loadingAudio:
        case SourceStep::preparing: return StatusKind::waiting;
        case SourceStep::registerVersion:
        case SourceStep::chooseVersion:
        case SourceStep::enableCheck:
        case SourceStep::chooseSource:
        case SourceStep::noMatchingPassage:
        case SourceStep::approveSampleRate:
        case SourceStep::outsideCue:
        case SourceStep::attention: return StatusKind::unable;
    }
    return StatusKind::unable;
}

static StatusLine composeStatusLine (const State& state)
{
    using Tracking = reference_audition::TrackingState;
    // Blind の間は今の文のまま（どれが鳴っているかを言わない。追従も持ち込まない）。
    if (isBlindSession (state.blindPhase))
        return { state.blindPhase == BlindPhase::invalidated ? StatusKind::unable
                     : state.blindPhase == BlindPhase::starting || ! state.transportPlaying ? StatusKind::waiting
                     : StatusKind::ready,
                 state.status };
    // 2026-10-03（R-12）：上限超えで、承認すれば A を下げて合わせられる（直し方はアクションの LOWER A）。
    if (state.lowerAOfferSlot != 0 && state.lowerAOfferSlot == state.comparisonSlot)
        return { StatusKind::unable, state.status };
    if (state.bSelected)
    {
        // 聴いているあいだは、どう合わせているか（追従か固定か）を言う。PRE の Δ は止まっている。
        const auto role = state.separateComparisons ? juce::String (roleLetter (state.audibleComparisonSlot)) : juce::String ("B");
        const auto how = state.originalAudition ? role + " ORIGINAL LEVEL"
            : state.tracking == Tracking::following ? role + " FOLLOWING A (LAST 10 S)"
            : state.tracking == Tracking::stoppedCeiling ? role + " FOLLOW STOPPED AT THE CEILING"
            : state.tracking == Tracking::stoppedRange ? role + " FOLLOW STOPPED 6 DB FROM MATCH"
            : state.tracking == Tracking::fixed ? role + " MATCHED AND FIXED" : role + " AUDITION";
        // 承認して A を下げているなら、その量も言う（足元の RETURN で戻すまで下がったまま）。
        const auto lowered = state.heldAttenuationDb < -0.05
            ? "  /  A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB" : juce::String {};
        return { StatusKind::ready, how + lowered + juce::String (juce::CharPointer_UTF8 ("  /  PRE \xce\x94 PAUSED")) };
    }
    if (const auto pending = pendingAuditionText (state); pending.isNotEmpty())
    {
        // H6: 待ちが上限を超えたら「できない」にして理由と直し方を出す。
        if (state.pendingAudition.waiting() && state.preparationOverdue.isNotEmpty())
            return { StatusKind::unable, juce::String (roleLetter (state.pendingAudition.slot)) + ": " + state.preparationOverdue };
        return { state.pendingAudition.waiting() ? StatusKind::waiting : StatusKind::unable, pending };
    }
    if (state.readiness == Readiness::rejected) return { StatusKind::unable, state.status };
    if (state.osAccess == os_access::State::unowned) return { StatusKind::unable, state.status };
    if (! state.separateComparisons)
        return { state.readiness == Readiness::ready && state.auditionBuffered ? StatusKind::ready : StatusKind::waiting,
                 state.status };
    if (state.comparisonSlot == 3 && state.songSets.empty())
        return { StatusKind::unable, ! state.libraryReceived ? state.status
                 : state.songSetsIssue.isNotEmpty() ? juce::String ("B: B SET NOT READ / UPDATE KIRIN OS AND HYPHA")
                 : juce::String ("B: RANK A B SET FOR HYPHA IN KIRIN OS") };
    const auto step = state.comparisonSlot == 1 ? state.versionStep
                    : state.comparisonSlot == 3 ? state.referenceStep : state.checkStep;
    // K13b: 見ている役の曲を Kirin OS が準備しているあいだは、Kirin OS の言う理由と進み具合を出す（確かめられない
    // 曲は「できない」と直し方）。Kirin OS が進めているので、待ちの上限（H6）より先に言う。
    if (step != SourceStep::ready && step != SourceStep::playDaw)
        if (const auto line = preparationLine (state.rolePreparation); line.isNotEmpty())
            return { preparationFailed (state.rolePreparation) ? StatusKind::unable : StatusKind::waiting,
                     juce::String (roleLetter (state.comparisonSlot)) + ": " + line };
    // Kirin OS を待っている段階は、Kirin OS が閉じていれば待っても進まない（開くのが直し方）。
    const auto kind = step == SourceStep::waitingForKirinOs && ! state.osOnline ? StatusKind::unable : kindOf (step);
    if (kind == StatusKind::waiting && state.preparationOverdue.isNotEmpty())  // H6
        return { StatusKind::unable, juce::String (roleLetter (state.comparisonSlot)) + ": " + state.preparationOverdue };
    return { kind, state.status };
}

// 承認して A を下げているあいだは「A は今の音のまま」と言わず、下げた量を言う（足元の RETURN で戻すまで）。
StatusLine referenceStatusLine (const State& state)
{
    auto line = composeStatusLine (state);
    if (state.heldAttenuationDb < -0.05)
        line.text = line.text.replace ("A REMAINS LIVE", "A LOWERED " + juce::String (-state.heldAttenuationDb, 1) + " DB");
    return line;
}

bool preparationFailed (const reference_audition::RuntimeSongPreparation& preparation) noexcept
{
    return preparation.state == "pending" && preparation.reason == "source_unavailable";
}

juce::String preparationWord (const reference_audition::RuntimeSongPreparation& preparation)
{
    if (preparation.state != "pending") return {};
    if (preparation.reason == "source_unavailable") return "NOT FOUND";
    if (preparation.step == "resolving") return "CHECKING";
    if (preparation.step == "measuring") return "MEASURING";
    if (preparation.phase == "waiting") return "WAITING";
    if (preparation.step == "queued") return preparation.ahead > 0 ? juce::String (preparation.ahead) + " AHEAD" : juce::String ("NEXT");
    return {};
}

juce::String preparationLine (const reference_audition::RuntimeSongPreparation& preparation)
{
    if (preparation.state != "pending") return {};
    if (preparation.reason == "source_unavailable")
        return preparation.retry == "automatic" ? "KIRIN OS CANNOT FIND THE FILE / IT RETRIES ONCE SOON"
                                                : "KIRIN OS CANNOT FIND THE FILE / RETRY IN KIRIN OS";
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
    return side + " " + fmtDelta (state.appliedGainDb + state.heldAttenuationDb) + " dB"
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
