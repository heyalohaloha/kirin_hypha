#include "ObservatoryViewContractTest.h"

#include "../src/HyphaObservatoryView.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Observatory view contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_OBSERVATORY_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession activeMeter()
{
    KirinMeterSession meter {};
    meter.generation = 7;
    meter.active_frames = 48'000 * 272;
    meter.observed_frames = meter.active_frames;
    meter.sample_rate = 48'000;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -13.8;
    meter.lufs_s = -14.2;
    meter.lufs_i = -14.3;
    meter.lra = 6.4;
    meter.true_peak = -3.6;
    meter.max_true_peak = -1.2;
    meter.plr = 13.1;
    meter.channels = 2;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.sample_peak_dbfs[0] = -5.1;
    meter.sample_peak_dbfs[1] = -5.7;
    meter.sample_peak_hold_dbfs[0] = -2.8;
    meter.sample_peak_hold_dbfs[1] = -3.0;
    meter.channel_true_peak_dbtp[0] = -4.2;
    meter.channel_true_peak_dbtp[1] = -4.8;
    meter.channel_max_true_peak_dbtp[0] = -1.2;
    meter.channel_max_true_peak_dbtp[1] = -1.5;
    meter.clip_events[0] = 2;
    meter.clip_events[1] = 1;
    meter.balance_db = 0.7;
    meter.correlation = 0.82;
    return meter;
}

KirinDelta activeDelta()
{
    KirinDelta delta {};
    delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    delta.lufs = 1.1;
    delta.lufs_s = 0.8;
    delta.true_peak = 0.4;
    return delta;
}

std::vector<KirinMeterHistoryEntry> historyFixture()
{
    std::vector<KirinMeterHistoryEntry> result (90);
    for (size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 7;
        entry.run_id = index < 44 ? 1 : 2;
        entry.first_observed_frames = index * 4'800;
        entry.last_observed_frames = entry.first_observed_frames + 4'799;
        entry.first_timeline_endpoint_samples = static_cast<int64_t> (entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = static_cast<int64_t> (entry.last_observed_frames);
        entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        const auto wave = std::sin (static_cast<double> (index) * 0.24);
        entry.lufs_m.min = entry.lufs_m.max = entry.lufs_m.mean = -22.0 + 3.2 * wave;
        entry.lufs_s.min = entry.lufs_s.max = entry.lufs_s.mean = -20.0 + 2.0 * wave;
        entry.true_peak.min = entry.true_peak.max = entry.true_peak.mean = -5.0 + wave;
        entry.correlation.min = entry.correlation.max = entry.correlation.mean = 0.8;
    }
    return result;
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
    KIRIN_OBSERVATORY_REQUIRE (left.getBounds() == right.getBounds());
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}

void verifyRoleAtEverySize (observatory::Role role,
                            const KirinMeterSession& meter,
                            const std::vector<KirinMeterHistoryEntry>& history)
{
    for (const auto preset : observatory::sizePresets)
    {
        observatory::View view (role);
        view.setSize (preset.width, preset.height);
        view.setConnectionText (role == observatory::Role::post ? "PAIR DRUM" : "SOURCE PRE",
                                COL_LED_BLUE);
        view.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
        view.setMeterSnapshot (meter, true);
        view.setHistory (history);

        for (const auto domain : {
                 observatory::Domain::level, observatory::Domain::time,
                 observatory::Domain::frequency, observatory::Domain::space })
        {
            view.setDomain (domain);
            const auto body = view.bodyBounds();
            KIRIN_OBSERVATORY_REQUIRE (! body.isEmpty());
            KIRIN_OBSERVATORY_REQUIRE (view.getLocalBounds().contains (body));
            const auto image = render (view);
            KIRIN_OBSERVATORY_REQUIRE (image.getPixelAt (0, 0).getAlpha() != 0);
            KIRIN_OBSERVATORY_REQUIRE (image.getPixelAt (
                body.getCentreX(), body.getCentreY()).getAlpha() != 0);
            if (domain == observatory::Domain::level)
            {
                auto noClipsMeter = meter;
                noClipsMeter.clip_events[0] = 0;
                noClipsMeter.clip_events[1] = 0;
                view.setMeterSnapshot (noClipsMeter, true);
                KIRIN_OBSERVATORY_REQUIRE (differentPixels (image, render (view)) > 8);
                view.setMeterSnapshot (meter, true);
            }
        }
    }
}
}

void verifyObservatoryViewContract()
{
    const auto meter = activeMeter();
    const auto delta = activeDelta();
    const auto history = historyFixture();
    verifyRoleAtEverySize (observatory::Role::pre, meter, history);
    verifyRoleAtEverySize (observatory::Role::post, meter, history);

    observatory::View pre (observatory::Role::pre);
    pre.setTarget (observatory::ObservationTarget::delta);
    KIRIN_OBSERVATORY_REQUIRE (pre.target() == observatory::ObservationTarget::absolute);

    observatory::View post (observatory::Role::post);
    post.setSize (600, 400);
    post.setMeterSnapshot (meter, true);
    post.setDeltaSnapshot (delta, true);
    post.setConnectionText ("PAIR DRUM", COL_LED_BLUE);
    post.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
    post.setDomain (observatory::Domain::level);
    const auto absolute = render (post);
    auto noClipsMeter = meter;
    noClipsMeter.clip_events[0] = 0;
    noClipsMeter.clip_events[1] = 0;
    post.setMeterSnapshot (noClipsMeter, true);
    const auto noClips = render (post);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, noClips) > 20);
    post.setMeterSnapshot (meter, true);
    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_OBSERVATORY_RENDER_OUTPUT", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_OBSERVATORY_REQUIRE (output != nullptr);
        KIRIN_OBSERVATORY_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (absolute, *output));
    }
    post.setTarget (observatory::ObservationTarget::delta);
    const auto difference = render (post);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, difference) > 500);
    KIRIN_OBSERVATORY_REQUIRE (post.domain() == observatory::Domain::level);

    post.setDomain (observatory::Domain::time);
    post.setHistory (history);
    KIRIN_OBSERVATORY_REQUIRE (post.domain() == observatory::Domain::time);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::delta);
    post.setDomain (observatory::Domain::space);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::absolute);
    post.setTarget (observatory::ObservationTarget::delta);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::absolute);
    post.setDomain (observatory::Domain::level);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::delta);
    post.setDomain (observatory::Domain::time);
    const auto request = post.historyRequest();
    KIRIN_OBSERVATORY_REQUIRE (request.resolution == KIRIN_METER_HISTORY_10_HZ);
    KIRIN_OBSERVATORY_REQUIRE (request.maxEntries == 300);
    for (const auto dimensions : {
             std::pair { 1'200, 630 }, std::pair { 1'080, 1'080 },
             std::pair { 1'080, 1'350 } })
    {
        const auto capture = post.createCaptureImage (dimensions.first, dimensions.second);
        const auto body = post.captureBodyBounds (dimensions.first, dimensions.second);
        KIRIN_OBSERVATORY_REQUIRE (capture.getWidth() == dimensions.first);
        KIRIN_OBSERVATORY_REQUIRE (capture.getHeight() == dimensions.second);
        KIRIN_OBSERVATORY_REQUIRE (capture.getBounds().contains (body));
    }
}
}
