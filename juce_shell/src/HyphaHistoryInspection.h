#pragma once

#include "HyphaCaptureHistoryPainter.h"
#include <cmath>
#include <functional>
#include <limits>

namespace hypha::history_inspection
{
inline bool strongPeak (double value) noexcept { return std::isfinite (value) && value > 0.0; }
inline juce::String peakText (double value)
{
    if (! std::isfinite (value)) return "---";
    if (value > 0.0 && value < 0.005) return "+<0.01";
    if (value < 0.0 && value > -0.005) return "-<0.01";
    return (value > 0.0 ? "+" : "") + juce::String (value, 2);
}

// This ABI retains the endpoint but not ProjectTimeline vs AudioRenderTimeline provenance.
// Never label it DAW/project time. It is a host-clock TP-window endpoint, not an exact peak.
inline juce::String positionText (const KirinMeterHistoryEntry& entry, double rate)
{
    if (! std::isfinite (rate) || rate <= 0.0) return "POSITION UNAVAILABLE";
    const auto timeline = entry.last_timeline_endpoint_samples;
    const bool known = timeline != std::numeric_limits<std::int64_t>::min();
    const double seconds = known ? static_cast<double> (timeline) / rate
                                 : static_cast<double> (entry.last_observed_frames) / rate;
    if (! std::isfinite (seconds) || std::abs (seconds) > 86400.0 * 365.0)
        return "POSITION UNAVAILABLE";
    const auto ms = static_cast<std::int64_t> (std::llround (std::abs (seconds) * 1000.0));
    return juce::String (known ? "HOST ~" : "ELAPSED ") + (seconds < 0 ? "-" : "")
        + juce::String (ms / 60000).paddedLeft ('0', 2) + ":"
        + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2) + "."
        + juce::String (ms % 1000).paddedLeft ('0', 3);
}

struct Selection
{
    std::vector<KirinMeterHistoryEntry> snapshot;
    std::optional<std::size_t> index;
    KirinChainSnapshot chainSnapshot {};
    std::vector<KirinChainPoint> chainPoints;
    std::optional<std::size_t> chainIndex;
    double sampleRate = 0.0;
    std::uint64_t epoch = 0, generation = 0;
    KirinObservatoryFrame packetFrame {};
    bool packetFrameAvailable = false;

