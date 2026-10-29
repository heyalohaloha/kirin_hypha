#include "HybridVuContractTest.h"
#include "HybridVuCalibrationContract.h"

#include "../src/HyphaHybridVuPainter.h"
#include "../src/HyphaVuLayerCache.h"
#include "../src/HyphaVuNeedleAnimator.h"
#include "../src/HyphaObservatoryView.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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
    meter.channel_vu_dbfs[0] = -28.0;
    meter.channel_vu_dbfs[1] = -27.0;
    meter.channel_instant_true_peak_dbtp[0] = -18.0;
    meter.channel_instant_true_peak_dbtp[1] = -17.0;
    meter.channel_max_true_peak_dbtp[0] = -3.2;
    meter.channel_max_true_peak_dbtp[1] = -4.7;
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

void writePreview (const juce::Image& image,
                   observatory::Role role,
                   const juce::String& state,
                   int width)
{
    const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    if (previewDirectory.isEmpty())
        return;

    auto output = juce::File (previewDirectory).getChildFile (
        juce::String (role == observatory::Role::pre ? "pre" : "post")
        + "-hybrid-vu-" + state + "-" + juce::String (width) + ".png").createOutputStream();
    KIRIN_HYBRID_VU_REQUIRE (output != nullptr);
    KIRIN_HYBRID_VU_REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *output));
}
}

void verifyHybridVuContract()
{
    vu_calibration_contract::verify();
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::vuNormalized (-38.0) > 0.06f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::vuNormalized (-38.0) < 0.08f);
    KIRIN_HYBRID_VU_REQUIRE (
        std::abs (hybrid_vu::vuNormalized (-15.0) - 1.0f) < 1.0e-6f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::vuNormalized (-18.0) > 0.77f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::vuNormalized (-18.0) < 0.79f);
    KIRIN_HYBRID_VU_REQUIRE (
        std::abs (hybrid_vu::truePeakNormalized (-28.0)) < 1.0e-6f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::truePeakNormalized (-24.0) > 0.14f);
    KIRIN_HYBRID_VU_REQUIRE (hybrid_vu::truePeakNormalized (-24.0) < 0.15f);
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
            auto* vuButton = dynamic_cast<juce::Button*> (
                view.findChildWithID ("observatory-hybrid-vu"));
            auto* clearButton = dynamic_cast<juce::Button*> (
                view.findChildWithID ("observatory-clear-peak-clip"));
            KIRIN_HYBRID_VU_REQUIRE (vuButton != nullptr);
            KIRIN_HYBRID_VU_REQUIRE (clearButton != nullptr);
            KIRIN_HYBRID_VU_REQUIRE (vuButton->isVisible());
            KIRIN_HYBRID_VU_REQUIRE (! clearButton->isVisible());
            bool retainedManualSelection = false;
            bool clearCallback = false;
            view.onHybridVuChange = [&view, &retainedManualSelection] (bool)
            { retainedManualSelection = view.manualHybridVuVisible(); };
            view.onClearPeakClipHolds = [&clearCallback] { clearCallback = true; };
            vuButton->onClick();
            KIRIN_HYBRID_VU_REQUIRE (retainedManualSelection);
            KIRIN_HYBRID_VU_REQUIRE (view.manualHybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (vuButton->isVisible());
            KIRIN_HYBRID_VU_REQUIRE (clearButton->isVisible());
            KIRIN_HYBRID_VU_REQUIRE (
                view.getLocalBounds().contains (vuButton->getBounds()));
            KIRIN_HYBRID_VU_REQUIRE (
                view.getLocalBounds().contains (clearButton->getBounds()));
            KIRIN_HYBRID_VU_REQUIRE (
                ! vuButton->getBounds().intersects (clearButton->getBounds()));
            const auto manualImage = render (view);
            writePreview (manualImage, role, "manual", preset.width);
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (true));
            const auto recordingImage = render (view);
            writePreview (recordingImage, role, "recording", preset.width);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (manualImage, recordingImage) > 12);
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (false));
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (manualImage, render (view)) == 0);
            clearButton->onClick();
            KIRIN_HYBRID_VU_REQUIRE (clearCallback);
            observatory::View reopened (role);
            reopened.setSize (preset.width, preset.height);
            reopened.setManualHybridVuVisible (retainedManualSelection);
            KIRIN_HYBRID_VU_REQUIRE (reopened.manualHybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (reopened.hybridVuVisible());
            vuButton->onClick();
            KIRIN_HYBRID_VU_REQUIRE (! retainedManualSelection);
            KIRIN_HYBRID_VU_REQUIRE (! view.manualHybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (! view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (vuButton->isVisible());
            KIRIN_HYBRID_VU_REQUIRE (! clearButton->isVisible());
            KIRIN_HYBRID_VU_REQUIRE (! view.setHybridVuOnRecordEnabled (true));
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (true));
            KIRIN_HYBRID_VU_REQUIRE (view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.informationAnchor().isVisible());
            const auto image = render (view);
            auto alternate = meter;
            alternate.channel_vu_dbfs[0] = -25.0;
            alternate.channel_instant_true_peak_dbtp[1] = -11.0;
            view.setMeterSnapshot (alternate, true);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (image, render (view)) > 24);
            auto sessionClipOnly = meter;
            sessionClipOnly.clip_events[0] = 1;
            view.setMeterSnapshot (sessionClipOnly, true);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (image, render (view)) == 0);
            auto clipped = sessionClipOnly;
            clipped.channel_clip_latched[0] = 1;
            view.setMeterSnapshot (clipped, true);
            KIRIN_HYBRID_VU_REQUIRE (differentPixels (image, render (view)) > 8);
            view.setMeterSnapshot (meter, true);
            KIRIN_HYBRID_VU_REQUIRE (view.dismissHybridVuForCurrentRecording());
            KIRIN_HYBRID_VU_REQUIRE (! view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (! view.dismissHybridVuForCurrentRecording());
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (false));
            KIRIN_HYBRID_VU_REQUIRE (! view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.domain() == observatory::Domain::time);
            KIRIN_HYBRID_VU_REQUIRE (view.setHybridVuOnRecordEnabled (false));
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (true));
            KIRIN_HYBRID_VU_REQUIRE (! view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (false));
            KIRIN_HYBRID_VU_REQUIRE (view.setHybridVuOnRecordEnabled (true));
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (true));
            KIRIN_HYBRID_VU_REQUIRE (view.hybridVuVisible());
            KIRIN_HYBRID_VU_REQUIRE (view.setHostRecording (false));
        }
    std::cout << "Hybrid VU: PASS (PRE/POST, five sizes, preference, manual override)\n";
}

