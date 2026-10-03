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

StatusLine referenceStatusLine (const State& state)
{
    using Tracking = reference_audition::TrackingState;
    // Blind の間は今の文のまま（どれが鳴っているかを言わない。追従も持ち込まない）。
    if (isBlindSession (state.blindPhase))
        return { state.blindPhase == BlindPhase::invalidated ? StatusKind::unable
                     : state.blindPhase == BlindPhase::starting || ! state.transportPlaying ? StatusKind::waiting
                     : StatusKind::ready,
                 state.status };
    if (state.bSelected)
    {
        // 聴いているあいだは、どう合わせているか（追従か固定か）を言う。PRE の Δ は止まっている。
        const auto role = state.separateComparisons ? juce::String (roleLetter (state.audibleComparisonSlot)) : juce::String ("B");
        const auto how = state.originalAudition ? role + " ORIGINAL LEVEL"
            : state.tracking == Tracking::following ? role + " FOLLOWING A (LAST 10 S)"
            : state.tracking == Tracking::stoppedCeiling ? role + " FOLLOW STOPPED AT THE CEILING"
            : state.tracking == Tracking::stoppedRange ? role + " FOLLOW STOPPED 6 DB FROM MATCH"
            : state.tracking == Tracking::fixed ? role + " MATCHED AND FIXED" : role + " AUDITION";
        return { StatusKind::ready, how + juce::String (juce::CharPointer_UTF8 ("  /  PRE \xce\x94 PAUSED")) };
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
    // Kirin OS を待っている段階は、Kirin OS が閉じていれば待っても進まない（開くのが直し方）。
    const auto kind = step == SourceStep::waitingForKirinOs && ! state.osOnline ? StatusKind::unable : kindOf (step);
    if (kind == StatusKind::waiting && state.preparationOverdue.isNotEmpty())  // H6
        return { StatusKind::unable, juce::String (roleLetter (state.comparisonSlot)) + ": " + state.preparationOverdue };
    return { kind, state.status };
}

juce::String gainReadoutState (const State& state)
{
    using Tracking = reference_audition::TrackingState;
    return state.originalAudition || state.comparisonFallbackOriginal ? "ORIGINAL"
         : state.gainLimited ? "MATCH UNAVAILABLE"
         : state.tracking == Tracking::following ? "FOLLOWING"
         : state.tracking == Tracking::stoppedCeiling || state.tracking == Tracking::stoppedRange ? "FOLLOW STOPPED"
         : "MATCHED";
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
