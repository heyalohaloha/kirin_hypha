#include "CaptureHistoryContractTest.h"

#include "../src/HyphaCaptureHistoryPainter.h"
#include "../src/HyphaCaptureHistoryTruePeak.h"
#include "../src/HyphaChainSummaryText.h"
#include "../src/HyphaObservatoryContract.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Capture History contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_CAPTURE_HISTORY_REQUIRE(expression) require ((expression), #expression, __LINE__)

std::vector<KirinMeterHistoryEntry> fixture()
{
    constexpr double peaks[] { -8.0, -2.0, -7.0, -9.0, -5.0, -0.8, -6.0 };
    std::vector<KirinMeterHistoryEntry> result (sizeof (peaks) / sizeof (peaks[0]));
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 4;
        entry.run_id = index < 4u ? 1u : 2u;
        entry.first_observed_frames = (index + 1u) * 4'800u;
        entry.last_observed_frames = entry.first_observed_frames;
        entry.first_timeline_endpoint_samples = static_cast<std::int64_t> (
            entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = entry.first_timeline_endpoint_samples;
        entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        entry.clip_event_count[0] = index == 1u ? 1u : 0u;
        entry.clip_event_count[1] = index == 5u ? 2u : 0u;
        const auto loudness = -24.0 + std::sin (static_cast<double> (index)) * 3.0;
        entry.lufs_m = { loudness, loudness, loudness };
        entry.lufs_s = { loudness + 2.0, loudness + 2.0, loudness + 2.0 };
        entry.true_peak = { peaks[index] - 0.5, peaks[index], peaks[index] - 2.0 };
    }
    return result;
}

juce::Image render (const std::vector<KirinMeterHistoryEntry>& history,
                    bool delta,
                    std::optional<std::size_t> hovered = std::nullopt,
                    const KirinMeterSession* meter = nullptr,
                    const KirinChainSnapshot* chain = nullptr,
                    const std::vector<KirinChainPoint>* chainPoints = nullptr)
{
    juce::Image image (juce::Image::ARGB, 500, 130, true);
    juce::Graphics graphics (image);
    capture_history::paint (graphics, image.getBounds(), history, delta, 48'000.0,
                            presentation::forEditor (500, 333), hovered, {}, meter,
                            chain, chainPoints);
    return image;
}

int changedPixels (const juce::Image& first, const juce::Image& second)
{
    KIRIN_CAPTURE_HISTORY_REQUIRE (first.getBounds() == second.getBounds());
    int changed = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            changed += first.getPixelAt (x, y).getARGB()
                    != second.getPixelAt (x, y).getARGB();
    return changed;
}

// Pixels of a cool hue. The page is warm except for the cyan family (true peak, the NOW dot).
int coolPixels (const juce::Image& image)
{
    int count = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto c = image.getPixelAt (x, y);
            count += c.getAlpha() > 0 && c.getBlue() > c.getRed() + 8 ? 1 : 0;
        }
    return count;
}

std::pair<KirinChainSnapshot, std::vector<KirinChainPoint>> chainFixture (double peak)
{
    KirinChainSnapshot snapshot {};
    snapshot.version = KIRIN_CHAIN_VERSION;
    snapshot.revision = 2u;
    snapshot.binding = 3u;
    snapshot.sample_rate = 48'000u;
    snapshot.post_observed = 33'600u;
    snapshot.status = KIRIN_CHAIN_ACTIVE;
    std::vector<KirinChainPoint> points (7u);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        auto& point = points[index];
        point.pre_generation = point.post_generation = 4u;
        point.pre_run = point.post_run = 2u;
        point.pre_observed = point.post_observed = (index + 1u) * 4'800u;
        point.pre_m = -25.0 + static_cast<double> (index) * 0.1;
        point.post_m = point.pre_m + 1.0;
        point.pre_tp = peak - 0.2;
        point.post_tp = peak;
        point.pre_severity = point.pre_tp > 0.0 ? 3u : point.pre_tp > -1.0 ? 2u : 1u;
        point.post_severity = point.post_tp > 0.0 ? 3u : point.post_tp > -1.0 ? 2u : 1u;
        point.crossing = point.pre_severity == 1u && point.post_severity > 1u ? 3u
                       : point.pre_severity > 1u && point.post_severity == 1u ? 2u
                       : point.pre_severity > 1u && point.post_severity > 1u ? 4u : 1u;
    }
    snapshot.count = static_cast<std::uint32_t> (points.size());
    return { snapshot, points };
}
}

