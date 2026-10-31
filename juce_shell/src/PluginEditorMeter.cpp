#include "PluginEditor.h"
#include "HyphaKeepPreWait.h"
#include "HyphaChainTimingPreference.h"
#include "HyphaChainTimingText.h"

void KirinHyphaEditor::refreshWatchSnapshot()
{
    KirinWatchDisplay watch {};
    if (! processorRef.pollWatchDisplay (watch))
        return;
    observatoryWatchDisplay = watch;
    haveObservatoryWatchDisplay = true;
}

uint8_t KirinHyphaEditor::refreshRecordPhase()
{
    KirinRecordDisplay observed {};
    if (processorRef.pollRecordDisplay (observed))
    {
        cachedRecordDisplay = observed;
        haveRecordDisplay = true;
    }
    const auto phase = haveRecordDisplay ? cachedRecordDisplay.phase
                                         : (uint8_t) KIRIN_RECORD_DISPLAY_WATCH;
    observatoryView.setRecordDisplay (
        cachedRecordDisplay,
        haveRecordDisplay && phase != KIRIN_RECORD_DISPLAY_WATCH);
    return phase;
}

void KirinHyphaEditor::updatePre()
{
    const bool alive = processorRef.measureAlive();
    const int signal = processorRef.signalStateLive();
    const bool recording = processorRef.isRecording();
    const bool acknowledged = processorRef.recordAcknowledged();
    const bool preset = processorRef.presetAvailable();

    nameField.setModelName (processorRef.preName());
    nameField.setFallback (instanceId8());

    const double now = nowSecs();
    if (acknowledged && ! prevAck)
        bannerUntil = now + 3.0;
    prevAck = acknowledged;

    const auto recordPhase = refreshRecordPhase();
    refreshWatchSnapshot();

    const auto anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty())
    {
        pathAnomalyText = anomaly;
        pathAnomalyUntil = now + 5.0;
    }
    const bool anomalyActive = now < pathAnomalyUntil && pathAnomalyText.isNotEmpty();
    const auto recordError = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText
                              : recordError.isNotEmpty() ? recordError
                              : recordPhase == KIRIN_RECORD_DISPLAY_UNAVAILABLE
                                  ? juce::String ("Final measurement unavailable")
                                  : juce::String();
    updateFeedback (now, now < bannerUntil, status);
    led.setState (hypha::deriveLedState (
        alive, signal, recording, acknowledged, preset));
}

void KirinHyphaEditor::updatePost()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (localBlindOpen || liveBlindOpen)
        return;
   #endif
    const bool alive = processorRef.measureAlive();
    const int signal = processorRef.signalStateLive();
    const bool recording = processorRef.isRecording();
    const bool acknowledged = processorRef.recordAcknowledged();
    const bool preset = processorRef.presetAvailable();
    const int keepPhase = processorRef.keepPhase();
    const bool preparing = keepPhase == (int) KIRIN_KEEP_PHASE_PREPARING;
    const bool armed = keepPhase == (int) KIRIN_KEEP_PHASE_ARMED;
    const bool keepActive = recording || preparing || armed || keepPreWaitSince > 0.0;

    observatoryView.setNoteAvailability (processorRef.licenseIsOs(), recording);
    observatoryView.setKeepActive (keepActive);
    nameField.setModelName (processorRef.pairDisplayName());
    nameField.setEditingEnabled (! (processorRef.isPlaying()
                                    && processorRef.heartbeatLive()));

    const double now = nowSecs();
    if (acknowledged && ! prevAck)
        bannerUntil = now + 3.0;
    prevAck = acknowledged;

    const auto recordPhase = refreshRecordPhase();
    const auto anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty())
    {
        pathAnomalyText = anomaly;
        pathAnomalyUntil = now + 5.0;
    }
    const bool anomalyActive = now < pathAnomalyUntil && pathAnomalyText.isNotEmpty();

    const auto keepNotice = processorRef.drainKeepActionNotice();
    if (keepNotice.isNotEmpty())
    {
        comparisonNoticeReason = KIRIN_COMPARISON_REASON_NONE;
        toastText = keepNotice;
        toastUntil = now + 3.0;
    }

    // The chain timing on the footer, only while the user keeps it turned on (INV-LC25).
    const auto chain = hypha::ChainTimingFooterPreference::shared().isEnabled()
        ? hypha::chain_timing::footerReadout (processorRef.chainTimingView())
        : hypha::chain_timing::FooterReadout {};
    chainFooterText = chain.text;
    observatoryView.setChainReadout (chain.text, chain.caution);

    const auto recordError = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText
                              : recordError.isNotEmpty() ? recordError
                              : recordPhase == KIRIN_RECORD_DISPLAY_UNAVAILABLE
                                  ? juce::String ("Final measurement unavailable")
                              : preparing ? juce::String ("Preparing pairs...")
                              : armed ? juce::String ("Ready to bounce")
                              : liveCompareWarningText();
    updateFeedback (now, now < bannerUntil, status);

    refreshWatchSnapshot();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (refreshAnalysisViews (
            alive, signal, recording, armed, acknowledged, preset,
            processorRef.pairStatus()))
        return;
   #endif

    // PREPARING is not an active Record light. The generation becomes user-ready only after
    // every member reaches the shared Armed barrier.
    led.setState (hypha::deriveLedState (
        alive, signal, recording && armed, acknowledged, preset));
}

