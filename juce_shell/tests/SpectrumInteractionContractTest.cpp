#include "SpectrumInteractionContractTest.h"

#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaUiContract.h"

#include <cstdlib>
#include <cmath>
#include <iostream>

namespace hypha::tests
{
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Spectrum interaction contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

   #define KIRIN_INTERACTION_REQUIRE(expression) require ((expression), #expression, __LINE__)

    int countDifferentPixels (const juce::Image& a, const juce::Image& b)
    {
        KIRIN_INTERACTION_REQUIRE (a.getBounds() == b.getBounds());
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y).getARGB() != b.getPixelAt (x, y).getARGB())
                    ++count;
        return count;
    }

    int countWarmChangedPixels (const juce::Image& withMark,
                                const juce::Image& withoutMark,
                                juce::Rectangle<int> requested)
    {
        KIRIN_INTERACTION_REQUIRE (withMark.getBounds() == withoutMark.getBounds());
        const auto area = requested.getIntersection (withMark.getBounds());
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
        {
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto marked = withMark.getPixelAt (x, y);
                if (marked.getARGB() != withoutMark.getPixelAt (x, y).getARGB()
                    && (int) marked.getRed() > (int) marked.getBlue() + 18
                    && (int) marked.getGreen() > (int) marked.getBlue() + 8)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    int countWarmPixels (const juce::Image& image, juce::Rectangle<int> requested)
    {
        const auto area = requested.getIntersection (image.getBounds());
        int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);
                if ((int) pixel.getRed() > (int) pixel.getBlue() + 18
                    && (int) pixel.getGreen() > (int) pixel.getBlue() + 8)
                {
                    ++count;
                }
            }
        return count;
    }

    void writeReviewImage (const juce::Image& image, const char* environmentVariable)
    {
        const auto path = juce::SystemStats::getEnvironmentVariable (
            environmentVariable, {});
        if (path.isEmpty())
            return;
        auto output = juce::File (path).createOutputStream();
        KIRIN_INTERACTION_REQUIRE (output != nullptr);
        KIRIN_INTERACTION_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (image, *output));
    }

    juce::MouseEvent mouseEvent (juce::Component& component,
                                  float x,
                                  float y,
                                  juce::Time eventTime)
    {
        return {
            juce::Desktop::getInstance().getMainMouseSource(),
            { x, y }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &component, &component, eventTime,
            { x, y }, eventTime, 0, false
        };
    }
}

