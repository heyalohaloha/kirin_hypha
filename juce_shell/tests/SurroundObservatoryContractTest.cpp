#include "SurroundObservatoryContractTest.h"

#include "../src/HyphaObservatoryView.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
void requireSurround (bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "Surround Observatory contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_SURROUND_REQUIRE(expression) \
    requireSurround ((expression), #expression, __LINE__)

KirinObservatoryFrame surroundFrame()
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.lra_state = KIRIN_LRA_READY;
    frame.meter.state = KIRIN_METER_SESSION_ACTIVE;
    frame.meter.sample_rate = 48'000;
    frame.meter.channels = 6;
    frame.meter.lufs_m = -14.0;
    frame.meter.lufs_s = -15.0;
    frame.meter.lufs_i = -16.0;
    frame.meter.lra = 5.0;
    frame.meter.true_peak = -1.0;
    frame.meter.max_true_peak = -0.5;
    frame.meter.plr = 12.0;
    constexpr std::array<uint8_t, 6> roles {
        KIRIN_CHANNEL_ROLE_LEFT, KIRIN_CHANNEL_ROLE_RIGHT, KIRIN_CHANNEL_ROLE_CENTRE,
        KIRIN_CHANNEL_ROLE_LFE, KIRIN_CHANNEL_ROLE_LEFT_SURROUND,
        KIRIN_CHANNEL_ROLE_RIGHT_SURROUND
    };
    const auto unavailable = std::numeric_limits<double>::quiet_NaN();
    for (size_t channel = 0; channel < KIRIN_MAX_CHANNELS; ++channel)
    {
        frame.meter.sample_peak_dbfs[channel] = unavailable;
        frame.meter.sample_peak_hold_dbfs[channel] = unavailable;
        frame.meter.channel_true_peak_dbtp[channel] = unavailable;
        frame.meter.channel_max_true_peak_dbtp[channel] = unavailable;
        frame.meter.channel_vu_dbfs[channel] = unavailable;
        frame.meter.channel_instant_true_peak_dbtp[channel] = unavailable;
    }
    for (size_t channel = 0; channel < roles.size(); ++channel)
    {
        frame.meter.channel_positions[channel] = roles[channel];
        frame.meter.sample_peak_dbfs[channel] = -4.0 - static_cast<double> (channel);
        frame.meter.sample_peak_hold_dbfs[channel] = -2.0 - static_cast<double> (channel);
        frame.meter.channel_true_peak_dbtp[channel] = -3.0 - static_cast<double> (channel);
        frame.meter.channel_max_true_peak_dbtp[channel] = -1.0 - static_cast<double> (channel);
        frame.meter.clip_events[channel] = channel == 3u ? 1u : 0u;
    }
    frame.meter.balance_state = KIRIN_BALANCE_UNAVAILABLE;
    frame.meter.balance_db = unavailable;
    frame.meter.correlation = unavailable;
    return frame;
}

juce::Image renderSurround (observatory::View& view)
{
    juce::Image image (juce::Image::RGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& a, const juce::Image& b)
{
    int changed = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) ++changed;
    return changed;
}
} // namespace

void verifySurroundObservatoryContract()
{
    const auto frame = surroundFrame();
    observatory::View surround (observatory::Role::post);
    surround.setSize (600, 400);
    surround.setObservatoryFrame (frame, true);
    surround.setSurroundMeasurementOnly (true);
    KIRIN_SURROUND_REQUIRE (surround.surroundMeasurementOnlyForTest());
    KIRIN_SURROUND_REQUIRE (! surround.frequencyControlVisibleForTest());
    KIRIN_SURROUND_REQUIRE (! surround.spaceControlVisibleForTest());
    KIRIN_SURROUND_REQUIRE (! surround.referenceControlVisibleForTest());
    surround.setDomain (observatory::Domain::frequency);
    KIRIN_SURROUND_REQUIRE (surround.domain() == observatory::Domain::level);
    surround.setDomain (observatory::Domain::reference);
    KIRIN_SURROUND_REQUIRE (surround.domain() == observatory::Domain::level);
    surround.setDomain (observatory::Domain::time);
    KIRIN_SURROUND_REQUIRE (surround.domain() == observatory::Domain::time);
    KIRIN_SURROUND_REQUIRE (! surround.bodyOwnedByExternalAnalysis());
    surround.setManualHybridVuVisible (true);
    KIRIN_SURROUND_REQUIRE (! surround.hybridVuVisible());
    surround.setDomain (observatory::Domain::level);

    auto stereoFrame = frame;
    stereoFrame.meter.channels = 2;
    observatory::View stereo (observatory::Role::post);
    stereo.setSize (600, 400);
    stereo.setObservatoryFrame (stereoFrame, true);
    KIRIN_SURROUND_REQUIRE (
        differentPixels (renderSurround (surround), renderSurround (stereo)) > 100);
}
} // namespace hypha::tests
