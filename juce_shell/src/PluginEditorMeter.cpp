#include "PluginEditor.h"

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
    if (localBlindOpen)
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
    const bool keepActive = recording || preparing || armed;

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
        toastText = keepNotice;
        toastUntil = now + 3.0;
    }

    const auto recordError = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText
                              : recordError.isNotEmpty() ? recordError
                              : recordPhase == KIRIN_RECORD_DISPLAY_UNAVAILABLE
                                  ? juce::String ("Final measurement unavailable")
                              : preparing ? juce::String ("Preparing pairs...")
                              : armed ? juce::String ("Ready to bounce")
                              : juce::String();
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
