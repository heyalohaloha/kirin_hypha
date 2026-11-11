#include "PluginEditor.h"
#include "HyphaReferenceTargetView.h"
#include "HyphaReferencePendingUI.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
#include <algorithm>
#include <cmath>
#include <iterator>
#include "HyphaUpdateContract.h"
#include "reference_audition/ReferenceRuntimePresetOptions.h"
#include "HyphaReferenceRuntimeView.h"
#include "HyphaReferenceRuntimeStatus.h"
#include "HyphaReferenceNotices.h"
#include "HyphaVersionBlindScreen.h"
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
    referenceView.onSelectA = [this] { referenceRematchUntil = liveCompareStartAfterReturnUntil = 0.0; afterTail = {}; processorRef.selectReferenceA(); };
    referenceView.onSelectVisualSlot = [this] (int slot) { processorRef.selectReferenceVisualSlot (slot); };
    // 開いている間に役や状態が替わったリストの回答は断られる（何も変えず、変わっていないと言う）。
    referenceView.onSelectAWindow = [this] (auto target, auto choice, int seconds)
    { if (! processorRef.selectReferenceAWindow (target, choice, seconds)) showToast ("A window was not changed"); };
    referenceView.onResetAWindow = [this] (auto target)
    { if (! processorRef.resetReferenceAWindow (target)) showToast ("A window was not changed"); };
    referenceView.onExplain = [this] (const juce::String& reason) { showToast (reason); };
    referenceView.onOverlayChange = [this] (const auto& overlay) { processorRef.setReferenceOverlay (overlay); };
    wireReferenceRoles();
    referenceView.onSelectB = [this]
    {
        if (waitsForAbTail (hypha::output_owner::Activity::audition, hypha::live_compare_ui::AfterTail::referenceV)) return;
        if (outputRefused (hypha::output_owner::Activity::audition)) return;  // a refused press keeps the waiting ones
        referenceRematchUntil = liveCompareStartAfterReturnUntil = 0.0;  // 別の役は待たせた C の MATCH・LISTEN を捨てる
        afterTail = {};
        processorRef.selectReferenceVisualSlot (1);  // 押した役の画面にする
        if (! processorRef.selectReferenceB())
        {
            const auto latest = processorRef.referenceAuditionSnapshot();
            const auto& slot = latest.versionSelection ? *latest.versionSelection : latest;
            showToast (hypha::reference_ui::notice::roleUnavailable (1, slotStep (slot, latest.transportPlaying), slot.matchFailure, rejectionCodeOf (slot)));
        }
    };
    referenceView.onSelectC = [this]
    {
        if (waitsForAbTail (hypha::output_owner::Activity::audition, hypha::live_compare_ui::AfterTail::referenceC)) return;
        if (outputRefused (hypha::output_owner::Activity::audition)) return;  // a refused press keeps the waiting ones
        referenceRematchUntil = liveCompareStartAfterReturnUntil = 0.0;  // 別の役は待たせた C の MATCH・LISTEN を捨てる
        afterTail = {};
        processorRef.selectReferenceVisualSlot (2);
        if (! processorRef.selectReferenceC())
        {
            const auto latest = processorRef.referenceAuditionSnapshot();
            const auto& slot = latest.checkSelection ? *latest.checkSelection : latest;
            showToast (hypha::reference_ui::notice::roleUnavailable (2, slotStep (slot, latest.transportPlaying), slot.matchFailure, rejectionCodeOf (slot)));
        }
    };
    referenceView.onSelectVersion = [this](const juce::String& id){if(!processorRef.selectReferenceVersion(id))showToast("Version selection was not changed");};
    referenceView.onSelectPreset = [this] (const juce::String& id)
    {
        referenceRematchUntil = 0.0;
        if (! processorRef.selectReferencePreset (id))
            showToast ("Check Preset selection was not changed");
    };
    referenceView.onSelectCheck = [this] (const juce::String& id)
    {
        referenceRematchUntil = 0.0;
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
        referenceRematchUntil = 0.0;
        if (! processorRef.selectReferenceCue (id))
            showToast ("Cue selection was not changed");
    };
    // REF のアクション：表示と一緒に決めた意図（state.action）だけを実行する。ほかの印から推し量らない。
    referenceView.onAction = [this]
    {
        using hypha::reference_ui::ActionKind;
        const auto state = referenceView.state();
        const auto unreachable = [this] (bool sent) { if (! sent) showToast ("Kirin OS could not receive the request"); };
        switch (state.action.kind)
        {
            case ActionKind::lowerAAndPlay: approveOfferedLowerA (state.action.offer); break;
            case ActionKind::approveBlindLowerA:
                if (outputRefused (hypha::output_owner::Activity::versionBlind)) break;
                if (! processorRef.approveReferenceBlindLowerA (state.aIntegratedLoudness, state.aMaximumTruePeakDbtp))
                    showToast ("Blind Compare could not start");
                break;
            case ActionKind::retryPresetPreparation: unreachable (processorRef.retryReferencePresetSelection()); break;
            case ActionKind::retryCandidatePreparation: unreachable (processorRef.retryReferenceCandidatePreparation()); break;
            case ActionKind::openReference:
            case ActionKind::chooseSource:
            case ActionKind::measureSource:
            case ActionKind::retryKirinOs: unreachable (processorRef.requestReferenceRecovery()); break;
            case ActionKind::none: break;
        }
    };
    referenceView.onStartBlind = [this]
    {
        if (outputRefused (hypha::output_owner::Activity::versionBlind)) return;  // A を下げている・ほかの比較のあいだは断る
        // 小さい窓は広げてそのまま始める（一押し）。広げた大きさは Blind の終わりで元へ戻す。
        const bool widened = getWidth() < 900 || getHeight() < 600;
        if (widened && ! ensureReferenceRoom()) return;
        const auto& state = referenceView.state();
        if (! processorRef.startReferenceBlind (state.aIntegratedLoudness,
                                                 state.aMaximumTruePeakDbtp))
        {
            if (widened) returnToHeldSize (referenceReturnSize);
            showToast ("Blind Compare could not start");
        }
        else if (widened) referenceReturnOnBlindEnd = true;
    };
    // 始めた VERSION BLIND は PRE/POST Blind と同じ画面（versionBlindView）が窓全体に出して操作を受ける。失敗は Blind の画面が言う。
    hypha::reference_ui::wireVersionBlindScreen (versionBlindView,
        { [this] (int stimulus) { return processorRef.selectReferenceBlindStimulus (stimulus); },
          [this] { return processorRef.revealReferenceBlind(); }, [this] { processorRef.endReferenceBlind(); } },
        [this] (const auto& notice) { versionBlindNotice = notice; versionBlindNoticeUntil = juce::Time::getMillisecondCounter() + 5000; });
    scaleRoot.addChildComponent (versionBlindView);
    referenceView.onTargetRequest = [this] (const auto& request) {
        if (! processorRef.changeReferenceTarget (request) && processorRef.referenceTargetSnapshot().issue.isEmpty())
            showToast ("TARGET UNAVAILABLE");
    };
    // A V, C or B press waiting for the A/B tail was for the choice on screen: another choice drops it.
    for (auto* choice : { &referenceView.onSelectSong, &referenceView.onSelectSongSet, &referenceView.onSelectVersion,
                          &referenceView.onSelectPreset, &referenceView.onSelectCheck, &referenceView.onSelectCandidate,
                          &referenceView.onSelectCue })
        *choice = [this, chosen = std::move (*choice)] (const juce::String& id)
        {
            dropReferenceAfterTail();
            if (chosen) chosen (id);
        };
    // 押した役の画面と状態を、次の見回り（10 Hz）を待たずに描く（2026-11-11：役を押した直後に古い画面のタブを押せた）。
    const auto refreshNow = [this]
    {
        KirinObservatoryFrame frame {};
        const bool available = processorRef.pollObservatoryFrame (frame);
        refreshReferenceAudition (frame, available);
    };
    for (auto* role : { &referenceView.onSelectA, &referenceView.onSelectB, &referenceView.onSelectC, &referenceView.onSelectRef })
        *role = [refreshNow, press = std::move (*role)] { if (press) press(); refreshNow(); };
    referenceView.onSelectVisualSlot = [refreshNow, choose = std::move (referenceView.onSelectVisualSlot)] (int slot)
    { if (choose) choose (slot); refreshNow(); };
    referenceView.pageCurrent = [this, refreshNow] (int page)
    {
        if (processorRef.referenceAuditionSnapshot().comparisonSlot == page) return true;
        refreshNow();
        return false;
    };
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
    state.switchingSlot = runtime.switchingSlot;
    state.versionId = runtime.selectedVersionId;
    state.versions = selectionOptions (runtime.versions);
    state.versionReady = callbackLive && runtime.versionReady;
    state.checkReady = callbackLive && runtime.checkReady;
    state.osAccess = hypha::os_access::classify (
        processorRef.licenseIsOs(), connected,
        runtime.state == hypha::reference_audition::RuntimeState::ready);
    state.auditionBuffered = callbackLive && runtime.transportPlaying
        && runtime.transportPositionValid && runtime.auditionBuffered;
    // 見ている画面ではなく、鳴らしている役の音が届いているか（状態の行の「C PAGE · V PLAYING」の V か A か）。
    const auto* heardRole = runtime.audibleComparisonSlot == 1 ? runtime.versionSelection.get()
        : runtime.audibleComparisonSlot == 2 ? runtime.checkSelection.get()
        : runtime.audibleComparisonSlot == 3 ? runtime.referenceSelection.get() : nullptr;
    state.audibleHeard = callbackLive && runtime.transportPlaying && runtime.transportPositionValid
        && heardRole != nullptr && heardRole->bSelected && heardRole->auditionBuffered;
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
    state.blindOneIsComparison = runtime.blindStimulusOneIsComparison;
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
    // V の Version を替えて A へ戻った一瞬は図を前のまま保つ（曲の頭の図へ跳ばない）。前の V が鳴っているあいだは今の図。
    const auto timelineNow = juce::Time::getMillisecondCounterHiRes();
    if (runtime.comparisonSlot == 1 && runtime.switchingSlot == 1 && runtime.audibleComparisonSlot != 1
        && referenceSwitchTimeline != nullptr && timelineNow < referenceSwitchHoldUntilMs)
        state.visualTimeline = referenceSwitchTimeline;
    else
    {
        state.visualTimeline = runtime.visualTimeline;
        referenceSwitchTimeline = runtime.visualTimeline;
        referenceSwitchHoldUntilMs = timelineNow + 3000.0;
    }
    state.visualPositionSeconds = runtime.visualPositionSeconds;
    state.visualPreferences = runtime.visualPreferences;
    if (runtime.visualPreferences) { state.aWindows = runtime.visualPreferences->aWindows(); state.overlay = runtime.visualPreferences->overlay(); }
    state.aWindowTarget = runtime.aWindowTarget;
    state.profiles = runtime.profiles;
    state.candidatePreparationPending = checkSelection.candidatePreparationStatus == "pending";
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
    }
    state.appliedGainDb = runtime.bSelected ? audible.appliedGainDb : runtime.appliedGainDb;
    state.peakShortfallDb = runtime.bSelected ? audible.peakShortfallDb : 0.0;
    state.rangeShortfallDb = runtime.bSelected ? audible.rangeShortfallDb : 0.0;
    {
        auto shown = hypha::reference_ui::runtimeStatus (runtime, state);
        state.status = std::move (shown.status);
        state.kirinOsRequest = shown.request;
        state.action = shown.action;
        state.actionText = std::move (shown.actionText);
    }
    applyReferenceRoles (state, runtime);
    if (const auto pending = hypha::reference_ui::pendingAuditionText (state);
        pending.isNotEmpty() && state.action.kind != hypha::reference_ui::ActionKind::lowerAAndPlay
            && ! (state.kirinOsRequest && state.action.kind != hypha::reference_ui::ActionKind::none))
        state.status = pending;  // 見ている役に上限超えの承認を出していれば、承認の文を残す
    hypha::reference_ui::applyMeasuredTarget (state, processorRef.referenceTargetSnapshot(), processorRef.licenseIsOs());
    referenceView.setState (std::move (state));
    serviceReferenceOpenedNotice();  // C・V を開いて 0.2 秒後、今の状態で窓と押下を確かめて言う
    if (juce::Time::getMillisecondCounter() > versionBlindNoticeUntil) versionBlindNotice = {};
    versionBlindView.setScreen (hypha::reference_ui::versionBlindScreen (referenceView.state(), versionBlindNotice));
    referenceAccessView.setOwned (processorRef.licenseIsOs());
    layoutReferenceAudition();
}
#endif