// Exercise the actual offscreen painter with one 100 ms publication step. The trace
// reports dial coordinates as well as producing an optional contact sheet.
void verifyVuFrameTrace()
{
    auto meter = meterFixture();
    auto watch = watchFixture();
    juce::Image sheet (juce::Image::ARGB, 1200, 600, true);
    juce::Graphics sheetGraphics (sheet);
    double previous = 0.0, largest = 0.0;
    bool intermediate = false;
    const bool legacy = juce::SystemStats::getEnvironmentVariable ("KIRIN_VU_TRACE_LEGACY", {}) == "1";
    observatory::View view (observatory::Role::pre);
    view.setSize (300, 200);
    meter.channel_vu_dbfs[0] = meter.channel_vu_dbfs[1] = -58.0;
    view.setMeterSnapshot (meter, true);
    view.setWatchDisplay (watch, true);
    view.setConnection ({}, COL_NORMAL, observatory::ConnectionState::source);
    view.setManualHybridVuVisible (true);
    if (! legacy) view.setVuFrameTestMode (true);
    for (int frame = 0; frame < 12; ++frame)
    {
        meter.channel_vu_dbfs[0] = frame < 6 ? -38.0 : -28.0;
        meter.channel_vu_dbfs[1] = meter.channel_vu_dbfs[0];
        const auto goal = hybrid_vu::vuNormalized (meter.channel_vu_dbfs[0]);
        if (! legacy)
        {
            view.setMeterSnapshot (meter, true);
            juce::Image update (juce::Image::ARGB, 300, 200, true);
            juce::Graphics updateGraphics (update);
            view.paint (updateGraphics);
            view.tickVuForTest (1.0 / 60.0);
        }
        const auto position = legacy ? static_cast<double> (goal) : view.vuPositionForTest (0);
        largest = std::max (largest, std::abs (position - previous));
        const auto published = static_cast<double> (hybrid_vu::vuNormalized (meter.channel_vu_dbfs[0]));
        intermediate |= std::abs (position - published) > 1.0e-6;
        previous = position;
        std::cout << "frame=" << frame << " time_ms=" << (frame + 1) * 1000.0 / 60.0
                  << " published_dbfs=" << meter.channel_vu_dbfs[0]
                  << " needle=" << position << '\n';
        juce::Image image (juce::Image::ARGB, 300, 200, true);
        juce::Graphics graphics (image);
        if (legacy)
        {
            hybrid_vu::State state { observatory::Role::pre, meter, watch, true, true, true,
                false, false, {}, COL_NORMAL, false, presentation::defaultContext(), -18 };
            hybrid_vu::paint (graphics, image.getBounds(), state);
        }
        else view.paint (graphics);
        sheetGraphics.drawImageAt (image, (frame % 4) * 300, (frame / 4) * 200);
    }
    const auto destination = juce::SystemStats::getEnvironmentVariable ("KIRIN_VU_TRACE_IMAGE", {});
    if (destination.isNotEmpty())
    {
        auto output = juce::File (destination).createOutputStream();
        KIRIN_HYBRID_VU_REQUIRE (output != nullptr);
        KIRIN_HYBRID_VU_REQUIRE (juce::PNGImageFormat().writeImageToStream (sheet, *output));
    }
    std::cout << "max_frame_jump=" << largest << " intermediate=" << intermediate << std::endl;
    KIRIN_HYBRID_VU_REQUIRE (intermediate && largest < 0.06);
}


