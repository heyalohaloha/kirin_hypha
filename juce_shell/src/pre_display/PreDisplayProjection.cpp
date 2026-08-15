#include "PreDisplayProjection.h"

#include <algorithm>
#include <cmath>

#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    namespace
    {
        constexpr std::int64_t kClockStaleMs = 2'000;
        constexpr std::int64_t kInspectHoldNs = 1'000'000'000;

        juce::String compactText (const juce::String& text, int maximumCharacters)
        {
            if (text.length() <= maximumCharacters)
                return text;
            return text.substring (0, juce::jmax (1, maximumCharacters - 1)).trimEnd()
                 + juce::String::charToString (0x2026);
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

        juce::String bandText (const GuideItem& item)
        {
            if (! item.hasBand)
                return {};
            if (item.lowHz >= 1'000.0)
                return conciseNumber (item.lowHz) + "–" + conciseNumber (item.highHz) + " kHz";
            if (item.highHz >= 1'000.0)
                return conciseNumber (item.lowHz) + " Hz–" + conciseNumber (item.highHz) + " kHz";
            return conciseNumber (item.lowHz) + "–" + conciseNumber (item.highHz) + " Hz";
        }

        juce::String timeText (std::int64_t nanoseconds)
        {
            const auto tenths = juce::jmax<std::int64_t> (0, (nanoseconds + 50'000'000) / 100'000'000);
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

        juce::String itemDetail (const GuideModel& guide, const GuideItem& item,
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
            return compactText (parts.joinIntoString ("  ·  "), 48);
        }

        void compact (DisplaySnapshot& snapshot)
        {
            snapshot.primary = compactText (snapshot.primary, 48);
            snapshot.detail = compactText (snapshot.detail, 48);
        }
    }

    DisplaySnapshot projectDisplay (const GuideModel& guide, const ClockSnapshot& clock,
                                    std::int64_t clockObservedAtMs, std::int64_t nowMs)
    {
        if (! guide.valid())
            return {};
        DisplaySnapshot out;
        out.guideId = guide.guideId;
        out.contentHash = guide.contentHash;
        out.payloadKind = guide.payloadKind;
        const juce::String heading = guide.payloadKind == "masking" ? "MASKING" : "INSPECT";

        if (guide.payloadKind == "masking" && guide.items.empty())
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
                out.detail = "Legacy guide  ·  No timed items  ·  Retained";
            }
            compact (out);
            return out;
        }

        std::int64_t projectNs = 0;
        if (! canProjectGuideTime (clock.source)
            || ! projectSamplesToNanoseconds (clock.positionSamples, clock.sampleRate, projectNs))
        {
            out.status = DisplayStatus::waitingForProjectClock;
            out.primary = heading + "  RECEIVED";
            if (! guide.items.empty())
            {
                const auto& first = guide.items.front();
                const auto fact = guide.payloadKind == "inspect" ? first.label : bandText (first);
                out.detail = "NEXT " + timeText (first.startNs) + (fact.isNotEmpty() ? "  ·  " + fact : "");
            }
            else
                out.detail = "No timed items  ·  Guide retained";
            compact (out);
            return out;
        }

        std::int64_t sourceNs = 0;
        if (! subtractNanoseconds (projectNs, guide.sourceZeroProjectNs, sourceNs))
        {
            out.status = DisplayStatus::waitingForProjectClock;
            out.primary = heading + "  RECEIVED";
            out.detail = "Timeline outside guide range  ·  Guide retained";
            compact (out);
            return out;
        }

        const GuideItem* chosen = nullptr;
        bool held = false;
        int overlapCount = 0;
        for (const auto& item : guide.items)
        {
            const bool active = containsHalfOpen (item.startNs, item.endNs, sourceNs);
            const bool isHeld = guide.payloadKind == "inspect" && sourceNs >= item.endNs
                             && sourceNs < item.endNs + kInspectHoldNs;
            if (! active && ! isHeld)
                continue;
            const bool itemFocused = item.itemId == guide.focusEventId;
            const bool chosenFocused = chosen != nullptr && chosen->itemId == guide.focusEventId;
            if (chosen == nullptr
                || (itemFocused && ! chosenFocused)
                || (itemFocused == chosenFocused && item.startNs > chosen->startNs))
            {
                chosen = &item;
                held = isHeld && ! active;
            }
            ++overlapCount;
        }
        if (chosen != nullptr)
        {
            out.status = DisplayStatus::active;
            out.primary = heading + "  " + timeText (sourceNs);
            if (guide.payloadKind == "inspect")
                out.primary += "  ·  " + chosen->label;
            else
            {
                const auto band = bandText (*chosen);
                if (band.isNotEmpty()) out.primary += "  ·  " + band;
            }
            out.detail = itemDetail (guide, *chosen, juce::jmax (0, overlapCount - 1),
                                     guide.payloadKind != "masking");
            if (held) out.detail += (out.detail.isEmpty() ? "HELD" : "  ·  HELD");
        }
        else
        {
            const auto next = std::find_if (guide.items.begin(), guide.items.end(), [sourceNs] (const auto& item)
            {
                return item.startNs > sourceNs;
            });
            if (next != guide.items.end())
            {
                out.status = DisplayStatus::next;
                out.primary = heading + "  NEXT " + timeText (next->startNs);
                out.detail = itemDetail (guide, *next, 0);
                if (guide.payloadKind == "inspect" && next->label.isNotEmpty())
                    out.detail = next->label + (out.detail.isNotEmpty() ? "  ·  " + out.detail : "");
            }
            else
            {
                out.status = DisplayStatus::end;
                out.primary = heading + "  END";
                out.detail = "Guide retained";
            }
        }

        const bool clockStale = clockObservedAtMs > 0 && nowMs - clockObservedAtMs > kClockStaleMs;
        if (clockStale || ! clock.playing)
            out.detail += (out.detail.isEmpty() ? "PAUSED  ·  Guide retained" : "  ·  PAUSED");
        compact (out);
        return out;
    }
}
