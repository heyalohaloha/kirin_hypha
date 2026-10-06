#include "HyphaObservatoryView.h"
#include "HyphaObservationEquality.h"

#include <cmath>

namespace hypha::observatory
{
namespace
{
bool sameMeterSession (const KirinMeterSession& a, const KirinMeterSession& b) noexcept
{
    return a.generation == b.generation && a.measurement_epoch == b.measurement_epoch
        && a.sample_rate == b.sample_rate && a.channels == b.channels && a.layout_id == b.layout_id
        && observation_equality::field (a.channel_positions, b.channel_positions);
}
}

// One publication owns LEVEL's current values, history and chain. A non-blocking acquisition
// miss is not an empty publication. Keep the entire last packet, including an inspected HOLD.
// Only a completed replacement or an observed lifecycle boundary may retire that packet.
void View::setLevelObservation (const KirinLevelSnapshot* packet,
                                std::vector<KirinMeterHistoryEntry> entries,
                                const KirinChainPoint* points,
                                const KirinObservatoryFrame* fallback)
{
    if (packet != nullptr && packet->version == KIRIN_LEVEL_SNAPSHOT_VERSION
        && packet->frame.version == KIRIN_OBSERVATORY_FRAME_VERSION
        && packet->history_count == entries.size())
    {
        const bool changedSession = ! frameAvailable
            || ! sameMeterSession (observatoryFrame.meter, packet->frame.meter)
            || packet->frame.meter.state == KIRIN_METER_SESSION_EMPTY;
        const bool changedState = observatoryFrame.signal_state != packet->frame.signal_state
            || observatoryFrame.meter.state != packet->frame.meter.state;
        if (changedSession)
        {
            if (levelInspection.held()) resumeLevelHistory();
            setHistory ({});
        }
        if (changedSession || changedState
            || observatoryFrame.comparison_identity != packet->frame.comparison_identity)
            clearChainObservation(); // A complete POST packet may still have a busy PRE-chain writer.
        setObservatoryFrame (packet->frame, true);
        setHistory (packet->frame.meter.state == KIRIN_METER_SESSION_EMPTY
                        ? std::vector<KirinMeterHistoryEntry> {} : std::move (entries));
        if (packet->chain_updated != 0u && packet->frame.meter.state != KIRIN_METER_SESSION_EMPTY)
            setChainObservation (packet->chain, points);
        return;
    }
    if (fallback == nullptr || fallback->version != KIRIN_OBSERVATORY_FRAME_VERSION)
        return;
    const bool changedSession = ! frameAvailable
        || ! sameMeterSession (observatoryFrame.meter, fallback->meter)
        || fallback->meter.state == KIRIN_METER_SESSION_EMPTY;
    const bool changedState = observatoryFrame.signal_state != fallback->signal_state
        || observatoryFrame.meter.state != fallback->meter.state;
    // Comparison generation also advances on transient PRE availability. Only its producer
    // identity is a binding boundary; an unrelated delta status must not erase POST history.
    const bool changedBinding = observatoryFrame.comparison_identity != fallback->comparison_identity;
    if (! changedSession)
    {
        if (! changedState && ! changedBinding) return;
        // Availability is not a new observation. Keep the accepted numeric/history cutoff,
        // including absolute HOLD, while exposing the current pause/bypass fact accurately.
        auto retained = observatoryFrame;
        retained.signal_state = fallback->signal_state;
        retained.meter.state = fallback->meter.state;
        retained.comparison_state = fallback->comparison_state;
        retained.comparison_reason = fallback->comparison_reason;
        retained.comparison_generation = fallback->comparison_generation;
        retained.comparison_identity = fallback->comparison_identity;
        retained.delta_available = 0u;
        clearChainObservation();
        setObservatoryFrame (retained, true);
        return;
    }
    // Only an empty/reset/format identity boundary retires absolute history. Never attach
    // the old packet's history or HOLD to a standalone publication from another session.
    if (levelInspection.held()) resumeLevelHistory();
    auto replacement = *fallback;
    replacement.delta_available = 0u;
    setObservatoryFrame (replacement, true);
    setHistory ({});
    clearChainObservation();
}

std::uint64_t View::levelChainRevision (bool latestOnly) const noexcept
{
    const auto version = latestOnly ? KIRIN_CHAIN_VERSION_LATEST : KIRIN_CHAIN_VERSION;
    return chainSnapshotAvailable && chainSnapshot.version == version ? chainSnapshot.revision : 0u;
}

void View::setChainObservation (const KirinChainSnapshot& value,
                                const KirinChainPoint* points)
{
    const auto valid = ((value.version == KIRIN_CHAIN_VERSION
                             && value.count <= KIRIN_CHAIN_CAPACITY)
                        || (value.version == KIRIN_CHAIN_VERSION_LATEST
                             && value.count <= 1u))
                    && (value.count == 0u || points != nullptr);
    if (! valid)
    {
        clearChainObservation();
        return;
    }
    if (levelInspection.held() && levelInspection.chainSnapshot.binding != 0u
        && (levelInspection.chainSnapshot.binding != value.binding
            || value.status == KIRIN_CHAIN_SUPPRESSED))
        resumeLevelHistory();
    if (chainSnapshotAvailable && chainSnapshot.revision == value.revision
        && chainSnapshot.version == value.version)
        return;
    chainSnapshot = value;
    if (value.count == 0u)
        chainPoints.clear();
    else
        chainPoints.assign (points, points + value.count);
    chainSnapshotAvailable = true;
    repaint (bodyArea);
}

void View::clearChainObservation()
{
    if (! chainSnapshotAvailable && chainPoints.empty())
        return;
    if (levelInspection.held() && levelInspection.chainSnapshot.binding != 0u)
        resumeLevelHistory();
    chainSnapshot = {};
    chainPoints.clear();
    chainSnapshotAvailable = false;
    repaint (bodyArea);
}

void View::setMeterSnapshot (const KirinMeterSession& value, bool available)
{
    const auto previous = observatoryFrame;
    const auto previousState = footerStatusText();
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
    {
        repaint (bodyArea);
        if (previousState != footerStatusText()) repaint (sessionArea);
    }
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
    const auto previousState = footerStatusText();
    const bool comparisonChanged = target() == ObservationTarget::delta
        && (observatoryFrame.comparison_identity != value.comparison_identity
            || observatoryFrame.comparison_generation != value.comparison_generation);
    const bool historyIdentityChanged = observatoryFrame.meter.generation != value.meter.generation
        || observatoryFrame.meter.measurement_epoch != value.meter.measurement_epoch
        || observatoryFrame.meter.state != value.meter.state;
    observatoryFrame = value;
    frameAvailable = true;
    if (chainSnapshotAvailable && ! chainPoints.empty()
        && (chainPoints.back().post_epoch != value.meter.measurement_epoch
            || chainPoints.back().post_generation != value.meter.generation))
        clearChainObservation();
    if (levelInspection.held() && (comparisonChanged || ! levelInspection.matches (value.meter)))
        resumeLevelHistory();
    else if (historyIdentityChanged) updateLevelHistoryControls();
    repaint (bodyArea);
    if (previousState != footerStatusText()) repaint (sessionArea);
}

void View::setRecordDisplay (const KirinRecordDisplay& value, bool available)
{
    if (recordDisplayAvailable == available
        && (! available || observation_equality::same (recordDisplay, value)))
        return;
    const auto previouslyShowing = recordDisplayShowing();
    recordDisplay = value;
    recordDisplayAvailable = available;
    const auto showing = recordDisplayShowing();
    if (previouslyShowing != showing && onRecordBodyOwnershipChange)
        onRecordBodyOwnershipChange (showing);
    if (previouslyShowing || showing)
    {
        ++recordBodyInvalidations;
        layoutLevelHistoryControls();
        repaint (bodyArea);
    }
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
