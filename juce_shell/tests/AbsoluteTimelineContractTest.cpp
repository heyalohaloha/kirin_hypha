#include "AbsoluteTimelineContractTest.h"

#include "../src/HyphaAbsoluteComponent.h"
#include "../src/HyphaTheme.h"
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
        std::cerr << "Absolute timeline contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

#define KIRIN_ABSOLUTE_REQUIRE(expression) require ((expression), #expression, __LINE__)

    KirinAbsoluteView frame (int index)
    {
        KirinAbsoluteView value {};
        value.status = KIRIN_SPECTRUM_ACTIVE;
        value.has_data = 1u;
        value.channels = 2u;
        value.sample_rate = 48'000u;
        value.aperture_samples = 4'800u;
        value.lufs_m = -24.0 + 0.12 * static_cast<double> (index);
        value.true_peak = -6.0 + 0.05 * static_cast<double> (index);
        value.sharpness = 0.8 + 0.01 * static_cast<double> (index);
        value.presentation_end_samples = static_cast<int64_t> (index) * 4'800;
        value.state_epoch_samples = 0;
        value.generation = 1u;
        return value;
    }

    KirinAbsoluteBatch batch (int first, int last)
    {
        KirinAbsoluteBatch value {};
        for (int index = first; index <= last; ++index)
            value.frames[value.count++] = frame (index);
        value.latest = value.frames[value.count - 1u];
        return value;
    }

    double renderAt (const ui_contract::SpectrumSizePreset& preset)
    {
        AbsoluteComponent component;
        const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        component.setSize (bounds.width, bounds.height);
        component.setBatch (batch (1, 60));
        juce::Image image (juce::Image::ARGB, bounds.width, bounds.height, true);
        constexpr int iterations = 200;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int index = 0; index < iterations; ++index)
        {
            image.clear (image.getBounds(), BG);
            juce::Graphics graphics (image);
            component.paintEntireComponent (graphics, true);
        }
        return (juce::Time::getMillisecondCounterHiRes() - started) / iterations;
    }
}

void verifyAbsoluteTimelineContract()
{
    static_assert (sizeof (KirinAbsoluteView) == 64u);
    static_assert (sizeof (KirinAbsoluteBatch) == 4'168u);

    AbsoluteComponent component;
    component.setBatch (batch (1, 60));
    KIRIN_ABSOLUTE_REQUIRE (component.frameCountForTest() == 60u);
    KIRIN_ABSOLUTE_REQUIRE (component.newestEndpointForTest() == 288'000);

    auto broken = batch (1, 3);
    broken.frames[1].presentation_end_samples += 1;
    component.setBatch (broken);
    KIRIN_ABSOLUTE_REQUIRE (component.frameCountForTest() == 60u);

    AbsoluteComponent gated;
    gated.setBatchAt (batch (1, 1), 0.0);
    KIRIN_ABSOLUTE_REQUIRE (gated.curvePresentationCountForTest() == 1u);
    KIRIN_ABSOLUTE_REQUIRE (gated.numericPresentationCountForTest() == 1u);
    gated.setBatchAt (batch (1, 2), 100.0);
    KIRIN_ABSOLUTE_REQUIRE (gated.curvePresentationCountForTest() == 1u);
    KIRIN_ABSOLUTE_REQUIRE (gated.numericPresentationCountForTest() == 1u);
    gated.setBatchAt (batch (1, 3), 200.0);
    KIRIN_ABSOLUTE_REQUIRE (gated.curvePresentationCountForTest() == 2u);
    gated.setBatchAt (batch (1, 4), 400.0);
    KIRIN_ABSOLUTE_REQUIRE (gated.curvePresentationCountForTest() == 3u);
    gated.setBatchAt (batch (1, 5), 500.0);
    KIRIN_ABSOLUTE_REQUIRE (gated.curvePresentationCountForTest() == 3u);
    KIRIN_ABSOLUTE_REQUIRE (gated.numericPresentationCountForTest() == 2u);
    KIRIN_ABSOLUTE_REQUIRE (ui_contract::absoluteTimelineSourceHz == 10);

    const double compact = renderAt (ui_contract::spectrumSizePresets[0]);
    const double medium = renderAt (ui_contract::spectrumSizePresets[1]);
    const double large = renderAt (ui_contract::spectrumSizePresets[2]);
    std::cout << "Absolute timeline paint samples: " << compact << '/'
              << medium << '/' << large << " ms/frame\n";
    KIRIN_ABSOLUTE_REQUIRE (compact < 1.5);
    KIRIN_ABSOLUTE_REQUIRE (medium < 2.5);
    KIRIN_ABSOLUTE_REQUIRE (large < 3.5);
}
}
