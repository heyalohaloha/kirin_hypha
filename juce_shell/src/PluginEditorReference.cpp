#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>

namespace
{
hypha::reference_ui::Readiness referenceReadiness (
    hypha::reference_audition::RuntimeState state) noexcept
{
    using Input = hypha::reference_audition::RuntimeState;
    using Output = hypha::reference_ui::Readiness;
    switch (state)
    {
        case Input::disconnected: return Output::disconnected;
        case Input::waiting:      return Output::waiting;
        case Input::verifying:    return Output::verifying;
        case Input::ready:        return Output::ready;
        case Input::rejected:     return Output::rejected;
    }
    return Output::disconnected;
}

juce::String sourceLabel (const juce::String& kind)
{
    if (kind == "work_version") return "WORK VERSION";
    if (kind == "catalog") return "CATALOG";
    return "KIRIN OS";
}

juce::String rejectedStatus (const juce::String& code)
{
    if (code == "source_changed") return "SOURCE CHANGED / PREPARE AGAIN IN KIRIN OS";
    if (code == "source_open_failed" || code == "source_decode_failed")
        return "SOURCE COULD NOT BE OPENED";
    return "PREPARE AGAIN IN KIRIN OS";
}

hypha::reference_ui::BlindPhase referenceBlindPhase (
    hypha::reference_audition::BlindPhase phase,
    bool available) noexcept
{
    using Input = hypha::reference_audition::BlindPhase;
    using Output = hypha::reference_ui::BlindPhase;
    switch (phase)
    {
        case Input::active:      return Output::active;
        case Input::revealed:    return Output::revealed;
        case Input::invalidated: return available ? Output::invalidated : Output::unavailable;
        case Input::inactive:    return available ? Output::available : Output::unavailable;
    }
    return Output::unavailable;
}
}

void KirinHyphaEditor::configureReferenceAudition()
{
    referenceView.onSelectA = [this] { processorRef.selectReferenceA(); };
    referenceView.onSelectB = [this]
    {
        const auto& state = referenceView.state();
        if (! processorRef.selectReferenceB (state.aIntegratedLoudness,
                                              state.aMaximumTruePeakDbtp))
            showToast ("Reference B is not ready");
    };
    referenceView.onStartBlind = [this]
    {
        const auto& state = referenceView.state();
        if (! processorRef.startReferenceBlind (state.aIntegratedLoudness,
                                                 state.aMaximumTruePeakDbtp))
            showToast ("Blind Compare could not start");
    };
    referenceView.onSelectBlindStimulus = [this] (int stimulus)
    {
        if (! processorRef.selectReferenceBlindStimulus (stimulus))
            showToast ("Blind source could not be confirmed");
    };
    referenceView.onRevealBlind = [this]
    {
        if (! processorRef.revealReferenceBlind())
            showToast ("Blind Compare could not be revealed");
    };
    referenceView.onEndBlind = [this] { processorRef.endReferenceBlind(); };
    scaleRoot.addChildComponent (referenceView);
}

void KirinHyphaEditor::layoutReferenceAudition (juce::Rectangle<int> body)
{
    referenceView.setBounds (body);
    referenceView.setVisible (observatoryDomain == hypha::observatory::Domain::reference);
    referenceView.toFront (false);
}

void KirinHyphaEditor::refreshReferenceAudition (const KirinObservatoryFrame& frame,
                                                  bool frameAvailable)
{
    auto runtime = processorRef.referenceAuditionSnapshot();
    const bool callbackLive = processorRef.heartbeatLive();
    const bool blindRunning = runtime.blindPhase
            == hypha::reference_audition::BlindPhase::active
        || runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed;
    if (blindRunning && (! callbackLive || ! runtime.transportPlaying
                         || ! runtime.transportPositionValid))
    {
        processorRef.endReferenceBlind();
        runtime = processorRef.referenceAuditionSnapshot();
    }
    hypha::reference_ui::State state;
    state.readiness = referenceReadiness (runtime.state);
    state.title = runtime.title;
    state.sourceLabel = sourceLabel (runtime.sourceKind);
    state.alignmentLabel = runtime.alignmentMode
            == hypha::reference_audition::AlignmentMode::sampleLock
        ? "PROJECT TIMELINE" : "REFERENCE CUE";
    state.bSelected = runtime.bSelected;
    state.gainLimited = runtime.gainLimited;
    state.activeBlindStimulus = runtime.activeBlindStimulus;
    state.pendingBlindStimulus = runtime.pendingBlindStimulus;
    state.blindReveal = runtime.blindReveal;

    const bool liveA = frameAvailable
        && frame.meter.state != KIRIN_METER_SESSION_EMPTY
        && std::isfinite (frame.meter.lufs_i)
        && std::isfinite (frame.meter.max_true_peak);
    const bool frozenBlindA = runtime.blindPhase == hypha::reference_audition::BlindPhase::active
        || runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed;
    state.aAvailable = runtime.bSelected || frozenBlindA || liveA;
    state.aIntegratedLoudness = runtime.bSelected || frozenBlindA
        ? runtime.aIntegratedLoudness
        : liveA ? frame.meter.lufs_i : hypha::reference_ui::unavailableValue();
    state.aMaximumTruePeakDbtp = runtime.bSelected || frozenBlindA
        ? runtime.aMaximumTruePeakDbtp
        : liveA ? frame.meter.max_true_peak : hypha::reference_ui::unavailableValue();
    const bool blindAvailable = runtime.state == hypha::reference_audition::RuntimeState::ready
        && callbackLive && runtime.transportPlaying && runtime.transportPositionValid
        && runtime.auditionBuffered
        && state.aAvailable && std::isfinite (state.aIntegratedLoudness)
        && std::isfinite (state.aMaximumTruePeakDbtp);
    state.blindPhase = referenceBlindPhase (runtime.blindPhase, blindAvailable);

    if (runtime.bSelected
        || runtime.blindPhase != hypha::reference_audition::BlindPhase::inactive)
    {
        state.adjustedBIntegratedLoudness = runtime.adjustedBIntegratedLoudness;
        state.adjustedBMaximumTruePeakDbtp = runtime.adjustedBMaximumTruePeakDbtp;
        state.loudnessDeltaBMinusA = runtime.loudnessDeltaBMinusA;
        state.truePeakDeltaBMinusA = runtime.truePeakDeltaBMinusA;
        state.appliedGainDb = runtime.appliedGainDb;
    }

    using Runtime = hypha::reference_audition::RuntimeState;
    if (runtime.blindPhase == hypha::reference_audition::BlindPhase::invalidated)
        state.status = "BLIND ENDED / CONDITION CHANGED";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active)
        state.status = "BLIND / SOURCE IDENTITY HIDDEN";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed)
        state.status = "BLIND / REVEALED";
    else if (runtime.bSelected)
        state.status = "B AUDITION / PRE DELTA PAUSED";
    else if (runtime.state == Runtime::ready)
        state.status = liveA ? "READY / B FOLLOWS A" : "PLAY A TO MEASURE";
    else if (runtime.state == Runtime::verifying)
        state.status = "VERIFYING SOURCE";
    else if (runtime.state == Runtime::rejected)
        state.status = rejectedStatus (runtime.rejectionCode);
    else if (runtime.state == Runtime::waiting)
        state.status = "OPEN IN HYPHA FROM KIRIN OS";
    else
        state.status = "CONNECT TO A KIRIN OS WORK";
    referenceView.setState (std::move (state));
}

#endif
