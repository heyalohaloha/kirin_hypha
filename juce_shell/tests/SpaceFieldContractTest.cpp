#include "SpaceFieldContractTest.h"

#include "../src/HyphaMonoSumPainter.h"

#include <cmath>
#include <limits>

#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "SPACE field contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_SPACE_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession fixture (bool sideDominant)
{
    KirinMeterSession meter {};
    meter.generation = 12;
    meter.active_frames = 48'000u * 18u;
    meter.observed_frames = meter.active_frames;
    meter.sample_rate = 48'000;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -17.0;
    meter.channels = 2;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.balance_db = sideDominant ? -1.4 : 0.6;
    meter.correlation = sideDominant ? -0.72 : 0.94;
    meter.field_size = KIRIN_STEREO_FIELD_SIZE;
    meter.field_observation_count = 30;
    constexpr size_t centre = KIRIN_STEREO_FIELD_SIZE / 2u;
    for (size_t offset = 2u; offset < KIRIN_STEREO_FIELD_SIZE - 2u; ++offset)
    {
        const size_t row = sideDominant ? centre : offset;
        const size_t column = sideDominant ? offset : centre;
        const int distance = std::abs ((int) offset - (int) centre);
        meter.field_density[row * KIRIN_STEREO_FIELD_SIZE + column]
            = static_cast<uint8_t> (juce::jmax (36, 255 - distance * 17));
    }
    // A plausible MONO shape: an ordinary mix near -0.7 dB, a low-end cancellation around 80 Hz,
    // and two bands with nothing to measure so the curve has to break rather than draw 0 dB.
    meter.mono_sum_band_count = (uint8_t) KIRIN_MONO_SUM_BAND_COUNT;
    meter.mono_sum_approximate_below_hz = 30.0f;
    for (size_t band = 0u; band < KIRIN_MONO_SUM_BAND_COUNT; ++band)
    {
        const float ratio = KIRIN_MONO_SUM_MAX_HZ / KIRIN_MONO_SUM_MIN_HZ;
        const float centre = KIRIN_MONO_SUM_MIN_HZ
            * std::pow (ratio, ((float) band + 0.5f) / (float) KIRIN_MONO_SUM_BAND_COUNT);
        const float octavesFrom80 = std::log2 (centre / 80.0f);
        meter.mono_sum_db[band] = -0.7f - 17.0f * std::exp (-octavesFrom80 * octavesFrom80 * 2.0f);
    }
    meter.mono_sum_db[1] = std::numeric_limits<float>::quiet_NaN();
    meter.mono_sum_db[2] = std::numeric_limits<float>::quiet_NaN();
    return meter;
}

juce::Image render (const KirinMeterSession& meter, int width, int height)
{
    observatory::View view (observatory::Role::post);
    view.setSize (width, height);
    view.setDomain (observatory::Domain::space);
    view.setMeterSnapshot (meter, true);
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int changedPixels (const juce::Image& first, const juce::Image& second)
{
    KIRIN_SPACE_REQUIRE (first.getBounds() == second.getBounds());
    int count = 0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x)
            count += first.getPixelAt (x, y).getARGB()
                  != second.getPixelAt (x, y).getARGB();
    return count;
}
}