void verifyVuLayerParity()
{
    auto meter = meterFixture();
    auto watch = watchFixture();
    const hybrid_vu::State state { observatory::Role::pre, meter, watch,
        true, true, true, false, false, {}, COL_NORMAL, false,
        presentation::forEditor (900, 600), -18 };
    juce::Image direct (juce::Image::ARGB, 900, 600, true);
    juce::Graphics directGraphics (direct);
    hybrid_vu::paint (directGraphics, direct.getBounds(), state);
    juce::Image layered (juce::Image::ARGB, 900, 600, true);
    hybrid_vu::LayerCache cache;
    {
        juce::Graphics layeredGraphics (layered);
        cache.paint (layeredGraphics, layered.getBounds(), state, 1.0f,
                     hybrid_vu::vuNormalized (meter.channel_vu_dbfs[0]),
                     hybrid_vu::vuNormalized (meter.channel_vu_dbfs[1]));
    }
    const auto differences = differentPixels (direct, layered);
    int maxChannelDifference = 0, overOne = 0;
    for (int y = 0; y < direct.getHeight(); ++y)
        for (int x = 0; x < direct.getWidth(); ++x)
        {
            const auto a = direct.getPixelAt (x, y), b = layered.getPixelAt (x, y);
            const auto delta = juce::jmax (std::abs ((int) a.getRed() - (int) b.getRed()),
                std::abs ((int) a.getGreen() - (int) b.getGreen()),
                std::abs ((int) a.getBlue() - (int) b.getBlue()),
                std::abs ((int) a.getAlpha() - (int) b.getAlpha()));
            maxChannelDifference = juce::jmax (maxChannelDifference, delta);
            overOne += delta > 1;
        }
    const auto dir = juce::SystemStats::getEnvironmentVariable ("KIRIN_VU_PARITY_DIR", {});
    if (dir.isNotEmpty())
        for (const auto& image : { std::make_pair ("direct.png", direct), std::make_pair ("layered.png", layered) })
        {
            auto output = juce::File (dir).getChildFile (image.first).createOutputStream();
            KIRIN_HYBRID_VU_REQUIRE (output != nullptr);
            KIRIN_HYBRID_VU_REQUIRE (juce::PNGImageFormat().writeImageToStream (image.second, *output));
        }
    std::cout << "layered_vs_direct_different_pixels=" << differences
              << " max_channel_delta=" << maxChannelDifference << " over_one=" << overOne << std::endl;
    // Separately composited 8-bit alpha can round a channel by up to two levels.
    KIRIN_HYBRID_VU_REQUIRE (maxChannelDifference <= 2 && differences <= 2700);
    KIRIN_HYBRID_VU_REQUIRE (cache.staticBuildsForTest() == 1);
    meter.channel_vu_dbfs[0] = -25.0;
    { juce::Graphics g (layered); cache.paint (g, layered.getBounds(), state, 1.0f, 0.3f, 0.3f); }
    KIRIN_HYBRID_VU_REQUIRE (cache.staticBuildsForTest() == 1);
    meter.channels = 1;
    { juce::Graphics g (layered); cache.paint (g, layered.getBounds(), state, 1.0f, 0.3f, 0.0f); }
    KIRIN_HYBRID_VU_REQUIRE (cache.staticBuildsForTest() == 1);
    auto calibrated = state;
    calibrated.calibrationDbfs = -20;
    { juce::Graphics g (layered); cache.paint (g, layered.getBounds(), calibrated, 1.0f, 0.3f, 0.0f); }
    KIRIN_HYBRID_VU_REQUIRE (cache.staticBuildsForTest() == 2);
    meter.channels = 2;

    for (const auto& pair : { std::make_pair (0.0f, 0.02f), std::make_pair (0.25f, 0.3f),
                              std::make_pair (0.5f, 0.7f), std::make_pair (0.0f, 1.0f) })
    {
        juce::Image before (juce::Image::ARGB, 900, 600, true), after (juce::Image::ARGB, 900, 600, true);
        { juce::Graphics g (before); cache.paint (g, before.getBounds(), state, 1.0f, pair.first, pair.first); }
        { juce::Graphics g (after); cache.paint (g, after.getBounds(), state, 1.0f, pair.second, pair.second); }
        auto dirty = hybrid_vu::needle_painter::dirtyRegions (before.getBounds(), 0, pair.first, pair.second);
        dirty.add (hybrid_vu::needle_painter::dirtyRegions (before.getBounds(), 1, pair.first, pair.second));
        for (int y = 0; y < before.getHeight(); ++y)
            for (int x = 0; x < before.getWidth(); ++x)
                if (before.getPixelAt (x, y).getARGB() != after.getPixelAt (x, y).getARGB())
                    KIRIN_HYBRID_VU_REQUIRE (dirty.containsPoint (x, y));
    }
}


