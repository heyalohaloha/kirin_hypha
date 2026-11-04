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

inline void verifySpectrumLowScopeContract()
{
    verifyPeakTagRows();
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