namespace
{
/// The split scale is the contract: the top half carries 0..-6 dB because that is where every
/// value a user acts on lives, and the bottom half carries -6..-24 dB.
void verifyMonoSumScale()
{
    const juce::Rectangle<float> plot { 0.0f, 100.0f, 200.0f, 80.0f };
    const auto top = mono_sum_curve::yForDb (0.0f, plot);
    const auto middle = mono_sum_curve::yForDb (KIRIN_MONO_SUM_DISPLAY_MIDPOINT_DB, plot);
    const auto floor = mono_sum_curve::yForDb (KIRIN_MONO_SUM_DISPLAY_FLOOR_DB, plot);
    KIRIN_SPACE_REQUIRE (std::abs (top - plot.getY()) < 0.01f);
    KIRIN_SPACE_REQUIRE (std::abs (middle - (plot.getY() + plot.getHeight() * 0.5f)) < 0.01f);
    KIRIN_SPACE_REQUIRE (std::abs (floor - plot.getBottom()) < 0.01f);

    // -3.01 dB is a hard-panned source and lands exactly halfway down the top half.
    const auto panned = mono_sum_curve::yForDb (-3.0103f, plot);
    const auto expected = plot.getY() + plot.getHeight() * 0.25f;
    KIRIN_SPACE_REQUIRE (std::abs (panned - expected) < 0.35f);

    // Beyond the scale the value stops at its edge rather than leaving the plot.
    KIRIN_SPACE_REQUIRE (mono_sum_curve::yForDb (-60.0f, plot) <= plot.getBottom() + 0.01f);
    KIRIN_SPACE_REQUIRE (mono_sum_curve::yForDb (5.0f, plot) >= plot.getY() - 0.01f);
}

KirinMeterSession flatMonoFixture (float db)
{
    auto meter = fixture (false);
    for (auto& value : meter.mono_sum_db)
        value = db;
    return meter;
}

int inkInColumn (const juce::Image& image, int x, int fromY, int toY)
{
    int count = 0;
    for (int y = fromY; y < toY; ++y)
        count += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
    return count;
}

/// The curve has to move with the value, stop where the scale stops, and break where a band was
/// not measured rather than drawing the one reading that means the band loses nothing.
void verifyMonoSumCurveRendering()
{
    // The editor that shows MONO. The smaller ones are covered by the size sweep below, which
    // requires them to be untouched by these same values.
    constexpr int kWidth = 900;
    constexpr int kHeight = 600;
    const auto identical = render (flatMonoFixture (0.0f), kWidth, kHeight);
    const auto panned = render (flatMonoFixture (-3.0103f), kWidth, kHeight);
    const auto collapsed = render (flatMonoFixture (KIRIN_MONO_SUM_DISPLAY_FLOOR_DB),
                                   kWidth, kHeight);
    KIRIN_SPACE_REQUIRE (changedPixels (identical, panned) > 100);
    KIRIN_SPACE_REQUIRE (changedPixels (panned, collapsed) > 100);

    // A band with nothing to measure leaves its column without a curve, while its neighbours keep
    // theirs. Both frames are otherwise the same, so the difference is the break itself.
    auto broken = flatMonoFixture (0.0f);
    for (size_t band = 12u; band < 20u; ++band)
        broken.mono_sum_db[band] = std::numeric_limits<float>::quiet_NaN();
    const auto withBreak = render (broken, kWidth, kHeight);
    KIRIN_SPACE_REQUIRE (changedPixels (identical, withBreak) > 40);

    // MONO is added only where it costs nothing. A size that does not show it has to be untouched
    // by the values rather than drawing a squashed plot. Both halves matter: a size that
    // half-draws it is worse than one that leaves it out.
    int showing = 0;
    for (const auto dimensions : {
             std::pair { 300, 200 }, std::pair { 375, 250 }, std::pair { 450, 300 },
             std::pair { 600, 400 }, std::pair { 900, 600 } })
    {
        const auto present = render (flatMonoFixture (-1.0f), dimensions.first, dimensions.second);
        const auto absent = render (flatMonoFixture (KIRIN_MONO_SUM_DISPLAY_FLOOR_DB),
                                    dimensions.first, dimensions.second);
        const auto difference = changedPixels (present, absent);
        KIRIN_SPACE_REQUIRE (difference == 0 || difference > 20);
        showing += difference > 0 ? 1 : 0;

        // The density scatter is what SPACE has always shown, and no size may have lost it.
        auto withoutField = flatMonoFixture (-1.0f);
        withoutField.field_observation_count = 0;
        KIRIN_SPACE_REQUIRE (changedPixels (present, render (withoutField, dimensions.first,
                                                             dimensions.second)) > 40);
    }
    // Only the editor with room for both shows MONO. Everywhere else SPACE is exactly the panel
    // that shipped before it, rather than a smaller scatter beside a squashed chart.
    KIRIN_SPACE_REQUIRE (showing == 1);

    // Mono input has no stereo to lose, so no curve is drawn and the state says why.
    auto monoInput = flatMonoFixture (-1.0f);
    monoInput.channels = 1;
    monoInput.mono_sum_band_count = 0;
    for (auto& value : monoInput.mono_sum_db)
        value = std::numeric_limits<float>::quiet_NaN();
    const auto withoutStereo = render (monoInput, kWidth, kHeight);
    KIRIN_SPACE_REQUIRE (changedPixels (render (flatMonoFixture (-1.0f), kWidth, kHeight),
                                        withoutStereo) > 100);
    KIRIN_SPACE_REQUIRE (! mono_sum_curve::hasBands (monoInput, true));
    KIRIN_SPACE_REQUIRE (mono_sum_curve::hasBands (flatMonoFixture (-1.0f), true));
    KIRIN_SPACE_REQUIRE (! mono_sum_curve::hasBands (flatMonoFixture (-1.0f), false));
}
}