void measureVuFramePerformance()
{
    auto meter = meterFixture();
    auto watch = watchFixture();
    const hybrid_vu::State state { observatory::Role::pre, meter, watch,
        true, true, true, false, false, {}, COL_NORMAL, false,
        presentation::forEditor (900, 600), -18 };
    const juce::Rectangle<int> bounds (0, 0, 900, 600);
    for (const auto scale : { 1.0f, 1.5f, 2.0f, 3.0f })
    {
        juce::Image image (juce::Image::ARGB, juce::roundToInt (900 * scale),
                           juce::roundToInt (600 * scale), true);
        hybrid_vu::LayerCache cache;
        auto draw = [&] (bool cached, int frame)
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            if (cached)
            {
                const auto next = static_cast<float> (frame % 60) / 60.0f;
                const auto previous = static_cast<float> (juce::jmax (0, frame - 1) % 60) / 60.0f;
                auto dirty = hybrid_vu::needle_painter::dirtyRegions (bounds, 0, previous, next);
                dirty.add (hybrid_vu::needle_painter::dirtyRegions (bounds, 1, previous, next));
                g.reduceClipRegion (dirty);
                cache.paint (g, bounds, state, scale, next, next);
            }
            else hybrid_vu::paint (g, bounds, state);
        };
        draw (true, 0);
        draw (false, 0);
        // Full and needle frames take turns over seven rounds and the medians are compared, so a
        // pause in one stretch of the run can neither fail the check nor hide a slow return.
        std::vector<double> full, cached;
        for (int round = 0; round < 7; ++round)
        {
            auto begin = juce::Time::getMillisecondCounterHiRes();
            for (int frame = 0; frame < 6; ++frame) draw (false, frame);
            full.push_back ((juce::Time::getMillisecondCounterHiRes() - begin) / 6.0);
            begin = juce::Time::getMillisecondCounterHiRes();
            for (int frame = 0; frame < 12; ++frame) draw (true, round * 12 + frame + 1);
            cached.push_back ((juce::Time::getMillisecondCounterHiRes() - begin) / 12.0);
        }
        std::sort (full.begin(), full.end());
        std::sort (cached.begin(), cached.end());
        const auto before = full[3], after = cached[3];
        std::cout << "scale=" << scale << " full_ms=" << before
                  << " cached_ms=" << after << std::endl;
        // Measured in the same run, so a slow machine slows both: the needle frame must stay a
        // small part of a full redraw (about a tenth when measured; a third still passes).
        KIRIN_HYBRID_VU_REQUIRE (after <= before * 0.35);
    }
}


