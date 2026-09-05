#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <iterator>

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
    if (kind == "catalog_track" || kind == "catalog") return "CATALOG";
    return "KIRIN OS";
}

juce::String rejectedStatus (const juce::String& code)
{
    if (code == "source_changed") return "SOURCE CHANGED / PREPARE AGAIN IN KIRIN OS";
    if (code == "source_open_failed" || code == "source_decode_failed"
        || code == "reference_source_open_failed"
        || code == "reference_source_decode_failed")
        return "SOURCE COULD NOT BE OPENED";
    if (code == "reference_source_changed") return "SOURCE CHANGED / VERIFY IN KIRIN OS";
    if (code == "reference_source_audio_mismatch")
        return "SOURCE FORMAT CHANGED / VERIFY IN KIRIN OS";
    return "PREPARE AGAIN IN KIRIN OS";
}

std::vector<hypha::reference_ui::SelectionOption> selectionOptions (
    const std::vector<hypha::reference_audition::RuntimeSelectionOption>& input)
{
    std::vector<hypha::reference_ui::SelectionOption> output;
    output.reserve (input.size());
    for (const auto& item : input) output.push_back ({ item.id, item.label });
    return output;
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
        case Input::invalidated: return Output::invalidated;
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
    referenceView.onSelectPreset = [this] (const juce::String& id)
    {
        if (! processorRef.selectReferencePreset (id))
            showToast ("Preset selection was not changed");
    };
    referenceView.onSelectCheck = [this] (const juce::String& id)
    {
        if (! processorRef.selectReferenceCheck (id))
            showToast ("Check selection was not changed");
    };
    referenceView.onSelectCandidate = [this] (const juce::String& id)
    {
        if (! processorRef.selectReferenceCandidate (id))
            showToast ("Reference selection was not changed");
    };
    referenceView.onSelectCue = [this] (const juce::String& id)
    {
        if (! processorRef.selectReferenceCue (id))
            showToast ("Cue selection was not changed");
    };
    referenceView.onAction = [this]
    {
        const auto& state = referenceView.state();
        const bool accepted = state.sampleRateApprovalRequired
            ? processorRef.approveReferenceSampleRateConversion()
            : state.blindLowerAApprovalRequired
                ? processorRef.approveReferenceBlindLowerA (
                    state.aIntegratedLoudness, state.aMaximumTruePeakDbtp)
                : processorRef.requestReferenceRecovery();
        if (! accepted) showToast ("Kirin OS could not receive the request");
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
    referenceView.onAnswerBlind = [this] (int stimulus)
    {
        if (! processorRef.answerReferenceBlind (stimulus))
            showToast ("Listen to both sources before choosing");
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
    bool connected = false;
   #if KIRIN_HYPHA_GUIDE_TRANSPORT
    connected = processorRef.connectedWorkReference().valid();
   #endif
    state.osAccess = hypha::os_access::classify (
        processorRef.licenseIsOs(), connected,
        runtime.state == hypha::reference_audition::RuntimeState::ready);
    state.auditionBuffered = callbackLive && runtime.transportPlaying
        && runtime.transportPositionValid && runtime.auditionBuffered;
    state.title = runtime.title;
    state.sourceLabel = sourceLabel (runtime.sourceKind);
    state.alignmentLabel = runtime.alignmentMode
            == hypha::reference_audition::AlignmentMode::sampleLock
        ? "PROJECT TIMELINE" : "REFERENCE CUE";
    state.bSelected = runtime.bSelected;
    state.gainLimited = runtime.gainLimited;
    state.comparisonFallbackOriginal = runtime.comparisonFallbackOriginal;
    state.activeBlindStimulus = runtime.activeBlindStimulus;
    state.pendingBlindStimulus = runtime.pendingBlindStimulus;
    state.answeredBlindStimulus = runtime.answeredBlindStimulus;
    state.blindStimulusOneHeard = runtime.blindStimulusOneHeard;
    state.blindStimulusTwoHeard = runtime.blindStimulusTwoHeard;
    state.blindLowerAApprovalRequired = runtime.blindLowerAApprovalRequired;
    state.blindRequiredAAttenuationDb = runtime.blindRequiredAAttenuationDb;
    state.blindReveal = runtime.blindReveal;

    const bool liveA = frameAvailable
        && frame.meter.state != KIRIN_METER_SESSION_EMPTY
        && std::isfinite (frame.meter.lufs_i)
        && std::isfinite (frame.meter.max_true_peak);
    const bool frozenBlindA = runtime.blindPhase == hypha::reference_audition::BlindPhase::active
        || runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed;
    state.aAvailable = runtime.bSelected || frozenBlindA
        || (callbackLive && runtime.transportPlaying && runtime.transportPositionValid);
    state.aIntegratedLoudness = runtime.bSelected || frozenBlindA
        ? runtime.aIntegratedLoudness
        : liveA ? frame.meter.lufs_i : hypha::reference_ui::unavailableValue();
    state.aMaximumTruePeakDbtp = runtime.bSelected || frozenBlindA
        ? runtime.aMaximumTruePeakDbtp
        : liveA ? frame.meter.max_true_peak : hypha::reference_ui::unavailableValue();
    const bool blindAvailable = callbackLive && runtime.blindEligible
        && hypha::reference_ui::canSelectB (state);
    state.blindPhase = referenceBlindPhase (runtime.blindPhase, blindAvailable);
    state.presetId = runtime.presetId;
    state.checkId = runtime.checkId;
    state.candidateId = runtime.candidateId;
    state.cueId = runtime.cueId;
    state.presetName = runtime.presetName;
    state.checkLabel = runtime.checkLabel;
    state.candidateName = runtime.candidateName;
    state.cueLabel = runtime.cueLabel;
    state.comparisonMode = runtime.comparisonMode;
    state.presentationLayout = runtime.presentationLayout;
    state.viewBindings = runtime.viewBindings;
    state.presets = selectionOptions (runtime.presets);
    state.checks = selectionOptions (runtime.checks);
    state.candidates = selectionOptions (runtime.candidates);
    state.cues = selectionOptions (runtime.cues);
    state.detailedMeasurement = runtime.detailedMeasurement;
    state.profiles = runtime.profiles;
    state.sampleRateApprovalRequired = runtime.sampleRateApprovalRequired;
    state.sourceSampleRateHz = runtime.sourceSampleRateHz;
    state.hostSampleRateHz = runtime.hostSampleRateHz;
    if (observatoryDomain == hypha::observatory::Domain::reference)
    {
        KirinSpectrumView spectrum {};
        if (processorRef.pollSpectrum (spectrum) && spectrum.post_has_data != 0)
        {
            state.liveSpectrumDbfs.assign (
                std::begin (spectrum.post_dbfs), std::end (spectrum.post_dbfs));
            state.liveSpectrumMinimumHz = spectrum.min_hz;
            state.liveSpectrumMaximumHz = spectrum.max_hz;
        }
    }

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
    using Access = hypha::os_access::State;
    if (state.osAccess == Access::unowned)
        state.status = "REF REQUIRES KIRIN OS";
    else if (state.osAccess == Access::ownedDisconnected)
        state.status = "WAITING FOR KIRIN OS REFERENCE";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::invalidated)
        state.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND STOPPED / A HELD -"
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURN A EXPLICITLY"
            : "BLIND ENDED / A LIVE / CONDITION CHANGED";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active)
        state.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND / A LOWERED "
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURNS ON END"
            : "BLIND / SOURCE IDENTITY HIDDEN";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed)
        state.status = "BLIND / REVEALED";
    else if (runtime.bSelected)
        state.status = "B AUDITION / PRE DELTA PAUSED";
    else if (runtime.state == Runtime::ready)
        state.status = state.auditionBuffered && liveA
            ? "READY / B FOLLOWS A" : "PLAY A TO ENABLE B";
    else if (runtime.state == Runtime::verifying)
        state.status = "VERIFYING SOURCE";
    else if (runtime.state == Runtime::rejected)
        state.status = rejectedStatus (runtime.rejectionCode);
    else if (runtime.state == Runtime::waiting)
        state.status = "WAITING FOR KIRIN OS REFERENCE";
    else
        state.status = "CONNECT TO A KIRIN OS WORK";
    if (runtime.recoveryStatus == "pending")
    {
        state.status = "OPENING REFERENCE IN KIRIN OS";
        state.actionText.clear();
    }
    else if (runtime.recoveryStatus == "exact_opened")
    {
        state.status = "KIRIN OS OPENED THE REFERENCE LOCATION";
        state.actionText.clear();
    }
    else if (runtime.recoveryStatus == "safe_fallback_opened")
    {
        state.status = "KIRIN OS OPENED SAFE REFERENCE SETTINGS";
        state.actionText.clear();
    }
    else if (runtime.recoveryStatus == "rejected")
    {
        state.status = "REFERENCE ITEM IS NO LONGER AVAILABLE";
        state.actionText = "TRY KIRIN OS AGAIN";
    }
    else if (runtime.recoveryStatus == "timed_out")
    {
        state.status = "KIRIN OS DID NOT RESPOND / A REMAINS LIVE";
        state.actionText = "TRY KIRIN OS AGAIN";
    }
    else if (state.sampleRateApprovalRequired)
    {
        const auto sourceRate = juce::String (state.sourceSampleRateHz / 1000.0, 1);
        const auto hostRate = juce::String (state.hostSampleRateHz / 1000.0, 1);
        state.status = "SAMPLE RATE " + sourceRate + " TO " + hostRate
                     + " kHz / A REMAINS LIVE";
        state.actionText = "USE " + sourceRate + " TO " + hostRate + " kHz";
    }
    else if (state.blindLowerAApprovalRequired)
    {
        const auto attenuation = juce::String (state.blindRequiredAAttenuationDb, 1);
        state.status = "BLIND NEEDS HEADROOM / A RETURNS +" + attenuation + " dB ON END";
        state.actionText = "LOWER A " + attenuation + " dB & START";
    }
    else if (connected && (runtime.state == Runtime::rejected
                           || runtime.state == Runtime::waiting))
        state.actionText = "FIX IN KIRIN OS";
    else if (connected && runtime.state == Runtime::ready
             && ! runtime.measurementAvailable && ! runtime.viewBindings.empty())
        state.actionText = "PREPARE VISUALS";
    referenceView.setState (std::move (state));
}

#endif