void verifySpaceFieldContract()
{
    verifyMonoSumScale();
    verifyMonoSumCurveRendering();
    const auto mid = fixture (false);
    const auto side = fixture (true);
    const auto midImage = render (mid, 600, 400);
    const auto sideImage = render (side, 600, 400);
    KIRIN_SPACE_REQUIRE (changedPixels (midImage, sideImage) > 500);
    const auto outputDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_SPACE_TEST_DIR", {});

    for (const auto dimensions : {
             std::pair { 300, 200 }, std::pair { 375, 250 },
             std::pair { 450, 300 }, std::pair { 600, 400 },
             std::pair { 900, 600 } })
    {
        const auto image = render (mid, dimensions.first, dimensions.second);
        int visible = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                visible += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
        KIRIN_SPACE_REQUIRE (visible == image.getWidth() * image.getHeight());
        if (outputDirectory.isNotEmpty())
        {
            const auto directory = juce::File (outputDirectory);
            KIRIN_SPACE_REQUIRE (directory.createDirectory().wasOk());
            const auto file = directory.getChildFile (
                "hypha-space-" + juce::String (dimensions.first) + "x"
                + juce::String (dimensions.second) + ".png");
            auto output = file.createOutputStream();
            KIRIN_SPACE_REQUIRE (output != nullptr);
            KIRIN_SPACE_REQUIRE (
                juce::PNGImageFormat().writeImageToStream (image, *output));
        }
    }

    auto mono = mid;
    mono.channels = 1;
    mono.field_size = 0;
    mono.field_observation_count = 0;
    KIRIN_SPACE_REQUIRE (changedPixels (midImage, render (mono, 600, 400)) > 300);

    // The editor keeps its View and backing surface between timer ticks, so the gate has to
    // measure the repaint alone. Building a View and a fresh surface inside the loop measured
    // construction as well and reported about twice the production cost, which both hides a real
    // repaint regression and would fail the gate for work the plug-in never repeats.
    constexpr int warmupIterations = 3;
    constexpr int paintIterations = 30;
    observatory::View steady (observatory::Role::post);
    steady.setSize (600, 400);
    steady.setDomain (observatory::Domain::space);
    steady.setMeterSnapshot (mid, true);
    juce::Image surface (juce::Image::ARGB, 600, 400, true);
    const auto repaint = [&steady, &surface] {
        juce::Graphics graphics (surface);
        steady.paintEntireComponent (graphics, true);
    };
    for (int index = 0; index < warmupIterations; ++index)
        repaint();

    const double startedMs = juce::Time::getMillisecondCounterHiRes();
    for (int index = 0; index < paintIterations; ++index)
        repaint();
    const double paintMs = (juce::Time::getMillisecondCounterHiRes() - startedMs)
                         / paintIterations;
    std::cout << "SPACE density paint: " << paintMs << " ms/frame\n";
    KIRIN_SPACE_REQUIRE (paintMs < 12.0);

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_SPACE_TEST_PNG", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_SPACE_REQUIRE (output != nullptr);
        KIRIN_SPACE_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (midImage, *output));
    }
}
}