void verifySpectrumInteractionContract (SpectrumComponent& spectrum,
                                        const KirinSpectrumView& snapshot,
                                        int width,
                                        int height,
                                        juce::Time eventTime)
{
    namespace ui = ui_contract;
    const auto componentBounds = juce::Rectangle<float> (
        0.0f, 0.0f, (float) width, (float) height);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (componentBounds);
    const float scale = spectrum_geometry::visualScaleFor (componentBounds);
    const auto markBounds = spectrum_geometry::markBoundsFor (outerPlot, scale);
    const float controlY = markBounds.getCentreY();
    const float markX = markBounds.getCentreX();
    spectrum.mouseDown (mouseEvent (spectrum, markX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (spectrum.hasMark());

    KirinSpectrumView movedSnapshot = snapshot;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        movedSnapshot.display_db[index] *= -0.55f;
    spectrum.setSnapshot (movedSnapshot);
    KIRIN_INTERACTION_REQUIRE (spectrum.hasMark());
    juce::Image withMark (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (withMark);
        spectrum.paintEntireComponent (graphics, true);
    }
    writeReviewImage (
        withMark,
        scale < 1.125f ? "KIRIN_UI_MARK_OUTPUT"
          : scale < 1.375f ? "KIRIN_UI_MARK_OUTPUT_MEDIUM"
                           : "KIRIN_UI_MARK_OUTPUT_LARGE");

    const float markClearX = spectrum_geometry::markClearBoundsFor (
        markBounds, scale).getCentreX();
    spectrum.mouseDown (mouseEvent (spectrum, markClearX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (! spectrum.hasMark());
    juce::Image afterMarkClear (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (afterMarkClear);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_INTERACTION_REQUIRE (countDifferentPixels (withMark, afterMarkClear) > 40);
    const auto dataPlot = spectrum_geometry::dataPlotBoundsFor (
        componentBounds).toNearestInt();
    const auto markButton = markBounds.toNearestInt();
    KIRIN_INTERACTION_REQUIRE (
        countWarmChangedPixels (withMark, afterMarkClear, dataPlot) > 20);
    KIRIN_INTERACTION_REQUIRE (
        countWarmChangedPixels (withMark, afterMarkClear, markButton) > 8);
    KIRIN_INTERACTION_REQUIRE (countWarmPixels (afterMarkClear, markButton) > 8);
    spectrum.mouseDown (mouseEvent (spectrum, markX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (spectrum.hasMark());
    KirinSpectrumView warmingSnapshot = movedSnapshot;
    warmingSnapshot.status = KIRIN_SPECTRUM_WARMING_UP;
    warmingSnapshot.has_data = 0u;
    spectrum.setSnapshot (warmingSnapshot);
    KIRIN_INTERACTION_REQUIRE (! spectrum.hasMark());
    KIRIN_INTERACTION_REQUIRE (spectrum.focusTrailSizeForTest() == 0u);
    spectrum.setSnapshot (snapshot);
    KIRIN_INTERACTION_REQUIRE (spectrum.focusTrailSizeForTest() == 1u);
    KirinSpectrumView nextEndpoint = snapshot;
    nextEndpoint.presentation_end_samples += 1'600;
    spectrum.setSnapshot (nextEndpoint);
    KIRIN_INTERACTION_REQUIRE (spectrum.focusTrailSizeForTest() == 2u);
    KirinSpectrumView skippedEndpoint = nextEndpoint;
    skippedEndpoint.presentation_end_samples += 3'200;
    spectrum.setSnapshot (skippedEndpoint);
    KIRIN_INTERACTION_REQUIRE (spectrum.focusTrailSizeForTest() == 3u);

    SpectrumComponent recoveredSpectrum;
    recoveredSpectrum.setSize (width, height);
    KirinSpectrumBatch recovery {};
    recovery.count = 4u;
    for (uint32_t index = 0u; index < recovery.count; ++index)
    {
        recovery.frames[index] = snapshot;
        recovery.frames[index].presentation_end_samples += 1'600 * index;
        recovery.frames[index].display_db[0] += (float) index;
        recovery.frames[index].post_dbfs[0] += (float) index;
    }
    recovery.latest = recovery.frames[recovery.count - 1u];
    recoveredSpectrum.setBatch (recovery);
    KIRIN_INTERACTION_REQUIRE (recoveredSpectrum.focusTrailSizeForTest() == 4u);
    KirinSpectrumBatch invalidRecovery = recovery;
    invalidRecovery.frames[2].presentation_end_samples
        = invalidRecovery.frames[1].presentation_end_samples;
    recoveredSpectrum.setBatch (invalidRecovery);
    KIRIN_INTERACTION_REQUIRE (recoveredSpectrum.focusTrailSizeForTest() == 4u);

    uint8_t requestedChannelMode = 0xffu;
    spectrum.onChannelModeChange = [&requestedChannelMode] (uint8_t mode) {
        requestedChannelMode = mode;
        return true;
    };
    const float midX = spectrum_geometry::channelModeBoundsFor (
        1u, outerPlot, scale).getCentreX();
    spectrum.mouseDown (mouseEvent (spectrum, midX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (requestedChannelMode == KIRIN_SPECTRUM_CHANNEL_MID);
    KIRIN_INTERACTION_REQUIRE (
        spectrum.channelModeForTest() == KIRIN_SPECTRUM_CHANNEL_MID);
    KIRIN_INTERACTION_REQUIRE (! spectrum.hasFocusLock() && ! spectrum.hasMark()
                               && spectrum.focusTrailSizeForTest() == 0u);

    SpectrumComponent monoSpectrum;
    monoSpectrum.setSize (width, height);
    KirinSpectrumView monoSnapshot = snapshot;
    monoSnapshot.channels = 1u;
    monoSpectrum.setSnapshot (monoSnapshot);
    int monoModeCallbacks = 0;
    monoSpectrum.onChannelModeChange = [&monoModeCallbacks] (uint8_t) {
        ++monoModeCallbacks;
        return true;
    };
    const float sideX = spectrum_geometry::channelModeBoundsFor (
        2u, outerPlot, scale).getCentreX();
    monoSpectrum.mouseDown (mouseEvent (monoSpectrum, sideX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (monoModeCallbacks == 0);
    KIRIN_INTERACTION_REQUIRE (
        monoSpectrum.channelModeForTest() == KIRIN_SPECTRUM_CHANNEL_LR);

    SpectrumComponent pacedSpectrum;
    pacedSpectrum.setSize (width, height);
    pacedSpectrum.setSnapshot (snapshot);
    KirinSpectrumView queuedSnapshot = snapshot;
    queuedSnapshot.presentation_end_samples += 1'600;
    queuedSnapshot.display_db[0] += 6.0f;
    queuedSnapshot.post_dbfs[0] += 6.0f;
    const float heldDelta = pacedSpectrum.readoutDeltaForTest (0u);
    pacedSpectrum.queueSnapshot (queuedSnapshot);
    KIRIN_INTERACTION_REQUIRE (
        pacedSpectrum.presentedEndpointForTest() == snapshot.presentation_end_samples);
    const double now = juce::Time::getMillisecondCounterHiRes();
    pacedSpectrum.presentationTickAt (now + 50.0);
    KIRIN_INTERACTION_REQUIRE (
        pacedSpectrum.presentedEndpointForTest() == snapshot.presentation_end_samples);
    pacedSpectrum.presentationTickAt (now + 90.0);
    KIRIN_INTERACTION_REQUIRE (
        pacedSpectrum.presentedEndpointForTest()
            == queuedSnapshot.presentation_end_samples);
    KIRIN_INTERACTION_REQUIRE (
        std::abs (pacedSpectrum.readoutDeltaForTest (0u) - heldDelta) < 1.0e-6f);
    pacedSpectrum.presentationTickAt (now + 510.0);
    KIRIN_INTERACTION_REQUIRE (
        std::abs (pacedSpectrum.readoutDeltaForTest (0u) - heldDelta) > 0.1f);
}
}