void verifyVuAnimatorLifecycle()
{
    juce::Component parent;
    juce::Component owner;
    parent.addAndMakeVisible (owner);
    parent.setVisible (true);
    int repaints = 0;
    hybrid_vu::NeedleAnimator animator (owner, [&repaints] { ++repaints; });
    animator.setVisible (true);
    KIRIN_HYBRID_VU_REQUIRE (animator.isVisible());
    animator.setTarget (0, 1.0);
    for (int frame = 0; frame < 120; ++frame) animator.tickForTest (1.0 / 60.0);
    const auto settled = repaints;
    for (int frame = 0; frame < 30; ++frame) animator.tickForTest (1.0 / 60.0);
    KIRIN_HYBRID_VU_REQUIRE (settled > 0 && repaints == settled);
    animator.setTarget (1, 0.5);
    parent.setVisible (false);
    KIRIN_HYBRID_VU_REQUIRE (! animator.isVisible());
    parent.setVisible (true);
    animator.setVisible (true);
    KIRIN_HYBRID_VU_REQUIRE (animator.isVisible());
    animator.setVisible (false);
    KIRIN_HYBRID_VU_REQUIRE (! animator.isVisible());
    for (int frame = 0; frame < 30; ++frame) animator.tickForTest (1.0 / 60.0);
    KIRIN_HYBRID_VU_REQUIRE (repaints == settled);
    observatory::View view (observatory::Role::pre);
    view.setSize (900, 600);
    view.setMeterSnapshot (meterFixture(), true);
    view.setWatchDisplay (watchFixture(), true);
    view.setManualHybridVuVisible (true);
    const auto first = render (view);
    KIRIN_HYBRID_VU_REQUIRE (differentPixels (first, render (view)) == 0);
    const auto capture = view.createCaptureImage (900, 600, false, "fixed", "test");
    const auto repeated = view.createCaptureImage (900, 600, false, "fixed", "test");
    KIRIN_HYBRID_VU_REQUIRE (differentPixels (capture, repeated) == 0);
}


// This diagnostic requires a real interactive desktop and runs in its own process.
void verifyVuVBlankInteractive()
{
    juce::Component owner;
    owner.setSize (300, 200);
    owner.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    owner.setVisible (true);
    int frames = 0, repaints = 0, settled = -1, idle = -1, hidden = -1;
    hybrid_vu::NeedleAnimator animator (owner, [&repaints] { ++repaints; });
    animator.setVisible (true);
    juce::VBlankAttachment probe (&owner, [&frames] { ++frames; });
    juce::Timer::callAfterDelay (100, [&] { animator.setTarget (0, 1.0); });
    juce::Timer::callAfterDelay (500, [&] { settled = repaints; });
    juce::Timer::callAfterDelay (900, [&]
    {
        idle = repaints;
        animator.setTarget (0, 0.0);
        animator.setVisible (false);
        owner.setVisible (false);
    });
    juce::Timer::callAfterDelay (1200, [&]
    {
        hidden = repaints;
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    std::cout << "vblank_frames=" << frames << " settled_repaints=" << settled
              << " idle_repaints=" << idle << " hidden_repaints=" << hidden << std::endl;
    KIRIN_HYBRID_VU_REQUIRE (frames > 0 && settled > 0 && idle == settled && hidden == idle);
    KIRIN_HYBRID_VU_REQUIRE (! animator.isVisible());
}

}
