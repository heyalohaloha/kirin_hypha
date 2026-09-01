#include "HyphaObservatoryView.h"

#include <cmath>

namespace hypha::observatory
{
void View::setMeterSnapshot (const KirinMeterSession& value, bool available)
{
    observatoryFrame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    observatoryFrame.meter = value;
    observatoryFrame.signal_state = value.state == KIRIN_METER_SESSION_ACTIVE
        ? KIRIN_SIGNAL_STATE_ACTIVE : KIRIN_SIGNAL_STATE_INACTIVE;
    const auto elapsed = value.sample_rate > 0
        ? static_cast<double> (value.active_frames) / static_cast<double> (value.sample_rate) : 0.0;
    observatoryFrame.lra_elapsed_seconds = elapsed;
    observatoryFrame.lra_state = value.state == KIRIN_METER_SESSION_EMPTY
        ? KIRIN_LRA_UNAVAILABLE
        : elapsed < 60.0 ? KIRIN_LRA_WARMING
                         : std::isfinite (value.lra) ? KIRIN_LRA_READY : KIRIN_LRA_UNAVAILABLE;
    frameAvailable = available;
    repaint (bodyArea);
}

void View::setDeltaSnapshot (const KirinDelta& value, bool available)
{
    observatoryFrame.delta = value;
    observatoryFrame.delta_available = available ? 1u : 0u;
    repaint (bodyArea);
}

void View::setObservatoryFrame (const KirinObservatoryFrame& value, bool available)
{
    observatoryFrame = value;
    frameAvailable = available && value.version == KIRIN_OBSERVATORY_FRAME_VERSION;
    repaint (bodyArea);
}

bool View::currentFactsAvailable() const noexcept
{
    return frameAvailable
        && observatoryFrame.signal_state == KIRIN_SIGNAL_STATE_ACTIVE
        && observatoryFrame.meter.state == KIRIN_METER_SESSION_ACTIVE;
}

bool View::cumulativeFactsAvailable() const noexcept
{
    return frameAvailable && observatoryFrame.meter.state != KIRIN_METER_SESSION_EMPTY;
}

bool View::deltaFactsAvailable() const noexcept
{
    return currentFactsAvailable()
        && observatoryFrame.delta.mode == KIRIN_DELTA_MODE_ACTIVE
        && observatoryFrame.delta_available != 0u;
}

observatory_world::State View::worldState() const noexcept
{
    observatory_world::State state;
    state.role = role;
    state.domain = selectedDomain;
    state.density = currentPreset().density;
    state.active = currentFactsAvailable();
    state.connection = connectionState;
    state.guidePresent = guidePresence() == GuidePresence::present;
    state.capture = captureFrame;
    state.energy = state.active && std::isfinite (observatoryFrame.meter.lufs_m)
        ? static_cast<float> (juce::jlimit (
            0.0, 1.0, (observatoryFrame.meter.lufs_m + 48.0) / 48.0)) : 0.0f;
    state.direction = state.active && std::isfinite (observatoryFrame.meter.balance_db)
        ? static_cast<float> (juce::jlimit (
            -1.0, 1.0, observatoryFrame.meter.balance_db / 12.0)) : 0.0f;
    return state;
}
}
