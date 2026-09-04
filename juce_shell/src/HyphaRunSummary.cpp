#include "HyphaRunSummary.h"

#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypha::run_summary
{
namespace
{
struct WeightedRange
{
    bool available = false;
    double minimum = 0.0;
    double maximum = 0.0;
    long double weightedSum = 0.0;
    std::uint64_t weight = 0;

    void add (const KirinMeterHistoryRange& value, std::uint64_t observations)
    {
        if (! std::isfinite (value.min) || ! std::isfinite (value.max)
            || ! std::isfinite (value.mean) || observations == 0)
            return;
        minimum = available ? std::min (minimum, value.min) : value.min;
        maximum = available ? std::max (maximum, value.max) : value.max;
        weightedSum += static_cast<long double> (value.mean)
                     * static_cast<long double> (observations);
        weight += observations;
        available = true;
    }

    Range finish() const noexcept
    {
        return { available, minimum, maximum,
                 available && weight > 0
                    ? static_cast<double> (weightedSum / static_cast<long double> (weight))
                    : 0.0 };
    }
};

struct Accumulator
{
    Summary summary;
    WeightedRange momentary;
    WeightedRange shortTerm;
    WeightedRange correlation;
    WeightedRange plr;

    explicit Accumulator (const KirinMeterHistoryEntry& entry)
    {
        summary.generation = entry.generation;
        summary.runId = entry.run_id;
        summary.firstObservedFrames = entry.first_observed_frames;
        add (entry);
    }

    void add (const KirinMeterHistoryEntry& entry)
    {
        summary.lastObservedFrames = entry.last_observed_frames;
        const auto observations = static_cast<std::uint64_t> (entry.observation_count);
        summary.observationCount += observations;
        for (std::size_t channel = 0; channel < summary.clipEvents.size(); ++channel)
            summary.clipEvents[channel] += entry.clip_event_count[channel];
        momentary.add (entry.lufs_m, observations);
        shortTerm.add (entry.lufs_s, observations);
        correlation.add (entry.correlation, observations);
        plr.add (entry.plr, observations);
        if (std::isfinite (entry.true_peak.max))
        {
            summary.maximumTruePeak = summary.truePeakAvailable
                ? std::max (summary.maximumTruePeak, entry.true_peak.max)
                : entry.true_peak.max;
            summary.truePeakAvailable = true;
        }
    }

    Summary finish()
    {
        summary.momentary = momentary.finish();
        summary.shortTerm = shortTerm.finish();
        summary.correlation = correlation.finish();
        summary.plr = plr.finish();
        return summary;
    }
};

bool sameRun (const KirinMeterHistoryEntry& entry, const Accumulator& run) noexcept
{
    return entry.generation == run.summary.generation && entry.run_id == run.summary.runId;
}

juce::String number (double value, int decimals = 1)
{
    return std::isfinite (value) ? juce::String (value, decimals) : juce::String ("---");
}

juce::String durationText (const Summary& run, double sampleRate)
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0
        || run.lastObservedFrames < run.firstObservedFrames)
        return "--:--";
    const auto seconds = static_cast<double> (
        run.lastObservedFrames - run.firstObservedFrames + 1u) / sampleRate;
    const auto whole = juce::jmax (0, static_cast<int> (std::floor (seconds)));
    return juce::String (whole / 60).paddedLeft ('0', 2) + ":"
         + juce::String (whole % 60).paddedLeft ('0', 2);
}