    bool held() const noexcept
    {
        return (index && *index < snapshot.size())
            || (chainIndex && *chainIndex < chainPoints.size());
    }
    void clear()
    {
        snapshot.clear(); index.reset();
        chainSnapshot = {}; chainPoints.clear(); chainIndex.reset();
        packetFrame = {}; packetFrameAvailable = false;
    }
    bool matches (const KirinMeterSession& meter) const noexcept
    {
        return epoch == meter.measurement_epoch && generation == meter.generation
            && std::equal_to<double> {} (sampleRate, meter.sample_rate)
            && meter.state != KIRIN_METER_SESSION_EMPTY;
    }
    bool pin (const std::vector<KirinMeterHistoryEntry>& live, std::size_t at,
              const KirinMeterSession& meter,
              const KirinChainSnapshot* liveChain = nullptr,
              const std::vector<KirinChainPoint>* liveChainPoints = nullptr,
              const KirinObservatoryFrame* frame = nullptr)
    {
        if (at >= live.size() || ! std::isfinite (meter.sample_rate) || meter.sample_rate <= 0) return false;
        if (meter.state == KIRIN_METER_SESSION_EMPTY
            || live.back().measurement_epoch != meter.measurement_epoch
            || live.back().generation != meter.generation) return false;
        snapshot = live; index = at; sampleRate = meter.sample_rate;
        epoch = meter.measurement_epoch; generation = meter.generation;
        retainFrame (frame, live.back().last_observed_frames);
        copyChain (liveChain, liveChainPoints, live.back().last_observed_frames);
        selectChainAtExactEndpoint();
        return true;
    }
    bool pinChain (const std::vector<KirinMeterHistoryEntry>& live, std::size_t at,
                   const KirinMeterSession& meter, const KirinChainSnapshot& liveChain,
                   const std::vector<KirinChainPoint>& liveChainPoints,
                   const KirinObservatoryFrame* frame = nullptr)
    {
        if (live.empty() || at >= liveChainPoints.size()
            || ! std::isfinite (meter.sample_rate) || meter.sample_rate <= 0.0
            || meter.state == KIRIN_METER_SESSION_EMPTY
            || live.back().measurement_epoch != meter.measurement_epoch
            || live.back().generation != meter.generation)
            return false;
        snapshot = live; index.reset(); sampleRate = meter.sample_rate;
        epoch = meter.measurement_epoch; generation = meter.generation;
        retainFrame (frame, live.back().last_observed_frames);
        copyChain (&liveChain, &liveChainPoints, live.back().last_observed_frames);
        const auto& wanted = liveChainPoints[at];
        for (std::size_t candidate = 0; candidate < chainPoints.size(); ++candidate)
            if (samePoint (chainPoints[candidate], wanted))
            {
                chainIndex = candidate;
                return true;
            }
        clear();
        return false;
    }
    void select (std::size_t at)
    {
        if (! held() || at >= snapshot.size()) return;
        index = at;
        selectChainAtExactEndpoint();
    }
    void selectChain (std::size_t at)
    {
        if (! held() || at >= chainPoints.size()) return;
        index.reset();
        chainIndex = at;
    }
    const KirinChainPoint* selectedChain() const noexcept
    {
        return chainIndex && *chainIndex < chainPoints.size()
            ? &chainPoints[*chainIndex] : nullptr;
    }
    const KirinMeterHistoryEntry* selectedAbsolute() const noexcept
    {
        return index && *index < snapshot.size() ? &snapshot[*index] : nullptr;
    }
    std::optional<std::size_t> event (const std::vector<KirinMeterHistoryEntry>& live,
                                     double rate, int direction) const
    {
        const auto& entries = held() ? snapshot : live;
        const auto peaks = capture_history::analyseTruePeak (entries, held() ? sampleRate : rate);
        if (peaks.eventIndices.empty()) return std::nullopt;
        if (! held()) return direction < 0 ? peaks.eventIndices.back() : peaks.eventIndices.front();
        const auto observed = selectedAbsolute() != nullptr
            ? selectedAbsolute()->last_observed_frames
            : selectedChain()->post_observed;
        if (direction < 0)
        {
            for (auto it = peaks.eventIndices.rbegin(); it != peaks.eventIndices.rend(); ++it)
                if (entries[*it].last_observed_frames < observed) return *it;
        }
        else
            for (auto at : peaks.eventIndices)
                if (entries[at].last_observed_frames > observed) return at;
        return std::nullopt;
    }

private:
    void retainFrame (const KirinObservatoryFrame* frame, std::uint64_t historyCutoff)
    {
        packetFrameAvailable = frame != nullptr
            && frame->version == KIRIN_OBSERVATORY_FRAME_VERSION
            && frame->meter.measurement_epoch == epoch
            && frame->meter.generation == generation
            && std::equal_to<double> {} (frame->meter.sample_rate, sampleRate)
            && frame->meter.observed_frames >= historyCutoff;
        packetFrame = packetFrameAvailable ? *frame : KirinObservatoryFrame {};
    }
    static bool samePoint (const KirinChainPoint& left, const KirinChainPoint& right) noexcept
    {
        return left.post_epoch == right.post_epoch
            && left.post_generation == right.post_generation
            && left.post_run == right.post_run
            && left.post_observed == right.post_observed
            && left.endpoint == right.endpoint;
    }
    void copyChain (const KirinChainSnapshot* liveChain,
                    const std::vector<KirinChainPoint>* liveChainPoints,
                    std::uint64_t cutoff)
    {
        chainSnapshot = {}; chainPoints.clear(); chainIndex.reset();
        if (liveChain != nullptr && liveChainPoints != nullptr
            && liveChain->version == KIRIN_CHAIN_VERSION
            && std::equal_to<double> {} (liveChain->sample_rate, sampleRate)
            && liveChain->count == liveChainPoints->size()
            && (liveChain->status == KIRIN_CHAIN_ACTIVE
                || liveChain->status == KIRIN_CHAIN_HOLD))
        {
            chainSnapshot = *liveChain;
            chainSnapshot.post_observed = cutoff;
            for (const auto& point : *liveChainPoints)
                if (point.post_observed <= chainSnapshot.post_observed
                    && point.post_epoch == epoch
                    && point.post_generation == generation)
                    chainPoints.push_back (point);
            chainSnapshot.count = static_cast<std::uint32_t> (chainPoints.size());
        }
    }
    void selectChainAtExactEndpoint()
    {
        chainIndex.reset();
        if (! index || *index >= snapshot.size() || chainPoints.empty()) return;
        const auto observed = snapshot[*index].last_observed_frames;
        for (std::size_t at = 0; at < chainPoints.size(); ++at)
        {
            const auto& point = chainPoints[at];
            if (point.post_observed == observed
                && point.post_run == snapshot[*index].run_id)
            {
                chainIndex = at;
                return;
            }
        }
    }
};
}
