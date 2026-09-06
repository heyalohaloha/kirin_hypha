#include "PluginEditor.h"
#include "HyphaDisplayContract.h"

#include <limits>

using hypha::COL_FLORA;
using hypha::COL_MUTED;
using hypha::COL_NORMAL;
using hypha::COL_SPECTRUM_POST;

namespace
{
    namespace display = hypha::display_contract;
    const double kNaN = std::numeric_limits<double>::quiet_NaN();

    juce::String pairStatusText (int status, bool postAbsolute = false)
    {
        if (postAbsolute) return "ABS";
        if (status == KIRIN_PAIR_STATUS_PAIRED) return juce::CharPointer_UTF8 ("PAIR ●");
        if (status == KIRIN_PAIR_STATUS_WAITING) return juce::CharPointer_UTF8 ("PAIR ◌");
        return juce::CharPointer_UTF8 ("PAIR —");
    }

    juce::Colour pairStatusColour (int status, bool postAbsolute = false)
    {
        if (postAbsolute) return COL_SPECTRUM_POST;
        if (status == KIRIN_PAIR_STATUS_PAIRED) return hypha::COL_LED_BLUE;
        if (status == KIRIN_PAIR_STATUS_WAITING) return COL_FLORA;
        return COL_MUTED;
    }

    juce::String pairStatusHelp (int status, bool postAbsolute = false)
    {
        if (postAbsolute) return "Paired PRE is off. Showing POST absolute values.";
        if (status == KIRIN_PAIR_STATUS_PAIRED) return "PRE and POST are paired.";
        if (status == KIRIN_PAIR_STATUS_WAITING) return "Waiting for the selected PRE.";
        return "No PRE pair is selected.";
    }

}