void paintRow (juce::Graphics& g, juce::Rectangle<int> row, const Summary& run,
               double sampleRate, bool latest, bool expanded)
{
    g.setColour ((latest ? COL_LED_BLUE : COL_MUTED).withAlpha (latest ? 0.15f : 0.08f));
    g.fillRoundedRectangle (row.toFloat(), 3.0f);
    g.setColour (COL_MUTED.withAlpha (0.24f));
    g.drawHorizontalLine (row.getBottom() - 1, static_cast<float> (row.getX()),
                          static_cast<float> (row.getRight()));

    const auto runWidth = expanded ? 92 : 62;
    auto identity = row.removeFromLeft (runWidth).reduced (5, 0);
    g.setColour (latest ? COL_LED_BLUE : COL_NORMAL);
    g.setFont (monoFont (expanded ? 10.0f : 8.5f));
    g.drawFittedText ("RUN " + juce::String (run.runId)
                      + (latest ? "  LATEST" : ""), identity,
                      juce::Justification::centredLeft, 1, 0.72f);

    auto duration = row.removeFromLeft (expanded ? 62 : 45).reduced (3, 0);
    g.setColour (COL_MUTED.brighter (0.22f));
    g.drawText (durationText (run, sampleRate), duration, juce::Justification::centredLeft);

    const auto clipText = "L" + juce::String (run.clipEvents[0])
                        + " R" + juce::String (run.clipEvents[1]);
    auto clips = row.removeFromRight (expanded ? 78 : 58).reduced (3, 0);
    g.setColour ((run.clipEvents[0] + run.clipEvents[1] > 0 ? COL_SPECTRUM_POST : COL_MUTED)
                     .withAlpha (0.90f));
    g.drawFittedText (clipText, clips, juce::Justification::centredRight, 1, 0.72f);

    auto peak = row.removeFromRight (expanded ? 106 : 80).reduced (3, 0);
    g.setColour (COL_FLORA_BR.withAlpha (0.92f));
    g.drawFittedText ("TP " + (run.truePeakAvailable
                        ? number (run.maximumTruePeak) : juce::String ("---")), peak,
                      juce::Justification::centredRight, 1, 0.72f);

    const auto loudness = run.momentary.available
        ? "M " + number (run.momentary.minimum) + ".." + number (run.momentary.maximum)
        : juce::String ("M ---");
    g.setColour (COL_SPECTRUM_POST.withAlpha (0.95f));
    g.drawFittedText (loudness, row.reduced (3, 0), juce::Justification::centredLeft,
                      1, expanded ? 0.76f : 0.62f);
}
}

Result summarize (const std::vector<KirinMeterHistoryEntry>& history)
{
    Result result;
    if (history.empty()) return result;
    const auto resolution = history.front().resolution;
    constexpr auto unavailable = std::numeric_limits<std::int64_t>::min();
    bool allExact = true;
    for (const auto& entry : history)
    {
        if (entry.resolution != resolution || entry.run_id == 0
            || entry.observation_count == 0)
            return result;
        const auto firstKnown = entry.first_timeline_endpoint_samples != unavailable;
        const auto lastKnown = entry.last_timeline_endpoint_samples != unavailable;
        if (firstKnown != lastKnown
            || (firstKnown
                && entry.last_timeline_endpoint_samples
                    < entry.first_timeline_endpoint_samples))
            return result;
        allExact = allExact && firstKnown;
    }

    Accumulator current (history.front());
    auto previousEnd = history.front().last_timeline_endpoint_samples;
    for (std::size_t index = 1; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        if (! sameRun (entry, current))
        {
            result.runs.push_back (current.finish());
            current = Accumulator (entry);
        }
        else
        {
            if (entry.first_timeline_endpoint_samples != unavailable
                && previousEnd != unavailable
                && entry.first_timeline_endpoint_samples < previousEnd)
                return {};
            current.add (entry);
        }
        previousEnd = entry.last_timeline_endpoint_samples;
    }
    result.runs.push_back (current.finish());
    if (result.runs.size() > maximumCachedRuns)
        result.runs.erase (result.runs.begin(),
                           result.runs.end() - static_cast<std::ptrdiff_t> (maximumCachedRuns));
    result.exactTimeline = allExact && ! result.runs.empty();
    return result;
}

int visibleRowCount (int width) noexcept
{
    return width <= 450 ? 1 : width < 900 ? 3 : 6;
}

void paint (juce::Graphics& g, juce::Rectangle<int> area, const Result& result,
            double sampleRate)
{
    g.setColour (BG.withAlpha (0.78f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (8, 6);
    auto heading = area.removeFromTop (juce::jlimit (18, 28, area.getHeight() / 7));
    g.setFont (monoFont (area.getWidth() >= 700 ? 11.0f : 9.0f));
    g.setColour (COL_NORMAL);
    g.drawText (result.exactTimeline ? "RUNS IN VIEW" : "SESSION RUN", heading,
                juce::Justification::centredLeft);
    g.setColour (COL_MUTED);
    g.drawText ("POST FACTS", heading, juce::Justification::centredRight);
    if (result.runs.empty()) return;

    const auto rows = juce::jmin (visibleRowCount (area.getWidth()),
                                  static_cast<int> (result.runs.size()));
    const auto rowHeight = juce::jmax (20, area.getHeight() / juce::jmax (1, rows));
    const auto first = result.runs.size() - static_cast<std::size_t> (rows);
    for (int index = 0; index < rows; ++index)
    {
        auto row = area.removeFromTop (juce::jmin (rowHeight, area.getHeight()));
        paintRow (g, row, result.runs[first + static_cast<std::size_t> (index)], sampleRate,
                  index + 1 == rows, row.getWidth() >= 520);
    }
}
}
