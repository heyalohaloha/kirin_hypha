#pragma once

#include "../src/HyphaAbsoluteComponent.h"
#include "../src/HyphaMonoSumHistory.h"
#include "../src/HyphaMonoSumPainter.h"
#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaTheme.h"
#include "SpectrumTerrainShowcase.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

// When the transport stops, the pages with a six-second history keep what was measured, dimmed,
// and withdraw only the present. And MONO's live curve does not blink between drum hits: a band
// with nothing to measure keeps its last value for up to a second, faintly, and no longer.
namespace hypha::tests
{
namespace stopped_history_contract
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Stopped history contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_STOPPED_REQUIRE(expression) \
    hypha::tests::stopped_history_contract::require ((expression), #expression, __LINE__)

inline juce::Image paintOf (juce::Component& component, int width, int height)
{
    component.setSize (width, height);
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);
    g.fillAll (BG);
    component.paintEntireComponent (g, true);
    return image;
}

inline int brightness (juce::Colour colour) noexcept
{
    return (int) colour.getRed() + (int) colour.getGreen() + (int) colour.getBlue();
}

// Pixels where `drawn` differs from `bare`, and the brightest of them in `drawn` and in `playing`.
struct Difference
{
    int pixels = 0;
    int brightestDrawn = 0;
    int brightestPlaying = 0;
};

inline Difference differenceOf (const juce::Image& drawn, const juce::Image& bare, const juce::Image& playing,
                                int fromRow, int toRow)
{
    Difference result;
    for (int y = fromRow; y < toRow; ++y)
        for (int x = 0; x < drawn.getWidth(); ++x)
        {
            const auto pixel = drawn.getPixelAt (x, y);
            if (std::abs (brightness (pixel) - brightness (bare.getPixelAt (x, y))) <= 12)
                continue;
            ++result.pixels;
            result.brightestDrawn = std::max (result.brightestDrawn, brightness (pixel));
            result.brightestPlaying = std::max (result.brightestPlaying, brightness (playing.getPixelAt (x, y)));
        }
    return result;
}

// What a stopped page draws beyond its status alone must be the history, and dimmer than playing.
// Only the plot rows count: the header's labels are the same whether playing or stopped.
inline void requireDimmedHistory (const char* page, const juce::Image& playing, const juce::Image& stopped,
                                  const juce::Image& stoppedWithoutHistory, int fromRow = -1, int toRow = -1)
{
    const auto history = differenceOf (stopped, stoppedWithoutHistory, playing,
                                       fromRow >= 0 ? fromRow : stopped.getHeight() * 35 / 100,
                                       toRow >= 0 ? toRow : stopped.getHeight());
    std::cout << "Stopped " << page << ": " << history.pixels << " history pixels, brightest "
              << history.brightestDrawn << " (playing " << history.brightestPlaying << ")\n";
    KIRIN_STOPPED_REQUIRE (history.pixels > 400);
    KIRIN_STOPPED_REQUIRE (history.brightestDrawn * 10 < history.brightestPlaying * 8);
}

inline KirinAbsoluteBatch liveBatch()
{
    KirinAbsoluteBatch batch {};
    batch.count = 60u;
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        auto& view = batch.frames[index];
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = 1u;
        view.channels = 2u;
        view.sample_rate = 48'000u;
        view.aperture_samples = 4'800u;
        view.lufs_m = -14.0 + 2.5 * std::sin ((double) index * 0.24);
        view.true_peak = -3.0 + 2.0 * std::sin ((double) index * 0.24 + 0.6);
        view.sharpness = 1.5 + 0.3 * std::sin ((double) index * 0.31);
        view.presentation_end_samples = 288'000 + (int64_t) index * 4'800;
        view.generation = 11;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

inline KirinPerceptualBatch sharpBatch()
{
    KirinPerceptualBatch batch {};
    batch.count = 60u;
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        auto& view = batch.frames[index];
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = 1u;
        view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
        view.channels = 2u;
        view.sample_rate = 48'000u;
        view.aperture_samples = 4'800u;
        view.pre_sharpness = 1.2 + 0.2 * std::sin ((double) index * 0.3);
        view.post_sharpness = view.pre_sharpness + 0.6 + 0.4 * std::sin ((double) index * 0.17);
        view.delta_sharpness = view.post_sharpness - view.pre_sharpness;
        view.presentation_end_samples = 288'000 + (int64_t) index * 4'800;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

inline void verifyLiveAndSharp()
{
    constexpr int width = 580;
    constexpr int height = 279;
    const auto context = presentation::forEditor (600, 400);
    {
        AbsoluteComponent live, bare;
        live.setPresentationContext (context);
        bare.setPresentationContext (context);
        live.setBatchAt (liveBatch(), 60'000.0);
        live.setSignalActive (true);
        const auto playing = paintOf (live, width, height);
        live.setSignalActive (false);
        bare.setSignalActive (false);
        requireDimmedHistory ("LIVE", playing, paintOf (live, width, height), paintOf (bare, width, height));
    }
    {
        PerceptualComponent sharp, bare;
        sharp.setPresentationContext (context);
        bare.setPresentationContext (context);
        sharp.setBatch (sharpBatch());
        sharp.presentationTickAt (60'000.0);
        sharp.setSignalActive (true);
        const auto playing = paintOf (sharp, width, height);
        sharp.setSignalActive (false);
        bare.setSignalActive (false);
        requireDimmedHistory ("SHARP", playing, paintOf (sharp, width, height), paintOf (bare, width, height));
    }
}

// FREQ at 300% (the landscape) and 150% (the flat field).
inline void verifyFreq()
{
    for (const auto& [editorWidth, editorHeight] : { std::pair { 900, 600 }, std::pair { 450, 300 } })
    {
        const auto bounds = ui_contract::spectrumPlotBounds (editorWidth, editorHeight);
        const auto context = presentation::forEditor (editorWidth, editorHeight);
        SpectrumComponent freq, bare;
        for (auto* component : { &freq, &bare })
        {
            component->setAbsoluteObservation (true);
            component->setPresentationContext (context);
        }
        freq.setSignalActive (true);
        for (int index = 0; index < freq_showcase::frameCount; ++index)
            freq.setSnapshot (freq_showcase::frame (index));
        const auto playing = paintOf (freq, bounds.width, bounds.height);
        freq.setSignalActive (false);
        bare.setSignalActive (false);
        requireDimmedHistory ("FREQ", playing, paintOf (freq, bounds.width, bounds.height),
                              paintOf (bare, bounds.width, bounds.height));
    }
}

inline KirinMeterSession monoObservation (uint64_t observedFrames, float db)
{
    KirinMeterSession meter {};
    meter.generation = 1;
    meter.measurement_epoch = 1;
    meter.sample_rate = 48'000;
    meter.channels = 2;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.observed_frames = observedFrames;
    meter.mono_sum_band_count = KIRIN_MONO_SUM_BAND_COUNT;
    meter.mono_sum_approximate_below_hz = 30.0f;
    for (auto& value : meter.mono_sum_db)
        value = db;
    return meter;
}

inline juce::Image monoPaint (const KirinMeterSession& meter, const mono_sum_history::History& history,
                              bool available)
{
    juce::Image image (juce::Image::ARGB, 520, 220, true);
    juce::Graphics g (image);
    g.fillAll (BG);
    mono_sum_curve::paint (g, image.getBounds(), meter, history, available, false, true,
                           presentation::forEditor (900, 600));
    return image;
}

// SPACE's MONO: the six-second field stays when stopped, and the live curve holds a band's last
// value for one second, no longer.
inline void verifyMono()
{
    mono_sum_history::History history;
    for (uint64_t step = 1; step <= 60; ++step)
        KIRIN_STOPPED_REQUIRE (history.append (monoObservation (step * 4'800, -2.0f - 0.05f * (float) step)));
    const auto newest = monoObservation (60 * 4'800, -5.0f);
    const mono_sum_history::History empty;
    // The six-second field's rows only; its frequency labels below stay at full strength.
    requireDimmedHistory ("MONO", monoPaint (newest, history, true), monoPaint (newest, history, false),
                          monoPaint (newest, empty, false), 128, 200);

    const auto silentNow = [] (uint64_t frames) {
        auto meter = monoObservation (frames, 0.0f);
        for (auto& value : meter.mono_sum_db)
            value = std::numeric_limits<float>::quiet_NaN();
        return meter;
    };
    const auto withHistoryAt = [&silentNow] (double ageSeconds) {
        mono_sum_history::History held;
        const uint64_t now = 480'000;
        KIRIN_STOPPED_REQUIRE (held.append (monoObservation (now - (uint64_t) (ageSeconds * 48'000.0), -3.0f)));
        KIRIN_STOPPED_REQUIRE (held.append (silentNow (now)));
        return monoPaint (silentNow (now), held, true);
    };
    mono_sum_history::History none;
    none.append (silentNow (480'000));
    const auto nothingHeld = monoPaint (silentNow (480'000), none, true);
    const auto recent = withHistoryAt (0.5);
    const auto stale = withHistoryAt (1.5);
    // Only the live curve's rows: the six-second field below keeps every stored observation.
    int recentInk = 0;
    int staleInk = 0;
    for (int y = 20; y < 115; ++y)
        for (int x = 0; x < nothingHeld.getWidth(); ++x)
        {
            const auto base = brightness (nothingHeld.getPixelAt (x, y));
            recentInk += std::abs (brightness (recent.getPixelAt (x, y)) - base) > 12 ? 1 : 0;
            staleInk += std::abs (brightness (stale.getPixelAt (x, y)) - base) > 12 ? 1 : 0;
        }
    KIRIN_STOPPED_REQUIRE (recentInk > 200);
    KIRIN_STOPPED_REQUIRE (staleInk < recentInk / 4);
}
}

inline void verifyStoppedHistoryContract()
{
    stopped_history_contract::verifyLiveAndSharp();
    stopped_history_contract::verifyFreq();
    stopped_history_contract::verifyMono();
}
}
