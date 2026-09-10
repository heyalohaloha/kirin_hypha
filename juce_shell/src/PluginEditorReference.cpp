#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY

#include <cmath>
#include <iterator>
#include "HyphaUpdateContract.h"

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
        case Input::starting:    return Output::starting;
        case Input::inactive:    return available ? Output::available : Output::unavailable;
    }
    return Output::unavailable;
}
}

void KirinHyphaEditor::configureReferenceAudition()
{
    referenceAccessView.onAbout = [this] { showReferenceInformationMenu(); };
    referenceAccessView.onRecheck = [this]
    {
        if (informationBlockedByBlind()) return;
        processorRef.refreshLicenseForUserAction();
        referenceAccessView.setOwned (processorRef.licenseIsOs());
        referenceAccessView.setRecheckUnconfirmed (! processorRef.licenseIsOs());
    };
    scaleRoot.addChildComponent (referenceAccessView);
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
            showToast ("Check Preset selection was not changed");
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
                : state.presetSelectionAction == "retry"
                    ? processorRef.retryReferencePresetSelection()
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
    const bool reference = observatoryDomain == hypha::observatory::Domain::reference;
    const bool access = hypha::reference_ui::needsAccessPanel (referenceView.state());
    referenceView.setBounds (body);
    referenceView.setVisible (reference && ! access);
    referenceView.toFront (false);
    referenceAccessView.setBounds (body);
    referenceAccessView.setVisible (reference && access);
    if (referenceAccessView.isVisible()) referenceAccessView.toFront (false);
    layoutLocalBlindProduct();
}

void KirinHyphaEditor::showReferenceInformationMenu()
{
    if (informationBlockedByBlind()) return;
    using Action = hypha::update_information::Action;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Kirin OS / Product, trial and purchase");
    menu.addItem (static_cast<int> (Action::osEnglish), "About Kirin OS (English)");
    menu.addItem (static_cast<int> (Action::osJapanese),
                  juce::String::fromUTF8 ("Kirin OSについて（日本語）"));
    menu.addSeparator();
    menu.addItem (static_cast<int> (Action::copyOsEnglish), "Copy official URL (English)");
    menu.addItem (static_cast<int> (Action::copyOsJapanese), "Copy official URL (Japanese)");
    const juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&referenceAccessView)
        .withDeletionCheck (*this).withMinimumWidth (360).withStandardItemHeight (28),
        [safe] (int selected) { if (safe != nullptr) safe->handleInformationMenu (selected); });
}

void KirinHyphaEditor::refreshReferenceAudition (const KirinObservatoryFrame& frame,
                                                  bool frameAvailable)
{
    auto runtime = processorRef.referenceAuditionSnapshot();
    const bool callbackLive = processorRef.heartbeatLive();
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
    state.presetId = runtime.presetSelectionTargetId.isNotEmpty()
        ? runtime.presetSelectionTargetId : runtime.presetId;
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
    state.presetSelectionAction = runtime.presetSelectionAction;
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
    if (runtime.blindPhase == hypha::reference_audition::BlindPhase::invalidated)
        state.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND STOPPED / A HELD -"
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURN A EXPLICITLY"
            : "BLIND STOPPED / RETURN A EXPLICITLY";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::starting)
        state.status = "BLIND / WAITING FOR FIRST AUDIBLE BLOCK";
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
    else if (state.osAccess == Access::unowned)
        state.status = "REF REQUIRES KIRIN OS";
    else if (state.osAccess == Access::ownedDisconnected)
        state.status = "WAITING FOR KIRIN OS REFERENCE";
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
    if (runtime.presetSelectionStatus == "pending")
    {
        state.status = "KIRIN OS PREPARING CHECK PRESET / A REMAINS LIVE";
        state.actionText.clear();
    }
    else if (runtime.presetSelectionStatus == "prepared")
    {
        state.status = "CHECK PRESET READY / A REMAINS LIVE";
        state.actionText.clear();
    }
    else if (runtime.presetSelectionStatus == "timed_out")
    {
        state.status = "KIRIN OS NEEDS MORE TIME / A REMAINS LIVE";
        state.actionText = "RETRY PREPARATION";
    }
    else if (runtime.presetSelectionStatus == "preset_setup_required")
    {
        state.status = "CHOOSE A REFERENCE IN KIRIN OS / A REMAINS LIVE";
        state.actionText = "OPEN REFERENCE";
    }
    else if (runtime.presetSelectionStatus == "source_unavailable")
    {
        state.status = "REFERENCE SOURCE NEEDS ATTENTION / A REMAINS LIVE";
        state.actionText = "CHOOSE SOURCE";
    }
    else if (runtime.presetSelectionStatus == "measurement_required")
    {
        state.status = "MEASURE THE REFERENCE SOURCE IN KIRIN OS / A REMAINS LIVE";
        state.actionText = "MEASURE SOURCE";
    }
    else if (runtime.presetSelectionStatus == "request_stale"
             || runtime.presetSelectionStatus == "storage_unavailable"
             || runtime.presetSelectionStatus == "publication_failed")
    {
        state.status = "CHECK PRESET NOT REFRESHED / A REMAINS LIVE";
        state.actionText = "RETRY PREPARATION";
    }
    else if (runtime.presetSelectionStatus == "work_unavailable"
             || runtime.presetSelectionStatus == "request_invalid")
    {
        state.status = "REFERENCE SETUP NEEDS ATTENTION / A REMAINS LIVE";
        state.actionText = "OPEN REFERENCE";
    }
    else if (runtime.recoveryStatus == "pending")
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
    referenceAccessView.setOwned (processorRef.licenseIsOs());
    layoutReferenceAudition (referenceView.getBounds());
}

#endif
