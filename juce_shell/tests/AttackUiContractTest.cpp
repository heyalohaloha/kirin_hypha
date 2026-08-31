#include "../src/HyphaAttackInternalComponent.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaTheme.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "ATTACK UI contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

#define KIRIN_REQUIRE(expression) require ((expression), #expression, __LINE__)

    bool nearRgb (juce::Colour pixel, juce::Colour target)
    {
        constexpr int tolerance = 12;
        return pixel.getAlpha() > 16
            && std::abs ((int) pixel.getRed() - (int) target.getRed()) <= tolerance
            && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
            && std::abs ((int) pixel.getBlue() - (int) target.getBlue()) <= tolerance;
    }

    int countRuns (const juce::Image& image, juce::Rectangle<int> requested,
                   juce::Colour target)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int runs = 0;
        bool previousColumn = false;
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            bool currentColumn = false;
            for (int y = area.getY(); y < area.getBottom(); ++y)
                currentColumn = currentColumn
                    || nearRgb (image.getPixelAt (x, y), target);
            if (currentColumn && ! previousColumn)
                ++runs;
            previousColumn = currentColumn;
        }
        return runs;
    }

    bool hasColourNear (const juce::Image& image, int centreX, juce::Colour target)
    {
        const auto area = juce::Rectangle<int> (
            centreX - 2, hypha::attack_ui::headerHeight, 5,
            image.getHeight() - hypha::attack_ui::headerHeight
                - hypha::attack_ui::axisLabelHeight).getIntersection (image.getBounds());
        for (int x = area.getX(); x < area.getRight(); ++x)
            for (int y = area.getY(); y < area.getBottom(); ++y)
                if (nearRgb (image.getPixelAt (x, y), target))
                    return true;
        return false;
    }

    juce::Image render (hypha::AttackInternalComponent& component)
    {
        juce::Image image (
            juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }
}

