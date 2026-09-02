#include "PreDisplayProjection.h"

#include <algorithm>
#include <cmath>

#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    namespace
    {
        constexpr std::int64_t kClockStaleMs = 2'000;
        juce::String rangeSeparator() { return juce::String::charToString (0x2013); }
        juce::String factSeparator() { return "  " + juce::String::charToString (0x00b7) + "  "; }

        void appendState (DisplaySnapshot& snapshot, const juce::String& state)
        {
            if (state.isEmpty())
                return;
            snapshot.stateText += snapshot.stateText.isEmpty() ? state : factSeparator() + state;
        }

        juce::String conciseNumber (double value)
        {
            if (value >= 1'000.0)
            {
                const auto kHz = value / 1'000.0;
                return juce::String (kHz, std::abs (kHz - std::round (kHz)) < 0.05 ? 0 : 1);
            }
            return juce::String (value, std::abs (value - std::round (value)) < 0.05 ? 0 : 1);
        }

        template <typename Fact>
        juce::String bandText (const Fact& item)
        {
            if (! item.hasBand)
                return {};
            if (item.lowHz >= 1'000.0)
                return conciseNumber (item.lowHz) + rangeSeparator() + conciseNumber (item.highHz) + " kHz";
            if (item.highHz >= 1'000.0)
                return conciseNumber (item.lowHz) + " Hz" + rangeSeparator()
                     + conciseNumber (item.highHz) + " kHz";
            return conciseNumber (item.lowHz) + rangeSeparator()
                 + conciseNumber (item.highHz) + " Hz";
        }

        juce::String timeText (std::int64_t nanoseconds)
        {
            const auto nonNegative = juce::jmax<std::int64_t> (0, nanoseconds);
            const auto tenths = nonNegative / 100'000'000
                              + (nonNegative % 100'000'000 >= 50'000'000 ? 1 : 0);
            const auto totalSeconds = tenths / 10;
            const auto hours = totalSeconds / 3'600;
            const auto minutes = (totalSeconds / 60) % 60;
            const auto seconds = totalSeconds % 60;
            const auto tenth = tenths % 10;
            if (hours > 0)
                return juce::String (hours) + ":" + juce::String (minutes).paddedLeft ('0', 2)
                     + ":" + juce::String (seconds).paddedLeft ('0', 2) + "." + juce::String (tenth);
            return juce::String (minutes) + ":" + juce::String (seconds).paddedLeft ('0', 2)
                 + "." + juce::String (tenth);
        }

        juce::String itemDetail (const GuidePresentationSnapshot& guide,
                                 const GuidePresentationFact& item,
                                 int overlapCount, bool includeBand = true)
        {
            juce::StringArray parts;
            if (guide.payloadKind == "masking")
                parts.add (guide.sourcePairLabel);
            else
            {
                if (item.sourceLabel.isNotEmpty()) parts.add (item.sourceLabel);
                if (item.channel.isNotEmpty() && item.channel != "mix") parts.add (item.channel.toUpperCase());
            }
            if (includeBand)
            {
                const auto band = bandText (item);
                if (band.isNotEmpty()) parts.add (band);
            }
            if (overlapCount > 0) parts.add ("+" + juce::String (overlapCount));
            return parts.joinIntoString (factSeparator());
        }

        GuidePresentationFact presentationFact (const GuideModel& guide,
                                                 const GuideItem& item,
                                                 GuideFactPhase phase)
        {
            GuidePresentationFact out;
            out.kind = guide.payloadKind == "masking"
                     ? GuidePresentationFactKind::maskingMeasuredInterval
                     : GuidePresentationFactKind::inspectEvent;
            out.phase = phase;
            out.itemId = item.itemId;
            out.selectionRef = item.selectionRef;
            out.label = item.label;
            out.sourceLabel = item.sourceLabel;
            out.channel = item.channel;
            out.frequencyBasis = item.frequencyBasis;
            out.startNs = item.startNs;
            out.endNs = item.endNs;
            out.temporalKind = item.temporalKind;
            out.lowHz = item.lowHz;
            out.highHz = item.highHz;
            out.hasBand = item.hasBand;
            out.focused = item.itemId == guide.focusEventId;
            return out;
        }

        GuidePresentationFact presentationFact (const GuideReviewSelection& selection,
                                                 GuideFactPhase phase)
        {
            GuidePresentationFact out;
            out.kind = GuidePresentationFactKind::maskingReviewSelection;
            out.phase = phase;
            out.itemId = selection.selectionId;
            out.selectionRef = selection.selectionId;
            out.startNs = selection.startNs;
            out.endNs = selection.endNs;
            out.temporalKind = TemporalFactKind::measuredInterval;
            out.lowHz = selection.lowHz;
            out.highHz = selection.highHz;
            out.hasBand = selection.hasBand;
            return out;
        }

        struct SelectedFacts
        {
            const GuideItem* primary = nullptr;
            const GuideItem* next = nullptr;
            GuideFactPhase phase = GuideFactPhase::none;
            int overlapCount = 0;
        };

        SelectedFacts selectFacts (const GuideModel& guide, std::int64_t sourceNs)
        {
            const GuideItem* chosenActive = nullptr;
            const GuideItem* chosenCue = nullptr;
            const GuideItem* chosenHeld = nullptr;
            int activeCount = 0;
            int cueCount = 0;
            int heldCount = 0;
            const auto prefer = [&guide] (const GuideItem& item, const GuideItem* chosen)
            {
                if (chosen == nullptr)
                    return true;
                const bool itemFocused = item.itemId == guide.focusEventId;
                const bool chosenFocused = chosen->itemId == guide.focusEventId;
                return (itemFocused && ! chosenFocused)
                    || (itemFocused == chosenFocused
                        && (item.startNs > chosen->startNs
                            || (item.startNs == chosen->startNs
                                && item.itemId < chosen->itemId)));
            };
            for (const auto& item : guide.items)
            {
                const bool active = containsHalfOpen (item.startNs, item.endNs, sourceNs);
                const auto cueEnd = juce::jmax (
                    item.endNs, saturatingAddNanoseconds (item.startNs, minimumCueWindowNs));
                const bool cue = ! active && containsHalfOpen (item.startNs, cueEnd, sourceNs);
                const bool held = guide.payloadKind == "inspect" && ! cue
                               && sourceNs >= cueEnd
                               && sourceNs < saturatingAddNanoseconds (
                                      item.endNs, inspectHoldWindowNs);
                if (active)
                {
                    ++activeCount;
                    if (prefer (item, chosenActive))
                        chosenActive = &item;
                }
                else if (cue)
                {
                    ++cueCount;
                    if (prefer (item, chosenCue))
                        chosenCue = &item;
                }
                else if (held)
                {
                    ++heldCount;
                    if (prefer (item, chosenHeld))
                        chosenHeld = &item;
                }
            }

            SelectedFacts out;
            if (chosenActive != nullptr)
            {
                out.primary = chosenActive;
                out.phase = GuideFactPhase::active;
                out.overlapCount = activeCount;
            }
            else if (chosenCue != nullptr)
            {
                out.primary = chosenCue;
                out.phase = GuideFactPhase::cue;
                out.overlapCount = cueCount;
            }
            else if (chosenHeld != nullptr)
            {
                out.primary = chosenHeld;
                out.phase = GuideFactPhase::held;
                out.overlapCount = heldCount;
            }
            const auto next = std::find_if (
                guide.items.begin(), guide.items.end(), [sourceNs] (const auto& item)
                {
                    return item.startNs > sourceNs;
                });
            if (next != guide.items.end())
                out.next = &*next;
            return out;
        }

        struct SelectedReviewSelections
        {
            const GuideReviewSelection* active = nullptr;
            const GuideReviewSelection* next = nullptr;
        };

        SelectedReviewSelections selectReviewSelections (
            const GuideModel& guide, std::int64_t sourceNs)
        {
            SelectedReviewSelections out;
            for (const auto& selection : guide.reviewSelections)
            {
                if (containsHalfOpen (selection.startNs, selection.endNs, sourceNs))
                {
                    if (out.active == nullptr
                        || selection.startNs > out.active->startNs
                        || (selection.startNs == out.active->startNs
                            && selection.selectionId < out.active->selectionId))
                        out.active = &selection;
                }
                else if (selection.startNs > sourceNs && out.next == nullptr)
                    out.next = &selection;
            }
            return out;
        }
    }

    GuidePresentationSnapshot projectGuidePresentation (
        const GuideModel& guide, const ClockSnapshot& clock,
        std::int64_t clockObservedAtMs, std::int64_t nowMs)
    {
        GuidePresentationSnapshot out;
        out.targetRole = guide.targetRole;
        out.guideId = guide.guideId;
        out.contentHash = guide.contentHash;
        out.payloadKind = guide.payloadKind;
        out.sourcePairLabel = guide.sourcePairLabel;
        out.revision = guide.revision;
        if (! guide.valid())
            return out;

        out.guideAvailable = true;
        out.clockPaused = ! clock.playing
                       || (clockObservedAtMs > 0
                           && nowMs - clockObservedAtMs > kClockStaleMs);
        if (guide.payloadKind == "masking" && guide.items.empty()
            && guide.reviewSelections.empty())
        {
            out.status = DisplayStatus::received;
            return out;
        }

        std::int64_t projectNs = 0;
        if (! canProjectGuideTime (clock.source)
            || ! projectSamplesToNanoseconds (
                   clock.positionSamples, clock.sampleRate, projectNs))
        {
            out.status = DisplayStatus::waitingForProjectClock;
            if (! guide.items.empty())
            {
                out.next = presentationFact (
                    guide, guide.items.front(), GuideFactPhase::next);
                out.hasNext = true;
            }
            if (! guide.reviewSelections.empty())
            {
                out.nextMaskingFocus = presentationFact (
                    guide.reviewSelections.front(), GuideFactPhase::next);
                out.hasNextMaskingFocus = true;
            }
            return out;
        }

        if (! subtractNanoseconds (projectNs, guide.sourceZeroProjectNs,
                                   out.sourcePositionNs))
        {
            out.clockState = GuideClockState::outsideGuideRange;
            out.status = DisplayStatus::waitingForProjectClock;
            return out;
        }
        out.clockState = GuideClockState::projectable;
        out.hasSourcePosition = true;

        const auto selected = selectFacts (guide, out.sourcePositionNs);
        const auto selectedReviews = selectReviewSelections (
            guide, out.sourcePositionNs);
        out.overlapCount = selected.overlapCount;
        if (selected.primary != nullptr)
        {
            out.primary = presentationFact (guide, *selected.primary, selected.phase);
            out.hasPrimary = true;
            out.status = DisplayStatus::active;
        }
        if (selected.next != nullptr)
        {
            out.next = presentationFact (guide, *selected.next, GuideFactPhase::next);
            out.hasNext = true;
        }
        if (selectedReviews.active != nullptr)
        {
            out.maskingFocus = presentationFact (
                *selectedReviews.active, GuideFactPhase::active);
            out.hasMaskingFocus = true;
            out.status = DisplayStatus::active;
        }
        if (selectedReviews.next != nullptr)
        {
            out.nextMaskingFocus = presentationFact (
                *selectedReviews.next, GuideFactPhase::next);
            out.hasNextMaskingFocus = true;
        }
        if (! out.hasPrimary && ! out.hasMaskingFocus)
            out.status = (out.hasNext || out.hasNextMaskingFocus)
                       ? DisplayStatus::next : DisplayStatus::end;
        return out;
    }

    DisplaySnapshot projectDisplay (const GuideModel& guide, const ClockSnapshot& clock,
                                    std::int64_t clockObservedAtMs, std::int64_t nowMs)
    {
        const auto presentation = projectGuidePresentation (
            guide, clock, clockObservedAtMs, nowMs);
        if (! presentation.guideAvailable)
            return {};
        DisplaySnapshot out;
        out.guideId = presentation.guideId;
        out.contentHash = presentation.contentHash;
        out.payloadKind = presentation.payloadKind;
        const juce::String heading = presentation.payloadKind == "masking"
                                   ? "MASKING" : "INSPECT";

        if (presentation.status == DisplayStatus::received)
        {
            out.status = DisplayStatus::received;
            if (guide.hasMeasuredEmptyMasking())
            {
                out.primary = "MASKING  NO TIMED ITEMS";
                out.detail = "Measured guide retained";
            }
            else
            {
                out.primary = "MASKING  RECEIVED";
                out.detail = "Legacy guide" + factSeparator() + "No timed items"
                           + factSeparator() + "Retained";
            }
            return out;
        }

        if (presentation.status == DisplayStatus::waitingForProjectClock)
        {
            out.status = DisplayStatus::waitingForProjectClock;
            out.primary = heading + "  RECEIVED";
            if (presentation.clockState == GuideClockState::outsideGuideRange)
            {
                out.detail = "Timeline outside guide range"
                           + factSeparator() + "Guide retained";
                return out;
            }
            if (presentation.hasNext || presentation.hasNextMaskingFocus)
            {
                const auto& first = presentation.hasNext
                                  ? presentation.next : presentation.nextMaskingFocus;
                const auto fact = presentation.payloadKind == "inspect"
                                    ? first.label : bandText (first);
                out.detail = "NEXT " + timeText (first.startNs)
                           + (fact.isNotEmpty() ? factSeparator() + fact : "");
            }
            else
                out.detail = "No timed items" + factSeparator() + "Guide retained";
            return out;
        }

        if (presentation.hasPrimary)
        {
            const auto& chosen = presentation.primary;
            const bool cue = chosen.phase == GuideFactPhase::cue;
            const bool held = chosen.phase == GuideFactPhase::held;
            out.status = DisplayStatus::active;
            out.sectionActive = chosen.phase == GuideFactPhase::active;
            out.cueActive = cue;
            // During an exact interval, time follows the playhead. A presentation-only cue or
            // retained INSPECT context must keep the fact's own location instead of falsely
            // relabelling it with the later playhead position.
            out.primary = heading + "  " + timeText (
                out.sectionActive ? presentation.sourcePositionNs : chosen.startNs);
            if (presentation.payloadKind == "inspect")
                out.primary += factSeparator() + chosen.label;
            else
            {
                const auto band = bandText (chosen);
                if (band.isNotEmpty()) out.primary += factSeparator() + band;
            }
            out.detail = itemDetail (
                presentation, chosen, juce::jmax (0, presentation.overlapCount - 1),
                presentation.payloadKind != "masking");
            if (cue) appendState (out, "CUE");
            if (held) appendState (out, "HELD");
        }
        else if (presentation.hasMaskingFocus)
        {
            const auto& focus = presentation.maskingFocus;
            out.status = DisplayStatus::active;
            out.sectionActive = true;
            out.primary = heading + "  " + timeText (presentation.sourcePositionNs);
            const auto band = bandText (focus);
            if (band.isNotEmpty()) out.primary += factSeparator() + band;
            out.detail = presentation.sourcePairLabel;
        }
        else if (presentation.hasNext || presentation.hasNextMaskingFocus)
        {
            const auto& next = presentation.hasNext
                             ? presentation.next : presentation.nextMaskingFocus;
            out.status = DisplayStatus::next;
            out.primary = heading + "  NEXT " + timeText (next.startNs);
            out.detail = itemDetail (presentation, next, 0);
            if (presentation.payloadKind == "inspect" && next.label.isNotEmpty())
                out.detail = next.label
                           + (out.detail.isNotEmpty() ? factSeparator() + out.detail : "");
        }
        else
        {
            out.status = DisplayStatus::end;
            out.primary = heading + "  END";
            out.detail = "Guide retained";
        }

        if (presentation.clockPaused)
            appendState (out, "PAUSED");
        return out;
    }
}