void KirinHyphaEditor::updatePre()
{
    const bool alive  = processorRef.measureAlive();
    const int  sig    = processorRef.signalStateLive(); // 0=Inactive 1=Active 2=Bypassed (B-113: heartbeat-aware)
    const bool rec    = processorRef.isRecording();    // record_sm (PRE autonomous record too)
    const bool ack    = processorRef.recordAcknowledged();
    const bool preset = processorRef.presetAvailable(); // PRE: always false
    const int pairStatus = processorRef.pairStatus();
    pairStatusLabel.setText (pairStatusText (pairStatus), juce::dontSendNotification);
    pairStatusLabel.setColour (juce::Label::textColourId, pairStatusColour (pairStatus));
    pairStatusLabel.setTooltip (pairStatusHelp (pairStatus));

    nameField.setModelName (processorRef.preName());
    nameField.setFallback (instanceId8());

    // Keeping banner: 3s on the false→true ack edge (PRE acked a POST's record signal).
    const double t = nowSecs();
    if (ack && ! prevAck)
        bannerUntil = t + 3.0; // RECORD_BANNER_DURATION_SECS
    prevAck = ack;

    KirinRecordDisplay observedRecord {};
    if (processorRef.pollRecordDisplay (observedRecord))
    {
        cachedRecordDisplay = observedRecord;
        haveRecordDisplay = true;
    }
    const uint8_t recordPhase = haveRecordDisplay
                                  ? cachedRecordDisplay.phase
                                  : (uint8_t) KIRIN_RECORD_DISPLAY_WATCH;
    const bool displayRecord = rec || recordPhase != KIRIN_RECORD_DISPLAY_WATCH;
    const bool finalUnavailable = recordPhase == KIRIN_RECORD_DISPLAY_UNAVAILABLE;
    const bool useShortTerm = processorRef.useShortTermLoudness();
    loudnessSelector.setShortTerm (useShortTerm);

    // B-128 (G-115-371 D3): restore identity anomaly を drain して 5s latch（toast 相当の寿命）。
    const juce::String anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty()) { pathAnomalyText = anomaly; pathAnomalyUntil = t + 5.0; }
    const bool anomalyActive = (t < pathAnomalyUntil) && pathAnomalyText.isNotEmpty();

    // PRE and POST share one prioritized feedback row. A path anomaly supersedes the persistent
    // I/O status; direct user toasts (POST only) are prioritized inside updateFeedback().
    const juce::String recErr = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText
                              : recErr.isNotEmpty() ? recErr
                              : finalUnavailable ? juce::String ("Final measurement unavailable")
                              : juce::String();
    updateFeedback (t, t < bannerUntil, status);

    const Kind want = displayRecord ? Kind::Abs6 : Kind::WatchAbs6;
    if (want != currentKind)
        configureForKind (want);

    KirinMeasureResult r {};
    bool have = false;
    bool muted = false;
    KirinSessionSummary summary {};
    bool haveSummary = false;
    KirinWatchDisplay watch {};
    const bool polledWatch = processorRef.pollWatchDisplay (watch);
    if (polledWatch)
    {
        observatoryWatchDisplay = watch;
        haveObservatoryWatchDisplay = true;
        watchMaximum = watch.maximum;
        haveWatchMaximum = true;
    }
    const bool haveWatch = polledWatch && sig == KIRIN_SIGNAL_STATE_ACTIVE;
    if (displayRecord)
    {
        if (haveRecordDisplay && cachedRecordDisplay.has_measure != 0)
        {
            const auto raw = cachedRecordDisplay.measure;
            r = recordPhase == KIRIN_RECORD_DISPLAY_LIVE
                    && sig == KIRIN_SIGNAL_STATE_ACTIVE
                  ? displaySmoother.smoothMeasure (raw, t)
                  : raw;
            have = true;
        }
        if (haveRecordDisplay && cachedRecordDisplay.has_session != 0)
        {
            summary = cachedRecordDisplay.session;
            haveSummary = true;
        }
    }
    else if (haveWatch)
    {
        r = displaySmoother.smoothMeasure (watch.current, t);
        have = true;
    }
    else if (sig == KIRIN_SIGNAL_STATE_INACTIVE)
    {
        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        if (displaySmoother.heldMeasureDisplay (held, t))
        {
            r = held.value;
            have = true;
            muted = held.muted;
        }
    }
    else if (! displayRecord && sig == KIRIN_SIGNAL_STATE_BYPASSED)
    {
        displaySmoother.reset();
        haveObservatoryWatchDisplay = false;
        haveWatchMaximum = false;
    }
    auto V = [&] (double x) { return have ? x : kNaN; };

    const int ledSig = (! displayRecord && sig == KIRIN_SIGNAL_STATE_INACTIVE && have && ! muted)
        ? KIRIN_SIGNAL_STATE_ACTIVE : sig;
    led.setState (hypha::deriveLedState (alive, ledSig, rec, ack, preset));

    const auto selected = [useShortTerm] (const KirinMeasureResult& value)
    {
        return useShortTerm ? value.lufs_s : value.lufs_m;
    };

    if (displayRecord)
    {
        fillAbs (0, V (selected (r)), false, muted);
        fillAbs (1, V (r.psr), false, muted);
        fillAbs (2, haveSummary ? summary.max_true_peak : kNaN, true, muted);
        fillAbs (3, haveSummary ? summary.lufs_i : kNaN, false, muted);
        fillAbs (4, V (r.crest), false, muted);
        fillAbs (5, V (r.sharpness), false, muted);
    }
    else
    {
        fillAbs (0, V (selected (r)), false, muted);
        fillAbs (1, haveWatchMaximum ? selected (watchMaximum) : kNaN, false, muted);
        fillAbs (2, V (r.true_peak), true, muted);
        fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, muted);
        fillAbs (4, V (r.crest), false, muted);
        fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, muted);
    }
}

