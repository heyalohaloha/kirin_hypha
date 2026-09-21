#include "HyphaObservatoryView.h"
#include "HyphaObservationEquality.h"

#include <cmath>

namespace hypha::observatory
{
void View::setMeterSnapshot (const KirinMeterSession& value, bool available)
{
    const auto previous = observatoryFrame;
    const auto previouslyAvailable = frameAvailable;
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
    const bool storedMono = monoSumHistory.append (value);
    if (previouslyAvailable != available || storedMono
        || ! observation_equality::same (previous, observatoryFrame))
        repaint (bodyArea);
}

void View::setDeltaSnapshot (const KirinDelta& value, bool available)
{
    if ((observatoryFrame.delta_available != 0u) == available
        && observation_equality::same (observatoryFrame.delta, value))
        return;
    observatoryFrame.delta = value;
    observatoryFrame.delta_available = available ? 1u : 0u;
    repaint (bodyArea);
}

void View::setObservatoryFrame (const KirinObservatoryFrame& value, bool available)
{
    if (! available || value.version != KIRIN_OBSERVATORY_FRAME_VERSION)
        return;
    // This is the entry point the plug-in uses. Anything a snapshot has to be stored into has to
    // be stored here, not only in setMeterSnapshot, which nothing but tests calls.
    const bool storedMono = monoSumHistory.append (value.meter);
    if (! storedMono && frameAvailable
        && observation_equality::same (observatoryFrame, value))
        return;
    observatoryFrame = value;
    frameAvailable = true;
    repaint (bodyArea);
}

void View::setRecordDisplay (const KirinRecordDisplay& value, bool available)
{
    const auto previouslyShowing = recordDisplayShowing();
    recordDisplay = value;
    recordDisplayAvailable = available;
    const auto showing = recordDisplayShowing();
    if (previouslyShowing != showing && onRecordBodyOwnershipChange)
        onRecordBodyOwnershipChange (showing);
    repaint (bodyArea);
}

bool View::recordDisplayShowing() const noexcept
{
    if (! recordDisplayAvailable)
        return false;
    return recordDisplay.phase == KIRIN_RECORD_DISPLAY_FINALIZING
        || recordDisplay.phase == KIRIN_RECORD_DISPLAY_RESULT_HOLD
        || recordDisplay.phase == KIRIN_RECORD_DISPLAY_UNAVAILABLE;
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
    state.jungle = jungleAppearance;
    state.energy = state.active && std::isfinite (observatoryFrame.meter.lufs_m)
        ? static_cast<float> (juce::jlimit (
            0.0, 1.0, (observatoryFrame.meter.lufs_m + 48.0) / 48.0)) : 0.0f;
    state.direction = state.active && std::isfinite (observatoryFrame.meter.balance_db)
        ? static_cast<float> (juce::jlimit (
            -1.0, 1.0, observatoryFrame.meter.balance_db / 12.0)) : 0.0f;
    return state;
}
}