namespace
{
// The current-loudness label shows its whole value at every editor size: the widest values it can
// hold must fit the label without being ellipsized.
void verifyCurrentLabelNeverCutsTheValue()
{
    for (const auto& preset : observatory::sizePresets)
        for (const bool inspection : { false, true })
            for (const auto* text : { "NOW  < -36", "NOW  -35.9", "NOW  +12.0" })
            {
                const auto context = presentation::forEditor (preset.width, preset.height);
                const auto font = monoFont (context, typography::TextRole::readout,
                                            typography::Composition::visualization);
                const auto inner = capture_history::currentLabelWidth (text, inspection, context) - 6.0f;
                KIRIN_CAPTURE_HISTORY_REQUIRE (text_style::ellipsizedText (text, font, inner) == text);
            }
}
}

void verifyCaptureHistoryContract()
{
    verifyCurrentLabelNeverCutsTheValue();
    static_assert (capture_history::normalizedLoudness (-36.0, false) == 0.0);
    static_assert (capture_history::normalizedLoudness (-18.0, false) == 0.5);
    static_assert (capture_history::normalizedLoudness (0.0, false) == 1.0);
    static_assert (capture_history::normalizedLoudness (-48.0, false) == 0.0);
    const auto history = fixture();
    const auto summary = capture_history::analyseTruePeak (history, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.windowMaximumIndex == 5u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (summary.windowMaximumDbtp - -0.8) < 1.0e-12);
    KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (summary.secondsBeforeEnd - 0.1) < 1.0e-12);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.eventIndices.size() == 1u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (summary.eventIndices.front() == 5u);
    auto twoEmphasisExcursions = history;
    twoEmphasisExcursions[1].true_peak.max = -0.5;
    const auto emphasized = capture_history::analyseTruePeak (
        twoEmphasisExcursions, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices.size() == 2u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices[0] == 1u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (emphasized.eventIndices[1] == 5u);

    auto belowEmphasis = history;
    for (auto& entry : belowEmphasis)
        entry.true_peak.max = juce::jmin (entry.true_peak.max, -1.0);
    const auto below = capture_history::analyseTruePeak (belowEmphasis, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (below.available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (below.eventIndices.empty());
    KIRIN_CAPTURE_HISTORY_REQUIRE (std::isfinite (below.windowMaximumDbtp));

    auto justAboveEmphasis = belowEmphasis;
    justAboveEmphasis[3].true_peak.max = -0.999;
    const auto justAbove = capture_history::analyseTruePeak (
        justAboveEmphasis, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (justAbove.eventIndices.size() == 1u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (justAbove.eventIndices.front() == 3u);

    auto noPeakFacts = history;
    for (auto& entry : noPeakFacts)
        entry.true_peak = { std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::quiet_NaN() };
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        ! capture_history::analyseTruePeak (noPeakFacts, 48'000.0).available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        // A window that stays at or below the -1 dBTP event threshold adds nothing: no number,
        // stem or glow (2026-09-24 contract).
        changedPixels (render (belowEmphasis, false), render (noPeakFacts, false)) == 0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (justAboveEmphasis, false),
                       render (belowEmphasis, false)) > 20);
    // The stem keeps the measured height on the TP axis, so a higher peak is drawn higher.
    auto higherPeak = justAboveEmphasis;
    higherPeak[3].true_peak.max = -0.2;
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (higherPeak, false), render (justAboveEmphasis, false)) > 2);
    // TP stems wear the cyan of the VU TP rail, never the champagne of the M line.
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        coolPixels (render (higherPeak, false)) > coolPixels (render (belowEmphasis, false)) + 20);
    // The TP axis covers only where stems live, -1..+3 dBTP: -0.8 and +0.3 stand at least a
    // quarter of the overlay apart, and a peak above +3 rests on the top instead of leaving it.
    {
        namespace tp = capture_history::true_peak;
        const auto overlay = tp::overlayFor ({ 40.0f, 30.0f, 420.0f, 90.0f });
        KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (tp::yFor (overlay, -1.0) - overlay.getBottom()) < 0.01f);
        KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (tp::yFor (overlay, 3.0) - overlay.getY()) < 0.01f);
        KIRIN_CAPTURE_HISTORY_REQUIRE (std::abs (tp::yFor (overlay, 7.5) - overlay.getY()) < 0.01f);
        KIRIN_CAPTURE_HISTORY_REQUIRE (
            tp::yFor (overlay, -0.8) - tp::yFor (overlay, 0.3) >= overlay.getHeight() * 0.25f);
    }
    const auto zeroRate = capture_history::analyseTruePeak (history, 0.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (zeroRate.available);
    KIRIN_CAPTURE_HISTORY_REQUIRE (zeroRate.secondsBeforeEnd == 0.0);

    const auto area = juce::Rectangle<int> (0, 0, 500, 130);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        ! capture_history::hitTest (
            area, history, { 250.0f, 10.0f }, 48'000.0).has_value());
    const auto beforeAvailableWindow = capture_history::hitTest (
        area, history, { 250.0f, 65.0f }, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (! beforeAvailableWindow.has_value());
    const auto newest = capture_history::hitTest (
        area, history, { 459.9f, 65.0f }, 48'000.0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (newest.has_value());
    KIRIN_CAPTURE_HISTORY_REQUIRE (*newest == history.size() - 1u);

    const auto factual = render (history, false);
    const auto missing = render (noPeakFacts, false);
    auto noClipFacts = history;
    for (auto& entry : noClipFacts)
        entry.clip_event_count[0] = entry.clip_event_count[1] = 0u;
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (factual, missing) > 40);
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (factual, render (noClipFacts, false)) > 10);
    KirinMeterSession surroundMeter {};
    surroundMeter.channels = 6;
    const std::array<uint8_t, 6> roles {
        KIRIN_CHANNEL_ROLE_LEFT, KIRIN_CHANNEL_ROLE_RIGHT, KIRIN_CHANNEL_ROLE_CENTRE,
        KIRIN_CHANNEL_ROLE_LFE, KIRIN_CHANNEL_ROLE_LEFT_SURROUND,
        KIRIN_CHANNEL_ROLE_RIGHT_SURROUND
    };
    std::copy (roles.begin(), roles.end(), surroundMeter.channel_positions);
    auto surroundClip = noClipFacts;
    surroundClip[3].clip_event_count[5] = 1u;
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (noClipFacts, false, std::nullopt, &surroundMeter),
                       render (surroundClip, false, std::nullopt, &surroundMeter)) > 4);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (factual, render (history, false, 5u)) > 40);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (history, true), render (noPeakFacts, true)) == 0);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        changedPixels (render (history, true), render (noClipFacts, true)) == 0);

    auto [belowChain, belowChainPoints] = chainFixture (-1.2);
    auto [crossingChain, crossingChainPoints] = chainFixture (-0.8);
    auto [strongChain, strongChainPoints] = chainFixture (0.2);
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        chain_action::summaryText (belowChain, belowChainPoints.back(), true)
            .contains ("BOTH<=-1"));
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        chain_action::summaryText (crossingChain, crossingChainPoints.back(), false)
            .contains ("POST>-1"));
    KIRIN_CAPTURE_HISTORY_REQUIRE (
        chain_action::summaryText (strongChain, strongChainPoints.back(), false)
            .contains ("POST>0"));
    const auto withoutChain = render (history, false);
    const auto belowChainImage = render (
        history, false, std::nullopt, nullptr, &belowChain, &belowChainPoints);
    const auto crossingImage = render (
        history, false, std::nullopt, nullptr, &crossingChain, &crossingChainPoints);
    const auto strongImage = render (
        history, false, std::nullopt, nullptr, &strongChain, &strongChainPoints);
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (withoutChain, belowChainImage) > 20);
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (belowChainImage, crossingImage) > 4);
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (crossingImage, strongImage) > 4);
    belowChain.status = KIRIN_CHAIN_AMBIGUOUS;
    KIRIN_CAPTURE_HISTORY_REQUIRE (changedPixels (
        withoutChain, render (history, false, std::nullopt, nullptr,
                              &belowChain, &belowChainPoints)) == 0);

    auto frozen = history;
    capture_history::retainThrough (frozen, history[4].last_observed_frames);
    KIRIN_CAPTURE_HISTORY_REQUIRE (frozen.size() == 5u);
    capture_history::retainThrough (frozen, 0u);
    KIRIN_CAPTURE_HISTORY_REQUIRE (frozen.empty());
}
}
