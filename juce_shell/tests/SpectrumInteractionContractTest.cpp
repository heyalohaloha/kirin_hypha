#include "SpectrumInteractionContractTest.h"

#include "../src/HyphaSpectrumUiContract.h"
#include "../src/HyphaUiContract.h"

#include <cstdlib>
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
    const float controlY = (float) ui::spectrumPlotTopInset
                         + (float) ui::spectrumChannelModeTop
                         + 0.5f * (float) ui::spectrumChannelModeHeight;
    const float markX = (float) width
                      - (float) ui::spectrumPlotRightInset
                      - 0.5f * (float) ui::spectrumMarkWidth;
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

    const float markClearX = (float) width
                           - (float) ui::spectrumPlotRightInset - 2.0f;
    spectrum.mouseDown (mouseEvent (spectrum, markClearX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (! spectrum.hasMark());
    juce::Image afterMarkClear (
        juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
    {
        juce::Graphics graphics (afterMarkClear);
        spectrum.paintEntireComponent (graphics, true);
    }
    KIRIN_INTERACTION_REQUIRE (countDifferentPixels (withMark, afterMarkClear) > 40);
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
    KIRIN_INTERACTION_REQUIRE (spectrum.focusTrailSizeForTest() == 1u);

    uint8_t requestedChannelMode = 0xffu;
    spectrum.onChannelModeChange = [&requestedChannelMode] (uint8_t mode) {
        requestedChannelMode = mode;
        return true;
    };
    const float midX = (float) ui::spectrumPlotLeftInset
                     + (float) ui::spectrumChannelModeWidths[0]
                     + (float) ui::spectrumChannelModeGap
                     + 0.5f * (float) ui::spectrumChannelModeWidths[1];
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
    const float sideX = (float) ui::spectrumPlotLeftInset
                      + (float) ui::spectrumChannelModeWidths[0]
                      + (float) ui::spectrumChannelModeGap
                      + (float) ui::spectrumChannelModeWidths[1]
                      + (float) ui::spectrumChannelModeGap
                      + 0.5f * (float) ui::spectrumChannelModeWidths[2];
    monoSpectrum.mouseDown (mouseEvent (monoSpectrum, sideX, controlY, eventTime));
    KIRIN_INTERACTION_REQUIRE (monoModeCallbacks == 0);
    KIRIN_INTERACTION_REQUIRE (
        monoSpectrum.channelModeForTest() == KIRIN_SPECTRUM_CHANNEL_LR);
}
}
