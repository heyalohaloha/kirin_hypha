#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaObservatoryResizeContract.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumPeakTags.h"
#include "../src/HyphaSpectrumUiContract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void checkLowScope (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Spectrum LOW contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_SPECTRUM_LOW_REQUIRE(expression) \
    checkLowScope ((expression), #expression, __LINE__)

// A bass with a kick under it and an upper note, over a slope, as LOW would measure one.
inline KirinSpectrumView lowFrame (std::int64_t endpoint, bool paired)
{
    KirinSpectrumView frame {};
    frame.status = paired ? KIRIN_SPECTRUM_ACTIVE : KIRIN_SPECTRUM_NO_PAIR;
    frame.has_data = paired ? 1u : 0u;
    frame.post_has_data = 1u;
    frame.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    frame.channels = 2u;
    frame.sample_rate = 48'000u;
    frame.min_hz = 20.0f;
    frame.max_hz = 300.0f;
    frame.presentation_end_samples = endpoint;
    frame.aperture_samples = 16'384u;
    frame.fft_size = 32'768u;
    frame.approximate_below_hz = 8.7890625f;
    const auto bump = [] (float hz, float centre, float height)
    {
        const auto distance = std::log2 (hz / centre) * 9.0f;
        return height * std::exp (-distance * distance);
    };
    for (std::size_t index = 0u; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float position = ((float) index + 0.5f) / (float) KIRIN_SPECTRUM_BAND_COUNT;
        const float hz = 20.0f * std::pow (15.0f, position);
        const float post = -46.0f - 14.0f * position + bump (hz, 41.2f, 28.0f)
                         + bump (hz, 55.0f, 34.0f) + bump (hz, 98.0f, 16.0f);
        frame.post_dbfs[index] = post;
        frame.pre_dbfs[index] = post - bump (hz, 55.0f, 3.0f);
        frame.display_db[index] = frame.post_dbfs[index] - frame.pre_dbfs[index];
        frame.shape_db[index] = frame.display_db[index];
        frame.shape_valid[index] = 1u;
    }
    return frame;
}

// Three peaks side by side, at the same height or not, wherever the plot leaves room: no plate
// covers another.
inline void verifyPeakTagRows()
{
    const juce::Rectangle<float> plot (0.0f, 0.0f, 200.0f, 100.0f);
    const juce::Rectangle<float> tag (40.0f, 0.0f, 60.0f, 8.5f);
    using Peaks = std::array<float, 3>;
    for (const auto& peaks : { Peaks { 2.0f, 2.0f, 2.0f }, Peaks { 30.0f, 30.0f, 30.0f },
                               Peaks { 98.0f, 98.0f, 98.0f }, Peaks { 75.0f, 84.0f, 90.0f },
                               Peaks { 90.0f, 84.0f, 75.0f }, Peaks { 4.0f, 9.0f, 16.0f } })
    {
        std::array<juce::Rectangle<float>, 3> placed {};
        for (std::size_t index = 0; index < placed.size(); ++index)
        {
            placed[index] = spectrum_peaks::freeRow (plot, tag, peaks[index], 2.0f,
                                                     placed.data(), index);
            KIRIN_SPECTRUM_LOW_REQUIRE (plot.contains (placed[index]));
            for (std::size_t other = 0; other < index; ++other)
                KIRIN_SPECTRUM_LOW_REQUIRE (! placed[other].intersects (placed[index]));
        }
    }
    // A plot too short for any free row still keeps the plate inside it.
    const juce::Rectangle<float> slim (0.0f, 0.0f, 200.0f, 12.0f);
    const std::array<juce::Rectangle<float>, 1> taken { { tag.withY (1.0f) } };
    KIRIN_SPECTRUM_LOW_REQUIRE (
        slim.contains (spectrum_peaks::freeRow (slim, tag, 5.0f, 2.0f, taken.data(), 1)));
}

// Compare both routes on the same changing history. Source acquisition remains 30 Hz even
// when the presentation rate doubles; cache misses and acquisition work are inside the timing.
inline void verifyLowPlaybackLoad()
{
    material_cache::Lifetime material;
    for (const int width : { 600, 900 })
        for (const int refreshHz : { 60, 120 })
        {
            std::array<std::array<double, 3>, 2> paintTimes {}, workTimes {};
            for (int round = 0; round < 3; ++round)
              for (int pass = 0; pass < 2; ++pass)
            {
                const bool cached = (round + pass) % 2 != 0;
                SpectrumComponent view; view.setAbsoluteObservation (true); view.setLowScope (true);
                view.setSignalActive (true); view.setHistoryCacheEnabledForTest (cached);
                const auto body = ui_contract::spectrumPlotBounds (width, width * 2 / 3);
                view.setSize (body.width, body.height);
                for (int tick = 0; tick < 180; ++tick)
                {
                    auto frame = lowFrame (48000 + tick * 1600, false);
                    frame.channel_mode = KIRIN_SPECTRUM_CHANNEL_MID; view.setSnapshot (frame);
                }
                juce::Image image (juce::Image::ARGB, body.width, body.height, true);
                juce::Graphics graphics (image); view.paintEntireComponent (graphics, true);
                const auto builds = view.backdropBuildsForTest();
                const auto clock = view.curveUpdateTimeForTest();
                double paintMs = 0, maximumMs = 0;
                int acquisitions = 0, labelChanges = 0;
                auto labelTime = view.peakLabelUpdateTimeForTest();
                const auto started = juce::Time::getMillisecondCounterHiRes();
                for (int draw = 0; draw < refreshHz * 2; ++draw)
                {
                    if (draw % (refreshHz / 30) == 0)
                    {
                        auto frame = lowFrame (336000 + acquisitions++ * 1600, false);
                        frame.channel_mode = KIRIN_SPECTRUM_CHANNEL_MID;
                        for (auto& value : frame.post_dbfs) value += 3 * std::sin (acquisitions * 0.1f);
                        KirinSpectrumBatch batch {}; batch.count = 1; batch.frames[0] = batch.latest = frame;
                        view.setBatch (batch);
                    }
                    view.presentationTickAt (clock + (draw + 1) * 1000.0 / refreshHz);
                    const auto reading = view.peakLabelUpdateTimeForTest();
                    if (std::abs (reading - labelTime) > 0) { ++labelChanges; labelTime = reading; }
                    image.clear (image.getBounds(), BG);
                    const auto before = juce::Time::getMillisecondCounterHiRes();
                    view.paintEntireComponent (graphics, true);
                    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - before;
                    paintMs += elapsed; maximumMs = std::max (maximumMs, elapsed);
                }
                const auto totalMs = juce::Time::getMillisecondCounterHiRes() - started;
                const auto generated = view.backdropBuildsForTest() - builds;
                paintTimes[cached][round] = paintMs / (refreshHz * 2); workTimes[cached][round] = totalMs / 2;
                std::cout << "LOW playback " << width << " " << refreshHz << " Hz " << (cached ? "cached" : "regenerated")
                          << " round=" << round << ": acquisitions=" << acquisitions << " backgrounds=" << generated << " labels=" << labelChanges
                          << " paint=" << paintTimes[cached][round] << " ms/frame max=" << maximumMs
                          << " work=" << workTimes[cached][round] << " ms/s\n";
                KIRIN_SPECTRUM_LOW_REQUIRE (acquisitions == 60 && labelChanges == 2);
                KIRIN_SPECTRUM_LOW_REQUIRE (generated == static_cast<unsigned> (cached ? 60 : refreshHz * 2));
                if (cached)
                {
                    KIRIN_SPECTRUM_LOW_REQUIRE (paintTimes[1][round] < std::min (12.0, 1000.0 / refreshHz) && workTimes[1][round] < 800.0);
                    // Bypassing the cache changes the work, never the picture at one observation.
                    const auto retained = image.createCopy();
                    view.setHistoryCacheEnabledForTest (false);
                    image.clear (image.getBounds(), BG); view.paintEntireComponent (graphics, true);
                    uint64_t colourError = 0;
                    for (int y = 0; y < body.height; ++y)
                        for (int x = 0; x < body.width; ++x)
                        {
                            const auto a = retained.getPixelAt (x, y), b = image.getPixelAt (x, y);
                            colourError += static_cast<uint64_t> (std::abs (a.getRed() - b.getRed())
                                + std::abs (a.getGreen() - b.getGreen()) + std::abs (a.getBlue() - b.getBlue()));
                        }
                    // The extra premultiplied raster may round edge pixels differently.
                    KIRIN_SPECTRUM_LOW_REQUIRE (colourError < static_cast<uint64_t> (body.width * body.height * 3));
                }
            }
            for (auto* times : { &paintTimes, &workTimes })
            {
                for (auto& samples : *times) std::sort (samples.begin(), samples.end());
                KIRIN_SPECTRUM_LOW_REQUIRE ((*times)[1][1] < (*times)[0][1] * 1.1);
            }
            std::cout << "LOW playback median " << width << " " << refreshHz << " Hz: paint "
                      << paintTimes[0][1] << " -> " << paintTimes[1][1] << " ms/frame; work "
                      << workTimes[0][1] << " -> " << workTimes[1][1] << " ms/s\n";
        }
}

inline void verifySpectrumLowScopeContract()
{
    verifyPeakTagRows();
    verifyLowPlaybackLoad();
    for (const auto& preset : observatory::sizePresets)
    {
        const auto body = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const juce::Rectangle<float> bounds (0, 0, (float) body.width, (float) body.height);
        const auto outer = spectrum_geometry::plotBoundsFor (bounds);
        const auto scale = spectrum_geometry::visualScaleFor (bounds);
        const auto low = spectrum_geometry::lowBoundsFor (outer, scale);
        if (spectrum_geometry::viewOnly (scale))
        {
            KIRIN_SPECTRUM_LOW_REQUIRE (low.isEmpty());
            continue;
        }
        const auto modes = spectrum_geometry::displayModeBoundsFor (3u, outer, scale);
        KIRIN_SPECTRUM_LOW_REQUIRE (low.getX() >= modes.getRight());
        KIRIN_SPECTRUM_LOW_REQUIRE (
            low.getRight() <= spectrum_geometry::subviewBoundsFor (outer, scale).getX());
    }

    material_cache::Lifetime editorMaterial;
    const auto* previews = std::getenv ("KIRIN_SPECTRUM_LOW_PREVIEW_DIR");
    if (previews != nullptr) KIRIN_SPECTRUM_LOW_REQUIRE (juce::File (previews).createDirectory().wasOk());
    for (const auto& preset : observatory::sizePresets)
        for (const bool paired : { false, true })
        {
            const auto body = ui_contract::spectrumPlotBounds (preset.width, preset.height);
            SpectrumComponent component;
            component.setPresentationContext (presentation::forEditor (preset.width, preset.height));
            component.setSignalActive (true);
            component.setSize (body.width, body.height);
            component.setAbsoluteObservation (! paired);
            component.setLowScope (true);
            KIRIN_SPECTRUM_LOW_REQUIRE (component.isLowScope());
            const auto frame = lowFrame (64'000, paired);
            KirinSpectrumBatch batch {};
            batch.count = 1u;
            batch.frames[0] = frame;
            batch.latest = frame;
            component.setBatch (batch);
            component.presentationTickAt (1'000.0);
            // The LOW layout passes the same validation as FULL: the frame is presented.
            KIRIN_SPECTRUM_LOW_REQUIRE (component.presentedEndpointForTest() == 64'000);
            // A level that falls is shown as it falls: LOW's curve stays with its numbers.
            auto quieter = lowFrame (72'000, paired);
            for (std::size_t band = 0u; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
            {
                quieter.post_dbfs[band] -= 30.0f;
                quieter.pre_dbfs[band] -= 30.0f;
            }
            KirinSpectrumBatch next {};
            next.count = 1u;
            next.frames[0] = quieter;
            next.latest = quieter;
            component.setBatch (next);
            component.presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
            KIRIN_SPECTRUM_LOW_REQUIRE (
                std::abs (component.displayedPostForTest (128u) - quieter.post_dbfs[128u]) < 3.0f);
            component.setBatch (batch);
            juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int draw = 0; draw < 100; ++draw)
            {
                image.clear (image.getBounds(), BG);
                juce::Graphics measured (image);
                component.paintEntireComponent (measured, true);
            }
            std::cout << "LOW paint " << preset.width << (paired ? " delta: " : " absolute: ")
                      << (juce::Time::getMillisecondCounterHiRes() - start) / 100.0 << " ms/frame\n";
            if (! paired && preset.width >= 600)
            {
                for (int tick = 0; tick < 180; ++tick)
                {
                    auto historyFrame = lowFrame (80'000 + tick * 1600, false);
                    historyFrame.channel_mode = KIRIN_SPECTRUM_CHANNEL_MID;
                    component.setSnapshot (historyFrame);
                }
                const auto buildsBefore = component.backdropBuildsForTest();
                const auto historyStart = juce::Time::getMillisecondCounterHiRes();
                for (int draw = 0; draw < 100; ++draw)
                {
                    image.clear (image.getBounds(), BG);
                    juce::Graphics measured (image);
                    component.paintEntireComponent (measured, true);
                }
                KIRIN_SPECTRUM_LOW_REQUIRE (component.backdropBuildsForTest() == buildsBefore + 1);
                std::cout << "LOW history paint " << preset.width << ": "
                          << (juce::Time::getMillisecondCounterHiRes() - historyStart) / 100.0 << " ms/frame\n";
                component.setSnapshot (frame);
                image.clear (image.getBounds(), BG);
                component.paintEntireComponent (graphics, true);
            }
            if (previews == nullptr)
                continue;
            const auto file = juce::File (previews).getChildFile (
                juce::String (preset.width) + (paired ? "-delta.png" : "-post.png"));
            file.deleteFile();
            juce::FileOutputStream stream (file);
            KIRIN_SPECTRUM_LOW_REQUIRE (stream.openedOk()
                && juce::PNGImageFormat().writeImageToStream (image, stream));
        }
}
}