// Keep（2026-10-31）：組んだ PRE がまだ応答しない（project を開いた直後・PRE の準備中）だけなら、押した Keep を
// 待たせて 0.5 秒ごとに確かめ、応答したら始める。上限まで応答しない時だけ「選び直して」と言う。retry は timer から。
void KirinHyphaEditor::requestKeepPair (bool retry)
{
    const auto now = nowSecs();
    if (retry && (keepPreWaitSince <= 0.0 || now < keepPreWaitNextAt)) return;
    if (retry && waitingPairKey() != keepPreWaitPair) { keepPreWaitSince = 0.0; return; }  // PRE が替わった
    if (retry && now - keepPreWaitSince >= hypha::keep_ui::preWaitSeconds)
    {   // 期限を先に確かめる：上限を過ぎた要求では Keep を始めない
        keepPreWaitSince = 0.0;
        showToast (juce::String (hypha::keep_ui::preUnavailableNotice.data(),
                                 hypha::keep_ui::preUnavailableNotice.size()));
        return;
    }
    if (! retry) { keepPreWaitSince = 0.0; keepPreWaitPair = waitingPairKey(); }
    const bool started = processorRef.keepPair();
    const juce::String notice = started ? juce::String() : processorRef.drainKeepActionNotice();
    const auto waited = keepPreWaitSince > 0.0 ? now - keepPreWaitSince : 0.0;
    using hypha::keep_ui::KeepWait;
    const auto next = hypha::keep_ui::keepWait (started, notice.toStdString(), waited);
    if (next == KeepWait::wait)
    {
        if (keepPreWaitSince <= 0.0) { keepPreWaitSince = now; showToast ("Waiting for PRE to start Keep"); }
        keepPreWaitNextAt = now + hypha::keep_ui::preRetrySeconds;
        return;
    }
    keepPreWaitSince = 0.0;
    if (next == KeepWait::done) return;
    if (notice.isNotEmpty()) { showToast (notice); return; }
    const juce::String err = processorRef.recordErrorMessage();
    if (err.isNotEmpty()) { showToast (err); return; }
    if (! retry) showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
}

// The pair a waiting press belongs to, with the processor's restore and pair ticket: a preset or Undo restore,
// or unpairing and pairing again, drops the press even when the PRE id and name come back the same.
juce::String KirinHyphaEditor::waitingPairKey() const
{
    return processorRef.pairedPreInstanceId() + "|" + processorRef.pairDisplayName() + "|"
         + juce::String ((juce::int64) processorRef.restoreOrPairTicket());
}

void KirinHyphaEditor::dropWaitingRequests()
{
    keepPreWaitSince = 0.0;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    liveCompareStartAfterReturnUntil = 0.0;
    referenceRematchUntil = 0.0;
    referenceOpenedSlot = 0;  // the page-opened notice of a C or V press
   #endif
}