void KirinHyphaEditor::updatePost()
{
    const bool alive  = processorRef.measureAlive();
    const int  sig    = processorRef.signalStateLive(); // B-113: heartbeat-aware (no stale Active)
    const bool rec    = processorRef.isRecording();
    const int keepPhase = processorRef.keepPhase();
    const bool preparing = keepPhase == (int) KIRIN_KEEP_PHASE_PREPARING;
    const bool armed = keepPhase == (int) KIRIN_KEEP_PHASE_ARMED;
    const bool keepActive = rec || preparing || armed;
    observatoryView.setNoteAvailability (processorRef.licenseIsOs(), rec);
    const bool ack    = processorRef.recordAcknowledged(); // POST: always false (egui parity)
    const bool preset = processorRef.presetAvailable();
    const bool playing = processorRef.isPlaying();
    const bool pairLocked = playing && processorRef.heartbeatLive();

    const juce::String pairName = processorRef.pairName();
    const int pairStatus = processorRef.pairStatus();
    const bool pairSelected = pairStatus != KIRIN_PAIR_STATUS_UNPAIRED;
    KirinDelta observedDelta {};
    const bool haveObservedDelta = pairSelected && processorRef.pollDelta (observedDelta);
    if (pairStatus != KIRIN_PAIR_STATUS_PAIRED)
        pairedPreExplicitlyBypassed = false;
    else if (haveObservedDelta)
        pairedPreExplicitlyBypassed = display::pairedPreIsExplicitlyBypassed (
            true, true, observedDelta.mode);
    const bool postAbsolute = pairStatus == KIRIN_PAIR_STATUS_PAIRED
                           && pairedPreExplicitlyBypassed;
    pairStatusLabel.setText (pairStatusText (pairStatus, postAbsolute),
                             juce::dontSendNotification);
    pairStatusLabel.setColour (juce::Label::textColourId,
                               pairStatusColour (pairStatus, postAbsolute));
    pairStatusLabel.setTooltip (pairStatusHelp (pairStatus, postAbsolute));

    nameField.setModelName (pairName);
    nameField.setEditingEnabled (! pairLocked); // W-280 + B-115 playback pair lock (playing AND live)

    const double t = nowSecs();
    if (ack && ! prevAck) bannerUntil = t + 3.0; // harmless (POST ack never true)
    prevAck = ack;

    KirinRecordDisplay observedRecord {};
    if (processorRef.pollRecordDisplay (observedRecord))
    {
        cachedRecordDisplay = observedRecord;
        haveRecordDisplay = true;
    }
    const uint8_t recordPhase = haveRecordDisplay
                                  ? cachedRecordDisplay.phase
                                  : (uint8_t) KIRIN_RECORD_DISPLAY_WATCH;
    const bool displayRecord = rec || recordPhase != KIRIN_RECORD_DISPLAY_WATCH;
    const bool finalUnavailable = recordPhase == KIRIN_RECORD_DISPLAY_UNAVAILABLE;
    const bool useShortTerm = processorRef.useShortTermLoudness();
    loudnessSelector.setShortTerm (useShortTerm);

    // B-128 (G-115-371 D3): restore identity anomaly を drain して 5s latch。
    const juce::String anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty()) { pathAnomalyText = anomaly; pathAnomalyUntil = t + 5.0; }
    const bool anomalyActive = (t < pathAnomalyUntil) && pathAnomalyText.isNotEmpty();

    const juce::String keepNotice = processorRef.drainKeepActionNotice();
    if (keepNotice.isNotEmpty())
    {
        toastText = keepNotice;
        toastUntil = t + 3.0;
    }

    // The shared slot keeps persistent errors distinct from direct user-action toasts without
    // letting independent labels overlap at the bottom of the fixed-size editor.
    const juce::String recErr = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText
                              : recErr.isNotEmpty() ? recErr
                              : finalUnavailable ? juce::String ("Final measurement unavailable")
                              : preparing ? juce::String ("Preparing pairs...")
                              : armed ? juce::String ("Ready to bounce")
                              : juce::String();
    updateFeedback (t, t < bannerUntil, status);

    postControls->update (keepActive, processorRef.licenseCode(),
                          pairStatus != KIRIN_PAIR_STATUS_UNPAIRED);

   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (refreshAnalysisViews (alive, sig, rec, armed, ack, preset, pairStatus))
        return;
   #endif

    // ── display-branch tree: Record uses one generation-bound presentation snapshot, while
    //    paired Watch keeps the delta grid through short PRE idle/stale gaps.
    KirinMeasureResult m {};
    bool haveM = false;
    bool mutedM = false;
    KirinSessionSummary summary {};
    bool haveSummary = false;
    KirinWatchDisplay watch {};
    const bool polledWatch = processorRef.pollWatchDisplay (watch);
    if (polledWatch)
    {
        observatoryWatchDisplay = watch;
        haveObservatoryWatchDisplay = true;
        watchMaximum = watch.maximum;
        haveWatchMaximum = true;
    }
    const bool haveWatch = polledWatch && sig == KIRIN_SIGNAL_STATE_ACTIVE;
    if (displayRecord)
    {
        if (haveRecordDisplay && cachedRecordDisplay.has_measure != 0)
        {
            const auto raw = cachedRecordDisplay.measure;
            m = recordPhase == KIRIN_RECORD_DISPLAY_LIVE
                    && sig == KIRIN_SIGNAL_STATE_ACTIVE
                  ? displaySmoother.smoothMeasure (raw, t)
                  : raw;
            haveM = true;
        }
        if (haveRecordDisplay && cachedRecordDisplay.has_session != 0)
        {
            summary = cachedRecordDisplay.session;
            haveSummary = true;
        }
    }
    else if (haveWatch)
    {
        m = displaySmoother.smoothMeasure (watch.current, t);
        haveM = true;
    }
    else if (sig == KIRIN_SIGNAL_STATE_INACTIVE)
    {
        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        if (displaySmoother.heldMeasureDisplay (held, t))
        {
            m = held.value;
            haveM = true;
            mutedM = held.muted;
        }
    }
    else if (! displayRecord && sig == KIRIN_SIGNAL_STATE_BYPASSED)
    {
        displaySmoother.reset();
        haveObservatoryWatchDisplay = false;
        haveWatchMaximum = false;
    }
    const bool tpWarn = ! mutedM && hypha::tpOver (haveM ? m.true_peak : kNaN);
    bool watchHeldNormal = false;
    const auto selectedMeasure = [useShortTerm] (const KirinMeasureResult& value)
    {
        return useShortTerm ? value.lufs_s : value.lufs_m;
    };
    const auto selectedDelta = [useShortTerm] (const KirinDelta& value)
    {
        return useShortTerm ? value.lufs_s : value.lufs;
    };

    if (displayRecord)
    {
        KirinDelta d {};
        const bool haveD = haveRecordDisplay && cachedRecordDisplay.has_delta != 0;
        if (haveD)
            d = cachedRecordDisplay.delta;
        const bool mutedD = recordPhase == KIRIN_RECORD_DISPLAY_LIVE
                         && haveD && display::deltaIsStale (d.mode);
        const bool recordPairSelected = display::recordPairContext (
            rec, pairSelected, haveD,
            haveRecordDisplay && cachedRecordDisplay.pair_matches_current != 0);

        if ((postAbsolute ? display::MetricMode::absolute
                          : display::recordMetricMode (recordPairSelected, haveD, d.mode))
            == display::MetricMode::absolute)
        {
            if (currentKind != Kind::Abs6) configureForKind (Kind::Abs6);
            auto V = [&] (double x) { return haveM ? x : kNaN; };
            fillAbs (0, V (selectedMeasure (m)), false, mutedM);
            fillAbs (1, V (m.psr), false, mutedM);
            fillAbs (2, haveSummary ? summary.max_true_peak : kNaN, true, mutedM);
            fillAbs (3, haveSummary ? summary.lufs_i : kNaN, false, mutedM);
            fillAbs (4, V (m.crest), false, mutedM);
            fillAbs (5, V (m.sharpness), false, mutedM);
        }
        else
        {
            if (currentKind != Kind::Delta6) configureForKind (Kind::Delta6);
            const juce::Colour base = mutedD ? COL_MUTED : COL_NORMAL;
            auto D = [&] (double x) { return haveD ? x : kNaN; };
            fillDelta (0, D (selectedDelta (d)), false, base, false, mutedD);
            fillDelta (1, D (d.psr),             false, base, false, mutedD);
            fillAbs   (2, haveSummary ? summary.max_true_peak : kNaN, true, mutedM);
            fillAbs   (3, haveSummary ? summary.lufs_i : kNaN, false, mutedM);
            fillDelta (4, D (d.crest),           false, base, false, mutedD);
            fillDelta (5, D (d.sharpness),       false, base, false, mutedD);
        }
    }
    else if (sig != KIRIN_SIGNAL_STATE_ACTIVE) // Bypassed / Inactive -> "---"
    {
        KirinDelta heldD {};
        bool haveHeldD = false;
        bool mutedHeldD = false;
        if (sig == KIRIN_SIGNAL_STATE_INACTIVE && pairSelected)
        {
            hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
            if (displaySmoother.heldDeltaDisplay (held, t))
            {
                heldD = held.value;
                haveHeldD = true;
                mutedHeldD = held.muted;
            }
        }
        if (pairSelected)
        {
            if (currentKind != Kind::WatchDelta6) configureForKind (Kind::WatchDelta6);
            const bool unavailable = ! haveHeldD;
            const juce::Colour base = mutedHeldD || unavailable ? COL_MUTED : COL_NORMAL;
            watchHeldNormal = haveHeldD && ! mutedHeldD;
            fillDelta (0, haveHeldD ? selectedDelta (heldD) : kNaN,
                       false, base, false, mutedHeldD || unavailable);
            fillAbs (1, haveWatchMaximum ? selectedMeasure (watchMaximum) : kNaN, false, true);
            fillDelta (2, haveHeldD ? heldD.true_peak : kNaN,
                       true, base, false, mutedHeldD || unavailable);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, true);
            fillDelta (4, haveHeldD ? heldD.crest : kNaN,
                       false, base, false, mutedHeldD || unavailable);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, true);
        }
        else
        {
            if (currentKind != Kind::WatchAbs6) configureForKind (Kind::WatchAbs6);
            watchHeldNormal = haveM && ! mutedM;
            fillAbs (0, haveM ? selectedMeasure (m) : kNaN, false, mutedM);
            fillAbs (1, haveWatchMaximum ? selectedMeasure (watchMaximum) : kNaN, false, true);
            fillAbs (2, haveM ? m.true_peak : kNaN, true, mutedM);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, true);
            fillAbs (4, haveM ? m.crest : kNaN, false, mutedM);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, true);
        }
    }
    else // Active + Watch
    {
        KirinDelta rawD = observedDelta;
        KirinDelta d {};
        const bool haveRawD = haveObservedDelta;
        const bool preUnavailable = haveRawD && display::preUnavailableForDelta (rawD.mode);
        bool haveD = false;
        bool mutedD = false;
        if (haveRawD && display::deltaIsActive (rawD.mode))
        {
            d = displaySmoother.smoothDelta (rawD, t);
            haveD = true;
        }
        else if (! preUnavailable && pairSelected)
        {
            hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
            if (displaySmoother.heldDeltaDisplay (held, t))
            {
                d = held.value;
                haveD = true;
                mutedD = held.muted;
            }
        }
        else if (haveRawD)
        {
            d = rawD;
            haveD = true;
            mutedD = true;
        }

        const bool effectiveHaveD = haveRawD || postAbsolute;
        const uint8_t effectiveMode = haveRawD ? rawD.mode
                                               : (uint8_t) KIRIN_DELTA_MODE_BYPASSED;
        if (display::watchMetricMode (pairSelected, effectiveHaveD, effectiveMode)
            == display::MetricMode::delta)
        {
            if (currentKind != Kind::WatchDelta6) configureForKind (Kind::WatchDelta6);
            const bool liveDelta = haveD && display::deltaIsActive (d.mode) && ! mutedD;
            const juce::Colour base = liveDelta ? COL_NORMAL : COL_MUTED;
            const bool warn = liveDelta ? tpWarn : false;
            fillDelta (0, haveD ? selectedDelta (d) : kNaN, false, base, warn, ! liveDelta);
            fillAbs (1, haveWatchMaximum ? selectedMeasure (watchMaximum) : kNaN, false, ! liveDelta);
            fillDelta (2, haveD ? d.true_peak : kNaN, true,  base, warn, ! liveDelta);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, ! liveDelta);
            fillDelta (4, haveD ? d.crest     : kNaN, false, base, warn, ! liveDelta);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, ! liveDelta);
        }
        else // no selected pair, or paired PRE explicitly bypassed -> POST absolute
        {
            if (currentKind != Kind::WatchAbs6) configureForKind (Kind::WatchAbs6);
            auto V = [&] (double x) { return haveM ? x : kNaN; };
            fillAbs (0, V (selectedMeasure (m)), false);
            fillAbs (1, haveWatchMaximum ? selectedMeasure (watchMaximum) : kNaN, false);
            fillAbs (2, V (m.true_peak), true);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true);
            fillAbs (4, V (m.crest), false);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false);
        }
    }

    const int ledSig = (! displayRecord && sig == KIRIN_SIGNAL_STATE_INACTIVE && watchHeldNormal)
        ? KIRIN_SIGNAL_STATE_ACTIVE : sig;
    // PREPARING is intentionally not shown as an active Record light. The producer becomes
    // user-ready only after every generation member has crossed the shared Armed barrier.
    led.setState (hypha::deriveLedState (alive, ledSig, rec && armed, ack, preset));
}
