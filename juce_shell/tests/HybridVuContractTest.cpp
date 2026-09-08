#include "HybridVuContractTest.h"

#include "../src/HyphaHybridVuPainter.h"
#include "../src/HyphaObservatoryView.h"

#include <cmath>
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
    std::cerr << "Hybrid VU contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_HYBRID_VU_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession meterFixture()
{
    KirinMeterSession meter {};
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.channels = 2;
    meter.true_peak = -3.2;
    meter.max_true_peak = -1.2;
    meter.channel_vu_dbfs[0] = -26.0;
    meter.channel_vu_dbfs[1] = -25.0;
    meter.channel_instant_true_peak_dbtp[0] = -3.2;
    meter.channel_instant_true_peak_dbtp[1] = -4.7;
    meter.channel_max_true_peak_dbtp[0] = -1.2;
    meter.channel_max_true_peak_dbtp[1] = -1.5;
    return meter;
}

KirinWatchDisplay watchFixture()
{
    KirinWatchDisplay watch {};
    watch.current.lufs_m = -18.2;
    watch.current.lufs_s = -18.8;
    watch.current.true_peak = -3.2;
    watch.current.crest = 11.4;
    return watch;
}

juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_HYBRID_VU_REQUIRE (left.getBounds() == right.getBounds());
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}
}

void verifyHybridVuContract()
{
    KIRIN_HYBRID_VU_REQUIRE (std::abs (hybrid_vu::vuNormalized (-38.0)) < 1.0e-6f);
    KIRIN_HYBRID_VU_REQUIRE (
        std::abs (hybrid_vu::vuNormalized (-15.0) - 1.0f) < 1.0e-6f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::vuNormalized (-18.0) > 0.60f);
    KIRIN_HYBRID_VU_REQUIRE (
        std::abs (hybrid_vu::truePeakNormalized (-24.0)) < 1.0e-6f);
    KIRIN_HYBRID_VU_REQUIRE (
        std::abs (hybrid_vu::truePeakNormalized (0.0) - 1.0f) < 1.0e-6f);

    const auto meter = meterFixture();
    for (const auto role : { observatory::Role::pre, observatory::Role::post })
        for (const auto preset : observatory::sizePresets)
        {
            observatory::View view (role);
            view.setSize (preset.width, preset.height);
            view.setDomain (observatory::Domain::time);
            view.setConnection (role == observatory::Role::post ? "PAIR DRUM" : "SOURCE PRE",
                                COL_LED_BLUE,
                                role == observatory::Role::post
                                    ? observatory::ConnectionState::paired
                                    : observatory::ConnectionState::source);
            view.setMeterSnapshot (meter, true);
            view.setWatchDisplay (watchFixture(), true);
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (true));
            KIRIN_HYBRID_VU_REQUIRE (view.hybridVuVisible());
            const auto image = render (view);
            const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
                "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
            if (previewDirectory.isNotEmpty())
            {
                auto output = juce::File (previewDirectory).getChildFile (
                    juce::String (role == observatory::Role::pre ? "pre" : "post")
                    + "-hybrid-vu-" + juce::String (preset.width) + ".png").createOutputStream();
                KIRIN_HYBRID_VU_REQUIRE (output != nullptr);
                KIRIN_HYBRID_VU_REQUIRE (
                    juce::PNGImageFormat().writeImageToStream (image, *output));
            }
            auto alternate = meter;
            alternate.channel_vu_dbfs[0] = -25.0;
            alternate.channel_instant_true_peak_dbtp[1] = -11.0;
            view.setMeterSnapshot (alternate, true);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (image, render (view)) > 24);
            auto clipped = meter;
            clipped.clip_events[0] = 1;
            view.setMeterSnapshot (clipped, true);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (image, render (view)) > 8);
            view.setMeterSnapshot (meter, true);
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (false));
            KIRIN_HYBRID_VU_REQUIRE (! view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.domain() == observatory::Domain::time);
        }
    std::cout << "Hybrid VU: PASS (PRE/POST, five exact sizes, needles, TP rails, restore)\n";
}
}
