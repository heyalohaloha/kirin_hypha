#include "../src/HyphaAttackComponent.h"
#include "../src/HyphaAttackPainter.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaTheme.h"
#include "AttackUiComparisonContract.h"
#include "AttackUiOverviewContract.h"
#include "AttackUiSizeContract.h"
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
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

    enum class VisualComponent
    {
        strength,
        brightness,
        transient,
        texture
    };

    int countAreaDifferences (const juce::Image& first,
                              const juce::Image& second,
                              juce::Rectangle<int> requested)
    {
        KIRIN_REQUIRE (first.getBounds() == second.getBounds());
        const auto area = requested.getIntersection (first.getBounds());
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                count += first.getPixelAt (x, y) != second.getPixelAt (x, y);
        return count;
    }

    float meanDifferenceRadius (const juce::Image& baseline,
                                const juce::Image& variant,
                                int centreX,
                                int centreY,
                                juce::Rectangle<int> lane)
    {
        const auto area = juce::Rectangle<int> (centreX - 5, lane.getY(), 11, lane.getHeight())
                              .getIntersection (baseline.getBounds());
        float total = 0.0f;
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (baseline.getPixelAt (x, y) != variant.getPixelAt (x, y))
                {
                    total += std::abs (static_cast<float> (y - centreY));
                    ++count;
                }
        return count > 0 ? total / static_cast<float> (count) : -1.0f;
    }

    int countVisiblePixels (const juce::Image& image)
    {
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                count += image.getPixelAt (x, y).getAlpha() > 0;
        return count;
    }

    int countTranslatedDifferences (const juce::Image& image,
                                    juce::Rectangle<int> first,
                                    juce::Rectangle<int> second)
    {
        KIRIN_REQUIRE (first.getWidth() == second.getWidth());
        KIRIN_REQUIRE (first.getHeight() == second.getHeight());
        int count = 0;
        for (int y = 0; y < first.getHeight(); ++y)
            for (int x = 0; x < first.getWidth(); ++x)
                count += image.getPixelAt (first.getX() + x, first.getY() + y)
                      != image.getPixelAt (second.getX() + x, second.getY() + y);
        return count;
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

    int countDifferentPixels (const juce::Image& first, const juce::Image& second)
    {
        KIRIN_REQUIRE (first.getBounds() == second.getBounds());
        int count = 0;
        for (int y = 0; y < first.getHeight(); ++y)
            for (int x = 0; x < first.getWidth(); ++x)
                count += first.getPixelAt (x, y) != second.getPixelAt (x, y);
        return count;
    }

    juce::Image render (hypha::AttackComponent& component)
    {
        juce::Image image (
            juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image);
        component.paintEntireComponent (graphics, true);
        return image;
    }

    void writePreview (const char* environmentName, const juce::Image& image)
    {
        if (const auto* path = std::getenv (environmentName))
        {
            juce::FileOutputStream output { juce::File { path } };
            juce::PNGImageFormat png;
            KIRIN_REQUIRE (output.openedOk() && png.writeImageToStream (image, output));
        }
    }

    juce::MouseEvent mouseEvent (juce::Component& component, float x, float y)
    {
        const auto eventTime = juce::Time::getCurrentTime();
        return {
            juce::Desktop::getInstance().getMainMouseSource(),
            { x, y }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &component, &component, eventTime, { x, y }, eventTime, 0, false
        };
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
    auto componentStorage = std::make_unique<hypha::AttackComponent>();
    auto& component = *componentStorage;
    const auto selectionColour = juce::Colour (hypha::attack_ui::selectionColour);
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

    auto waveformStorage = std::make_unique<KirinAttackWaveformBatch>();
    auto& waveform = *waveformStorage;
    waveform.capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    waveform.count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (std::uint32_t index = 0; index < waveform.count; ++index)
    {
        waveform.points[index].generation = 7;
        waveform.points[index].sample_rate = 48'000;
        waveform.points[index].channels = 2;
        waveform.points[index].start_sample = static_cast<std::int64_t> (index) * 480;
        waveform.points[index].end_sample = waveform.points[index].start_sample + 480;
        const auto sample = waveform.points[index].start_sample
                          + (waveform.points[index].end_sample
                             - waveform.points[index].start_sample) / 2;
        const auto nearestEvent = sample < 72'000 ? 0
                                : sample < 216'000 ? 144'000 : 288'000;
        const auto relative = sample - nearestEvent;
        auto pulse = 0.0f;
        if (relative >= 0)
        {
            const auto elapsed = static_cast<float> (relative);
            pulse = 0.70f * std::exp (-elapsed / 8'500.0f)
                  + 0.20f * std::exp (-elapsed / 2'600.0f)
                      * std::abs (std::sin (elapsed / 720.0f))
                  + 0.10f * std::exp (-elapsed / 25'000.0f);
        }
        else if (relative >= -1'440)
        {
            pulse = 0.08f * std::exp (static_cast<float> (relative) / 520.0f);
        }
        waveform.points[index].rms_dbfs = -58.0f + pulse * 48.0f;
    }

    auto detailsStorage = std::make_unique<KirinAttackDetailBatch>();
    auto& details = *detailsStorage;
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
    detail.attack_rms_dbfs = -14.0f;
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
    details.count = 2;
    details.details[1] = details.details[0];
    details.details[1].event_sample = 144'000;
    details.details[1].shape_start_sample = 139'200;
    details.details[1].shape_end_sample = 145'440;
    details.count = 3;
    details.details[2] = details.details[0];
    details.details[2].event_sample = 0;
    details.details[2].shape_start_sample = -4'800;
    details.details[2].shape_end_sample = 1'440;

    auto preWaveformStorage = std::make_unique<KirinAttackWaveformBatch> (waveform);
    auto& preWaveform = *preWaveformStorage;
    for (std::uint32_t index = 0; index < preWaveform.count; ++index)
        preWaveform.points[index].rms_dbfs -= 2.0f;
    auto preDetailsStorage = std::make_unique<KirinAttackDetailBatch> (details);
    auto& preDetails = *preDetailsStorage;
    for (std::uint32_t index = 0; index < preDetails.count; ++index)
    {
        preDetails.details[index].contrast_db = 5.0f;
        preDetails.details[index].attack_rms_dbfs = -20.0f;
        preDetails.details[index].crest_db = 8.0f;
        preDetails.details[index].sample_edge_ratio_db = -14.0f;
        preDetails.details[index].peak_plateau_ms = 1.0f;
        preDetails.details[index].sharpness_available = 1;
        preDetails.details[index].sharpness_acum = 1.2f;
    }
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
    pairEvents.events[1].pre_available = 1;
    pairEvents.events[1].post_available = 1;
    pairEvents.events[2].pre_available = 1;
    pairEvents.events[2].post_available = 1;
    pairEvents.events[0].pre_available = 1;
    pairEvents.events[0].post_available = 1;

    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    component.setOverlayMode (false);
    KIRIN_REQUIRE (hypha::attack_ui_test::verifySignedComparisonSpecimen());
    KIRIN_REQUIRE (hypha::attack_ui_test::verifyNoPreOnsetFeatureInk());
    KIRIN_REQUIRE (hypha::attack_ui_test::verifyAsymmetricMeasuredFlow());
    KIRIN_REQUIRE (hypha::attack_ui_test::verifySignedOverviewGlyph());
    KIRIN_REQUIRE (hypha::attack_ui_test::verifyContinuousTrace (waveform, details));
    const auto image = render (component);
    writePreview ("KIRIN_ATTACK_UI_PREVIEW_PATH", image);
    KIRIN_REQUIRE (countRuns (image, { 0, 20, image.getWidth(), 150 }, selectionColour) > 0);
    KIRIN_REQUIRE (countRuns (image, { 0, 200, image.getWidth(), image.getHeight() },
                              juce::Colour (hypha::attack_ui::textureColour)) > 0);
    const auto timelineHeight = hypha::attack_ui::timelineHeight (component.getHeight());
    auto identityComponentStorage = std::make_unique<hypha::AttackComponent>();
    auto& identityComponent = *identityComponentStorage;
    identityComponent.setSize (bounds.width, bounds.height);
    identityComponent.setOverlayMode (false);
    identityComponent.setSnapshot (events, waveform, details, waveform, details, pairEvents,
                                   288'000, 48'000, 7, stats);
    const auto identity = render (identityComponent);
    const auto paintedTimelineY = hypha::attack_ui::headerHeight + 1;
    const auto paintedLaneHeight = (timelineHeight - 2) / 2;
    const auto comparisonHeight = paintedLaneHeight - 25;
    const auto identityDifferences = countTranslatedDifferences (
        identity,
        { 42, paintedTimelineY + 20,
          image.getWidth() - 48, comparisonHeight },
        { 42, paintedTimelineY + paintedLaneHeight + 20,
          image.getWidth() - 48, comparisonHeight });
    KIRIN_REQUIRE (identityDifferences <= 100); // Mirrored curve antialiasing differs at subpixels.

    const auto middleEventX = hypha::attack_ui::eventX (
        events.events[1].event_sample, 288'000, 48'000, image.getWidth());
    const auto firstEventX = hypha::attack_ui::eventX (
        events.events[0].event_sample, 288'000, 48'000, image.getWidth());
    const auto lastEventX = hypha::attack_ui::eventX (
        events.events[2].event_sample, 288'000, 48'000, image.getWidth());
    identityComponent.setOverlayMode (true);
    const auto identityOverlay = render (identityComponent);
    juce::Image identityDifferenceLayer (
        juce::Image::ARGB, image.getWidth(), timelineHeight, true);
    juce::Graphics identityDifferenceGraphics (identityDifferenceLayer);
    hypha::attack_painter::drawWaveformDifferences (
        identityDifferenceGraphics, details, details, pairEvents,
        identityDifferenceLayer.getBounds(), 0, 288'000, 48'000);
    KIRIN_REQUIRE (countVisiblePixels (identityDifferenceLayer) > 0);
    auto thresholdDetailsStorage = std::make_unique<KirinAttackDetailBatch> (details);
    auto& thresholdDetails = *thresholdDetailsStorage;
    for (std::uint32_t index = 0; index < thresholdDetails.count; ++index)
    {
        thresholdDetails.details[index].attack_rms_dbfs = hypha::attack_ui::strengthGlowOnDbfs;
        thresholdDetails.details[index].sharpness_acum = hypha::attack_ui::brightnessGlowOnAcum;
        thresholdDetails.details[index].contrast_db = hypha::attack_ui::transientGlowOnDb;
        thresholdDetails.details[index].sample_edge_ratio_db = -24.0f;
        thresholdDetails.details[index].crest_db = 12.0f;
        thresholdDetails.details[index].peak_plateau_ms = 0.0f;
    }
    constexpr int focusWidth = 240;
    constexpr int focusHeight = 90;
    const auto renderFocus = [] (const KirinAttackDetail& focusDetail)
    {
        juce::Image focus (juce::Image::ARGB, focusWidth, focusHeight, true);
        juce::Graphics graphics (focus);
        hypha::attack_painter::drawEventFocus (
            graphics, nullptr, &focusDetail, focus.getBounds(), 0.0f);
        return focus;
    };
    const auto thresholdImage = renderFocus (thresholdDetails.details[1]);
    auto strengthRadius = -1.0f;
    auto textureRadius = -1.0f;
    auto brightnessRadius = -1.0f;
    auto transientRadius = -1.0f;
    for (const auto visual : { VisualComponent::strength, VisualComponent::brightness,
                               VisualComponent::transient, VisualComponent::texture })
    {
        auto feature = thresholdDetails.details[1];
        switch (visual)
        {
            case VisualComponent::strength:
                feature.attack_rms_dbfs = hypha::attack_ui::strengthGlowFullDbfs;
                break;
            case VisualComponent::brightness:
                feature.sharpness_acum = hypha::attack_ui::brightnessGlowFullAcum;
                break;
            case VisualComponent::transient:
                feature.contrast_db = hypha::attack_ui::transientGlowFullDb;
                break;
            case VisualComponent::texture:
                feature.sample_edge_ratio_db = 0.0f;
                feature.crest_db = 0.0f;
                feature.peak_plateau_ms = hypha::attack_ui::textureGlowFull * 4.0f;
                break;
        }
        const auto featureImage = renderFocus (feature);
        KIRIN_REQUIRE (countAreaDifferences (
            thresholdImage, featureImage, featureImage.getBounds()) > 0);
        const auto specimenCoreX = focusWidth * 43 / 100;
        const auto radius = meanDifferenceRadius (
            thresholdImage, featureImage, specimenCoreX, focusHeight / 2,
            featureImage.getBounds());
        if (visual == VisualComponent::strength)
            strengthRadius = radius;
        else if (visual == VisualComponent::texture)
            textureRadius = radius;
        else if (visual == VisualComponent::brightness)
            brightnessRadius = radius;
        else
            transientRadius = radius;
    }
    KIRIN_REQUIRE (strengthRadius > 0.0f && strengthRadius < textureRadius);
    KIRIN_REQUIRE (textureRadius < brightnessRadius);
    KIRIN_REQUIRE (brightnessRadius < transientRadius);
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), middleEventX, selectionColour));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), firstEventX, selectionColour));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), middleEventX, selectionColour));

    auto laterPairEvents = pairEvents;
    laterPairEvents.count = 4;
    laterPairEvents.events[3].event_sample = 320'000;
    laterPairEvents.events[3].pre_event_sample = 320'000;
    laterPairEvents.events[3].post_event_sample = 320'000;
    const auto transitionStart = juce::Time::getMillisecondCounterHiRes();
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, laterPairEvents,
                           336'000, 48'000, 7, stats);
    KIRIN_REQUIRE (hasColourNear (render (component), middleEventX, selectionColour));
    component.presentationTickAt (transitionStart + 50.0);
    const auto halfTransitionX = hypha::attack_ui::eventX (
        events.events[1].event_sample, 312'000, 48'000, image.getWidth());
    KIRIN_REQUIRE (hasColourNear (render (component), halfTransitionX, selectionColour));
    component.presentationTickAt (transitionStart + 200.0);
    const auto lockedMiddleX = hypha::attack_ui::eventX (
        events.events[1].event_sample, 336'000, 48'000, image.getWidth());
    const auto newLastX = hypha::attack_ui::eventX (
        laterPairEvents.events[3].event_sample, 336'000, 48'000, image.getWidth());
    KIRIN_REQUIRE (hasColourNear (render (component), lockedMiddleX, selectionColour));
    KIRIN_REQUIRE (! hasColourNear (render (component), newLastX, selectionColour));

    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    KIRIN_REQUIRE (hasColourNear (render (component), newLastX, selectionColour));

    auto newestPairEvents = laterPairEvents;
    newestPairEvents.count = 5;
    newestPairEvents.events[4].event_sample = 370'000;
    newestPairEvents.events[4].pre_event_sample = 370'000;
    newestPairEvents.events[4].post_event_sample = 370'000;
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, newestPairEvents,
                           384'000, 48'000, 7, stats);
    component.presentationTick (false); // Inactive transport snaps and then remains still.
    const auto newestX = hypha::attack_ui::eventX (
        newestPairEvents.events[4].event_sample, 384'000, 48'000, image.getWidth());
    KIRIN_REQUIRE (hasColourNear (render (component), newestX, selectionColour));
    KIRIN_REQUIRE (! component.keyPressed (juce::KeyPress ('x')));

    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto scrubY = static_cast<float> (hypha::attack_ui::headerHeight + 20);
    component.mouseDown (mouseEvent (component, static_cast<float> (firstEventX), scrubY));
    KIRIN_REQUIRE (hasColourNear (render (component), firstEventX, selectionColour));
    component.mouseDrag (mouseEvent (component, static_cast<float> (middleEventX), scrubY));
    KIRIN_REQUIRE (hasColourNear (render (component), middleEventX, selectionColour));
    writePreview ("KIRIN_ATTACK_UI_LOCK_PREVIEW_PATH", render (component));
    component.mouseDown (mouseEvent (
        component, static_cast<float> (component.getWidth() - 4),
        static_cast<float> (hypha::attack_ui::headerHeight
            + hypha::attack_ui::timelineHeight (component.getHeight())
            + hypha::attack_ui::axisLabelHeight / 2)));
    KIRIN_REQUIRE (hasColourNear (render (component), lastEventX, selectionColour));
    const auto twoRows = render (component);
    component.setOverlayMode (true);
    const auto overlay = render (component);
    KIRIN_REQUIRE (countDifferentPixels (twoRows, overlay) > 100);
    const auto overlayTimeline = juce::Rectangle<int> (
        0, hypha::attack_ui::headerHeight, image.getWidth(), timelineHeight);
    KIRIN_REQUIRE (countAreaDifferences (identityOverlay, overlay, overlayTimeline) > 0);
    writePreview ("KIRIN_ATTACK_UI_OVERLAY_PREVIEW_PATH", overlay);
    KIRIN_REQUIRE (hypha::attack_ui_test::verifySupportedSizes (component));

    stats.worker_running = 0;
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto warming = render (component);
    KIRIN_REQUIRE (countRuns (warming, { 0, 20, warming.getWidth(), 150 },
                              selectionColour) == 0
                   && hypha::attack_ui_test::verifyDormantSpecimenBlack (warming));
    std::cout << "ATTACK UI contract passed: split PRE/POST, scrub, factual deltas\n";
    return EXIT_SUCCESS;
}