int main()
{
    static_assert (sizeof (KirinAttackWaveformPoint) == 40);
    static_assert (sizeof (KirinAttackWaveformBatch) == 24'008);
    static_assert (sizeof (KirinAttackDetail) == 512);
    static_assert (offsetof (KirinAttackDetail, shape) == 128);
    static_assert (sizeof (KirinAttackDetailBatch) == 122'888);
    static_assert (sizeof (KirinAttackPairEvent) == 112);
    static_assert (sizeof (KirinAttackPairEventBatch) == 26'896);
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    hypha::AttackInternalComponent component;
    const auto bounds = hypha::ui_contract::spectrumPlotBounds (600, 400);
    component.setSize (bounds.width, bounds.height);

    KirinAttackStats stats {};
    stats.available = 1;
    stats.enabled = 1;
    stats.worker_running = 1;
    KirinAttackEventBatch events {};
    events.capacity = KIRIN_ATTACK_EVENT_BATCH_CAPACITY;
    events.count = 4;
    for (std::uint32_t index = 0; index < events.count; ++index)
    {
        events.events[index].generation = 7;
        events.events[index].sample_rate = 48'000;
    }
    events.events[0].event_sample = 0;
    events.events[1].event_sample = 144'000;
    events.events[2].event_sample = 288'000;
    events.events[3].event_sample = 200'000;
    events.events[3].generation = 6; // stale transport generation must not be painted

    KirinAttackWaveformBatch waveform {};
    waveform.capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    waveform.count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (std::uint32_t index = 0; index < waveform.count; ++index)
    {
        waveform.points[index].generation = 7;
        waveform.points[index].sample_rate = 48'000;
        waveform.points[index].channels = 2;
        waveform.points[index].start_sample = static_cast<std::int64_t> (index) * 480;
        waveform.points[index].end_sample = waveform.points[index].start_sample + 480;
        const auto pulsePosition = static_cast<int> (index % 120u);
        const auto pulse = juce::jmax (
            0.0f, 1.0f - std::abs (static_cast<float> (pulsePosition - 8)) / 24.0f);
        waveform.points[index].rms_dbfs = -58.0f + pulse * 48.0f;
    }

    KirinAttackDetailBatch details {};
    details.capacity = KIRIN_ATTACK_DETAIL_BATCH_CAPACITY;
    details.count = 1;
    auto& detail = details.details[0];
    detail.generation = 7;
    detail.sample_rate = 48'000;
    detail.channels = 2;
    detail.event_sample = 288'000;
    detail.shape_start_sample = detail.event_sample - 4'800;
    detail.shape_end_sample = detail.event_sample + 1'440;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.contrast_db = 8.0f;
    detail.sample_peak_dbfs = -3.0f;
    detail.crest_db = 6.0f;
    detail.sample_edge_ratio_db = -12.0f;
    detail.peak_plateau_ms = 1.5f;
    detail.sharpness_available = 1;
    detail.sharpness_acum = 1.6f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
    {
        const auto distance = std::abs (static_cast<int> (index) - 74);
        detail.shape[index] = index < 74 ? 0.025f
            : 0.82f * std::exp (-static_cast<float> (distance) / 8.0f) + 0.018f;
    }

    auto preWaveform = waveform;
    for (std::uint32_t index = 0; index < preWaveform.count; ++index)
        preWaveform.points[index].rms_dbfs -= 2.0f;
    KirinAttackDetailBatch preDetails = details;
    preDetails.details[0].contrast_db = 5.0f;
    preDetails.details[0].crest_db = 8.0f;
    preDetails.details[0].sample_edge_ratio_db = -14.0f;
    preDetails.details[0].peak_plateau_ms = 0.5f;
    preDetails.details[0].sharpness_available = 1;
    preDetails.details[0].sharpness_acum = 1.2f;
    KirinAttackPairEventBatch pairEvents {};
    pairEvents.status = KIRIN_SPECTRUM_ACTIVE;
    pairEvents.capacity = KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY;
    pairEvents.count = 3;
    for (std::uint32_t index = 0; index < pairEvents.count; ++index)
    {
        pairEvents.events[index].event_sample = events.events[index].event_sample;
        pairEvents.events[index].pre_event_sample = events.events[index].event_sample;
        pairEvents.events[index].post_event_sample = events.events[index].event_sample;
    }
    pairEvents.events[2].pre_available = 1;
    pairEvents.events[2].post_available = 1;

    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto image = render (component);
    if (const auto* previewPath = std::getenv ("KIRIN_ATTACK_UI_PREVIEW_PATH"))
    {
        juce::FileOutputStream output { juce::File { previewPath } };
        juce::PNGImageFormat png;
        KIRIN_REQUIRE (output.openedOk());
        KIRIN_REQUIRE (png.writeImageToStream (image, output));
    }
    KIRIN_REQUIRE (countRuns (image, { 0, 20, image.getWidth(), 150 }, hypha::COL_FLORA_BR) == 1);
    KIRIN_REQUIRE (countRuns (image, { 0, 110, image.getWidth(), 190 },
                              hypha::dim (hypha::COL_NORMAL, 0.72f)) > 0);
    KIRIN_REQUIRE (countRuns (image, { 0, 200, image.getWidth(), image.getHeight() },
                              juce::Colour (0xffff8c74)) > 0);

    const auto middleEventX = hypha::attack_ui::eventX (
        events.events[1].event_sample, 288'000, 48'000, image.getWidth());
    const auto firstEventX = hypha::attack_ui::eventX (
        events.events[0].event_sample, 288'000, 48'000, image.getWidth());
    const auto lastEventX = hypha::attack_ui::eventX (
        events.events[2].event_sample, 288'000, 48'000, image.getWidth());
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), middleEventX, hypha::COL_FLORA_BR));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), firstEventX, hypha::COL_FLORA_BR));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), lastEventX, hypha::COL_FLORA_BR));
    KIRIN_REQUIRE (! component.keyPressed (juce::KeyPress ('x')));

    component.setOverlayMode (true);
    const auto overlay = render (component);
    KIRIN_REQUIRE (countRuns (overlay, { 0, 110, overlay.getWidth(), 215 },
                              juce::Colour (0xffe8b76a)) > 2);
    KIRIN_REQUIRE (countRuns (overlay, { 0, 110, overlay.getWidth(), 215 },
                              juce::Colour (0xff71d8ff)) > 2);
    KIRIN_REQUIRE (countRuns (overlay, { 0, 110, overlay.getWidth(), 215 },
                              juce::Colour (0xffff8c74)) > 2);
    if (const auto* overlayPath = std::getenv ("KIRIN_ATTACK_UI_OVERLAY_PREVIEW_PATH"))
    {
        juce::FileOutputStream output { juce::File { overlayPath } };
        juce::PNGImageFormat png;
        KIRIN_REQUIRE (output.openedOk());
        KIRIN_REQUIRE (png.writeImageToStream (overlay, output));
    }

    stats.worker_running = 0;
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto warming = render (component);
    KIRIN_REQUIRE (countRuns (warming, { 0, 20, warming.getWidth(), 150 },
                              hypha::COL_FLORA_BR) == 0);
    std::cout << "ATTACK UI contract passed: split PRE/POST, scrub, factual deltas\n";
    return EXIT_SUCCESS;
}
