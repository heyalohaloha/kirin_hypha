#include "PluginEditor.h"
#include "HyphaReferencePendingUI.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include <algorithm>
#include <cmath>
#include <iterator>
#include "HyphaUpdateContract.h"
#include "reference_audition/ReferenceRuntimePresetOptions.h"
#include "HyphaReferenceRuntimeView.h"
using namespace hypha::reference_ui::runtime_view;
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
    referenceView.onSelectA = [this] { referenceLowerAOffer = {}; processorRef.selectReferenceA(); };
    referenceView.onSelectVisualSlot = [this] (int slot) { processorRef.selectReferenceVisualSlot (slot); };
    referenceView.onExplain = [this] (const juce::String& reason) { showToast (reason); };
    wireReferenceRoles();
    referenceView.onSelectB = [this]
    {
        if (liveCompareHoldBlocksAudition()) return;
        processorRef.selectReferenceVisualSlot (1);  // H10: 押した役の画面にする
        if (! processorRef.selectReferenceB())
        {
            const auto latest = processorRef.referenceAuditionSnapshot();
            const auto& slot = latest.versionSelection ? *latest.versionSelection : latest;
            if (offerReferenceLowerA (1, slot)) return;  // 上限超え：A を下げて合わせる承認を出す
            const auto step = slotStep (slot, latest.transportPlaying);
            const auto failure = matchFailureText (slot.matchFailure);
            showToast (failure.isNotEmpty() ? failure : step == hypha::reference_ui::SourceStep::ready
                ? "V could not switch at this playhead. A remains live; retry when V is ready."
                : "V: " + hypha::reference_ui::stepText (step));
        }
    };
    referenceView.onSelectC = [this]
    {
        if (liveCompareHoldBlocksAudition()) return;
        processorRef.selectReferenceVisualSlot (2);
        if (! processorRef.selectReferenceC())
        {
            const auto latest = processorRef.referenceAuditionSnapshot();
            const auto& slot = latest.checkSelection ? *latest.checkSelection : latest;
            if (offerReferenceLowerA (2, slot)) return;
            const auto step = slotStep (slot, latest.transportPlaying);
            const auto failure = matchFailureText (slot.matchFailure);
            showToast (failure.isNotEmpty() ? failure : step == hypha::reference_ui::SourceStep::ready
                ? "C could not switch at this playhead. A remains live; retry when C is ready."
                : "C: " + hypha::reference_ui::stepText (step));
        }
    };
    referenceView.onSelectVersion = [this](const juce::String& id){if(!processorRef.selectReferenceVersion(id))showToast("Version selection was not changed");};
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
        if (approveOfferedLowerA()) return;  // 上限超えの承認：A を下げて合わせる
        const auto& state = referenceView.state();
        if (state.blindLowerAApprovalRequired && !state.blindLargeScreen) { setSize (900, 600); return; }
        const bool accepted = state.sampleRateApprovalRequired
            ? processorRef.approveReferenceSampleRateConversion(state.sampleRateApprovalSlot)
            : state.blindLowerAApprovalRequired
                ? processorRef.approveReferenceBlindLowerA (
                    state.aIntegratedLoudness, state.aMaximumTruePeakDbtp)
                : state.presetSelectionAction == "retry"
                    ? processorRef.retryReferencePresetSelection()
                    : state.candidatePreparationAction == "retry"
                        ? processorRef.retryReferenceCandidatePreparation()
                    : processorRef.requestReferenceRecovery();
        if (! accepted) showToast (state.sampleRateApprovalRequired
            ? "This source is no longer awaiting sample-rate approval; check V or C again"
            : "Kirin OS could not receive the request");
    };
    referenceView.onStartBlind = [this]
    {
        if (liveCompareHoldBlocksAudition()) return;
        if (getWidth() < 900 || getHeight() < 600)
        {
            setSize (900, 600);
            showToast ("Blind view opened. Press VERSION BLIND to start after checking the level.");
            return;
        }
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
    referenceView.onRevealBlind = [this] { if (! processorRef.revealReferenceBlind()) showToast ("Listen to both sources before revealing"); };
    referenceView.onEndBlind = [this] { processorRef.endReferenceBlind(); };
    referenceView.onStartReview=[this]{if(!processorRef.startLatestReferenceReview())showToast("Today's review is unavailable");};
    referenceView.onStartBookmark=[this]{if(!processorRef.startLatestReferenceBookmark())showToast("Bookmark is unavailable");};
    referenceView.onWorkflowBack=[this]{if(!processorRef.moveReferenceWorkflow(-1,false,false))showToast("Previous item is unavailable");};
    referenceView.onWorkflowConfirmed=[this]{if(!processorRef.moveReferenceWorkflow(1,true,false))showToast("Next item is unavailable");};
    referenceView.onWorkflowDeferred=[this]{if(!processorRef.moveReferenceWorkflow(1,false,true))showToast("Next item is unavailable");};
    referenceView.onWorkflowEnd=[this]{processorRef.endReferenceWorkflow();};
    referenceView.onCapturedTonalRange=[this](double start,double end)
    {processorRef.setReferenceCaptureTonalRange(start,end);};
    scaleRoot.addChildComponent (referenceView);
}
void KirinHyphaEditor::layoutReferenceAudition()
{
    // A selected domain survives VU/Blind replacement, but does not own the visible surface.
    // Timer refresh must not bring an invisible Reference pane over the VU return control.
    const bool reference = observatoryDomain == hypha::observatory::Domain::reference
        && ! observatoryView.hybridVuVisible() && ! localBlindOpen && ! liveBlindOpen;
    const bool access = hypha::reference_ui::needsAccessPanel (referenceView.state());
    processorRef.setReferenceViewPresented (reference);
    const bool referenceWasVisible = referenceView.isVisible(), accessWasVisible = referenceAccessView.isVisible();
    referenceView.setVisible (reference && ! access);
    if (referenceView.isVisible() && ! referenceWasVisible) referenceView.toFront (false);
    referenceAccessView.setVisible (reference && access);
    if (referenceAccessView.isVisible() && ! accessWasVisible) referenceAccessView.toFront (false);
    layoutLocalBlindProduct();
    layoutBodyAndFeedback();
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
    if (appearanceSnapshot.activationSeen)
    {
        menu.addSeparator();
        menu.addSectionHeader ("Display");
        menu.addItem (jungleModeMenuAction, "Jungle Mode", true,
                      observatoryView.jungleAppearanceEnabled());
    }
    const juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&referenceAccessView)
        .withDeletionCheck (*this).withMinimumWidth (360).withStandardItemHeight (28),
        [safe] (int selected) { if (safe != nullptr) safe->handleInformationMenu (selected); });
}
void KirinHyphaEditor::refreshReferenceAudition (const KirinObservatoryFrame& frame,
                                                  bool frameAvailable)
{
    auto runtime = processorRef.referenceAuditionSnapshot();
    const auto& checkSelection = runtime.checkSelection ? *runtime.checkSelection : runtime;
    const auto& versionSelection = runtime.versionSelection ? *runtime.versionSelection : runtime;
    const auto& referenceSelection = runtime.referenceSelection ? *runtime.referenceSelection : runtime;
    const auto& audible = runtime.audibleComparisonSlot == 1 || runtime.blindPhase != hypha::reference_audition::BlindPhase::inactive
        ? versionSelection : runtime.audibleComparisonSlot == 3 ? referenceSelection : checkSelection;
    const bool callbackLive = processorRef.heartbeatLive();
    // H3: 追従が上限（True Peak）か MATCH から ±6 dB で止まったら一度だけ知らせる（R-28）。今の gain は保たれる。
    using Tracking = hypha::reference_audition::TrackingState;
    const bool trackingStopped = runtime.bSelected && (audible.tracking == Tracking::stoppedCeiling || audible.tracking == Tracking::stoppedRange);
    if (trackingStopped && ! referenceTrackingStopShown)
        showToast (audible.tracking == Tracking::stoppedCeiling ? "Level follow stopped at the safe ceiling. The current gain is kept."
                                                                : "Level follow stopped 6 dB from the MATCH. The current gain is kept.");
    referenceTrackingStopShown = trackingStopped;
    hypha::reference_ui::State state;
    state.readiness = referenceReadiness (runtime.state);
    const bool connected = runtime.libraryReceived;
    state.osOnline = runtime.osOnline;
    state.libraryReceived = runtime.libraryReceived;
    state.blindLargeScreen = getWidth() >= 900 && getHeight() >= 600;
    state.separateComparisons = runtime.separateComparisons;
    state.comparisonSlot = runtime.comparisonSlot;
    state.audibleComparisonSlot = runtime.audibleComparisonSlot;
    state.transportPlaying = runtime.transportPlaying;
    state.versionArmable = runtime.versionArmable;
    state.checkArmable = runtime.checkArmable;
    state.pendingAudition = runtime.pendingAudition;
    state.versionId = runtime.selectedVersionId;
    state.versions = selectionOptions (runtime.versions);
    state.versionReady = callbackLive && runtime.versionReady;
    state.checkReady = callbackLive && runtime.checkReady;
    state.osAccess = hypha::os_access::classify (
        processorRef.licenseIsOs(), connected,
        runtime.state == hypha::reference_audition::RuntimeState::ready);
    state.auditionBuffered = callbackLive && runtime.transportPlaying
        && runtime.transportPositionValid && runtime.auditionBuffered;
    state.title = runtime.title;
    state.sourceLabel = sourceLabel (runtime.sourceKind);
    state.alignmentLabel = runtime.separateComparisons && runtime.comparisonSlot == 1
        ? (runtime.versionReady ? "CONTENT ALIGNED" : "ALIGNING") : runtime.alignmentMode
            == hypha::reference_audition::AlignmentMode::sampleLock
        ? "PROJECT TIMELINE" : "REFERENCE CUE";
    state.bSelected = runtime.bSelected;
    state.gainLimited = audible.gainLimited;
    state.comparisonFallbackOriginal = audible.comparisonFallbackOriginal;
    state.originalAudition = audible.comparisonMode == "original";
    state.activeBlindStimulus = runtime.activeBlindStimulus;
    state.pendingBlindStimulus = runtime.pendingBlindStimulus;
    state.answeredBlindStimulus = runtime.answeredBlindStimulus;
    state.blindStimulusOneHeard = runtime.blindStimulusOneHeard;
    state.blindStimulusTwoHeard = runtime.blindStimulusTwoHeard;
    state.blindLowerAApprovalRequired = runtime.blindLowerAApprovalRequired;
    state.blindRequiredAAttenuationDb = runtime.blindRequiredAAttenuationDb;
    state.blindReveal = runtime.blindReveal;
    state.blindPaused = runtime.blindPhase == hypha::reference_audition::BlindPhase::active && !runtime.transportPlaying;
    state.blindOutsideSong = runtime.blindPhase == hypha::reference_audition::BlindPhase::active && runtime.transportPlaying && runtime.activeBlindStimulus == 0;
    const bool liveA = frameAvailable
        && frame.meter.state != KIRIN_METER_SESSION_EMPTY
        && std::isfinite (frame.meter.lufs_i)
        && std::isfinite (frame.meter.max_true_peak);
    const bool frozenBlindA = runtime.blindPhase == hypha::reference_audition::BlindPhase::active
        || runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed;
    state.aAvailable = runtime.bSelected || frozenBlindA
        || (callbackLive && runtime.transportPlaying && runtime.transportPositionValid);
    setSourceSteps (state, runtime);
    setSampleRateApproval (state, runtime);
    state.aIntegratedLoudness = runtime.bSelected || frozenBlindA
        ? audible.aIntegratedLoudness
        : liveA ? frame.meter.lufs_i : hypha::reference_ui::unavailableValue();
    state.aMaximumTruePeakDbtp = runtime.bSelected || frozenBlindA
        ? audible.aMaximumTruePeakDbtp
        : liveA ? frame.meter.max_true_peak : hypha::reference_ui::unavailableValue();
    const bool blindAvailable = callbackLive && runtime.blindEligible
        && (runtime.separateComparisons ? hypha::reference_ui::canHearVersion (state)
                                       : hypha::reference_ui::canSelectB (state));
    state.blindPhase = referenceBlindPhase (runtime.blindPhase, blindAvailable);
    state.presetId = hypha::reference_audition::runtimePresetDisplaySelection (checkSelection);
    state.checkId = runtime.separateComparisons
        ? checkSelection.checkId + "/" + checkSelection.candidateId : runtime.checkId;
    state.candidateId = runtime.candidatePreparationTargetId.isNotEmpty()
        ? runtime.candidatePreparationTargetId : runtime.candidateId;
    state.cueId = checkSelection.cueId;
    state.presetName = checkSelection.presetName;
    state.checkLabel = runtime.checkLabel;
    state.candidateName = runtime.candidateName;
    state.cueLabel = checkSelection.cueLabel;
    state.comparisonMode = runtime.comparisonMode;
    state.presentationLayout = runtime.presentationLayout;
    state.viewBindings = runtime.viewBindings;
    state.presets = selectionOptions (checkSelection.presets);
    state.checks = selectionOptions (runtime.separateComparisons ? checkSelection.checkTargets : runtime.checks);
    state.candidates = selectionOptions (runtime.candidates);
    state.cues = selectionOptions (checkSelection.cues);
    state.checkViewBindings = checkSelection.checkViewBindings;
    state.listeningChecks = checkSelection.listeningChecks;
    state.detailedMeasurement = runtime.detailedMeasurement;
    state.visualTimeline = runtime.visualTimeline; state.visualPositionSeconds = runtime.visualPositionSeconds;
    state.visualPreferences = runtime.visualPreferences; state.captureAccess=runtime.captureAccess;
    state.profiles = runtime.profiles;
    state.presetSelectionAction = runtime.presetSelectionAction;
    state.candidatePreparationAction = runtime.candidatePreparationAction;
    state.candidatePreparationPending = checkSelection.candidatePreparationStatus == "pending";
    state.workflow = runtime.workflow;
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
    const auto& viewed = runtime.comparisonSlot == 1 ? versionSelection : runtime.comparisonSlot == 3 ? referenceSelection : checkSelection;
    if (viewed.bSelected
        || runtime.blindPhase != hypha::reference_audition::BlindPhase::inactive)
    {
        state.adjustedBIntegratedLoudness = runtime.adjustedBIntegratedLoudness;
        state.adjustedBMaximumTruePeakDbtp = runtime.adjustedBMaximumTruePeakDbtp;
        state.loudnessDeltaBMinusA = runtime.loudnessDeltaBMinusA;
        state.truePeakDeltaBMinusA = runtime.truePeakDeltaBMinusA;
    }
    state.appliedGainDb = runtime.bSelected ? audible.appliedGainDb : runtime.appliedGainDb;
    state.peakShortfallDb = runtime.bSelected ? audible.peakShortfallDb : 0.0;
    using Runtime = hypha::reference_audition::RuntimeState;
    using Access = hypha::os_access::State;
    if (runtime.blindPhase == hypha::reference_audition::BlindPhase::invalidated)
        state.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND STOPPED / A HELD -"
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURN A EXPLICITLY"
            : "BLIND STOPPED / RETURN A EXPLICITLY";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active && !runtime.transportPlaying)
        state.status = "PAUSED / PLAY TO RESUME BLIND";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active
        && runtime.activeBlindStimulus == 0)
        state.status = "PLAY WITHIN THE SONG / A REMAINS LIVE";
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
        state.status = juce::String (hypha::reference_ui::roleLetter (runtime.audibleComparisonSlot)) + " AUDITION / PRE DELTA PAUSED";
    else if (state.osAccess == Access::unowned)
        state.status = "REF REQUIRES KIRIN OS";
    else if (state.osAccess == Access::ownedDisconnected)
        state.status = "WAITING FOR KIRIN OS REFERENCE";
    else if (runtime.state == Runtime::ready)
        state.status = state.auditionBuffered ? "READY / A REMAINS LIVE"
            : runtime.auditionOutsideCue ? "OUTSIDE " + juce::String (hypha::reference_ui::roleLetter (runtime.comparisonSlot))
                + " CUE / MOVE OR CHOOSE LONGER CUE"
            : state.aAvailable ? "LOADING " + juce::String (hypha::reference_ui::roleLetter (runtime.comparisonSlot))
                + " AT PLAYHEAD / KEEP PLAYING" : "PLAY A TO AUDITION";
    else if (runtime.rejectionCode == "reference_selection_unavailable")
        state.status = "SAVED CHOICE UNAVAILABLE / CHOOSE AGAIN";
    else if (runtime.state == Runtime::verifying)
        state.status = "VERIFYING SOURCE";
    else if (runtime.state == Runtime::rejected)
        state.status = rejectedStatus (runtime.rejectionCode);
    else if (runtime.state == Runtime::waiting)
        state.status = runtime.rejectionCode == "reference_version_unselected" ? "CHOOSE VERSION V"
            : runtime.rejectionCode == "reference_alignment_waiting_for_content" ? "PLAY A / ALIGNING VERSION V"
            : runtime.rejectionCode == "reference_alignment_no_match" ? "NO VERIFIED MATCH / CHECK VERSION V"
            : runtime.rejectionCode == "reference_alignment_ambiguous" ? "PLAY ANOTHER PASSAGE TO ALIGN V"
            : runtime.rejectionCode == "reference_candidates_empty" ? "CHOOSE A SOURCE IN KIRIN OS"
            : runtime.rejectionCode == "reference_checks_empty" ? "ENABLE A CHECK IN KIRIN OS"
            : runtime.rejectionCode == "reference_source_unavailable" ? "SOURCE UNAVAILABLE / OPEN KIRIN OS"
            : "RECEIVING REFERENCE";
    else
        state.status = "OPEN KIRIN OS";
    if (state.sampleRateApprovalRequired)
    {
        const auto sourceRate = juce::String (state.sourceSampleRateHz / 1000.0, 1);
        const auto hostRate = juce::String (state.hostSampleRateHz / 1000.0, 1);
        const juce::String target = state.sampleRateApprovalSlot == 1 ? "V" : "C";
        if (!runtime.bSelected) state.status = target + " SAMPLE RATE " + sourceRate + " TO " + hostRate
            + " kHz / A REMAINS LIVE";
        state.actionText = getWidth() < 600 ? "APPROVE " + target + " RATE"
            : "APPROVE " + target + " " + sourceRate + " TO " + hostRate + " kHz";
    }
    else if (runtime.presetSelectionStatus == "pending")
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
    else if (runtime.candidatePreparationStatus == "pending")
    {
        state.status = "KIRIN OS PREPARING REFERENCE / A REMAINS LIVE";
        state.actionText.clear();
    }
    else if (runtime.candidatePreparationStatus == "prepared")
    {
        state.status = "REFERENCE READY / A REMAINS LIVE";
        state.actionText.clear();
    }
    else if (runtime.candidatePreparationStatus == "timed_out")
    {
        state.status = "KIRIN OS NEEDS MORE TIME / A REMAINS LIVE";
        state.actionText = "RETRY PREPARATION";
    }
    else if (runtime.candidatePreparationStatus == "source_unavailable")
    {
        state.status = "REFERENCE SOURCE NEEDS ATTENTION / A REMAINS LIVE";
        state.actionText = "CHOOSE SOURCE";
    }
    else if (runtime.candidatePreparationStatus == "measurement_required")
    {
        state.status = "MEASURE THE REFERENCE SOURCE IN KIRIN OS / A REMAINS LIVE";
        state.actionText = "MEASURE SOURCE";
    }
    else if (runtime.candidatePreparationStatus == "request_stale"
             || runtime.candidatePreparationStatus == "storage_unavailable"
             || runtime.candidatePreparationStatus == "publication_failed")
    {
        state.status = "REFERENCE NOT REFRESHED / A REMAINS LIVE";
        state.actionText = "RETRY PREPARATION";
    }
    else if (runtime.candidatePreparationStatus.isNotEmpty())
    {
        state.status = "REFERENCE SETUP NEEDS ATTENTION / A REMAINS LIVE";
        state.actionText = "OPEN REFERENCE";
    }
    else if (runtime.recoveryStatus == "pending")
    {
        state.status = "OPENING REFERENCE IN KIRIN OS";
        state.actionText.clear();
    }
    else if (runtime.recoveryStatus == "opened")
    {
        state.status = "CONTINUE IN KIRIN OS";
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
    else if (state.blindLowerAApprovalRequired)
    {
        const auto attenuation = juce::String (state.blindRequiredAAttenuationDb, 1);
        state.status = "BLIND NEEDS HEADROOM / A RETURNS +" + attenuation + " dB ON END";
        state.actionText = state.blindLargeScreen ? "LOWER A " + attenuation + " dB & START" : juce::String {};
    }
    else if (connected && (runtime.state == Runtime::rejected
                           || runtime.state == Runtime::waiting))
        state.actionText = "OPEN REFERENCE";
    // 2026-10-04：PREPARE VISUALS（Kirin OS を開くだけ。Kirin OS は自分で測る。進み具合は状態の行が言う）と
    // EDIT GENRE（Kirin OS が受け付けない "balance" の表示にしか出ない）は外した。
    applyReferenceRoles (state, runtime);
    if (const auto pending = hypha::reference_ui::pendingAuditionText (state);
        pending.isNotEmpty() && ! (state.lowerAOfferSlot != 0 && state.lowerAOfferSlot == state.comparisonSlot))
        state.status = pending;  // 見ている役に上限超えの承認を出していれば、承認の文を残す
    referenceView.setState (std::move (state));
    referenceAccessView.setOwned (processorRef.licenseIsOs());
    layoutReferenceAudition();
}
#endif
