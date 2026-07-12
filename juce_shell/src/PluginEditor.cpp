#include "PluginEditor.h"

#include <cmath>
#include <limits>

using hypha::COL_FLORA;
using hypha::COL_FLORA_BR;
using hypha::COL_MUTED;
using hypha::COL_NORMAL;

namespace
{
    constexpr int   kMargin   = 10;  // egui add_space(10) left/right
    constexpr int   kTopSpace = 8;   // egui add_space(8)
    constexpr int   kTitleH   = 26;  // title row (font 20)
    constexpr int   kLedPx    = 12;  // led 12×12 box (radius 5 circle)
    constexpr int   kMetricH  = 70;  // metric area reserve (3 rows of pitch 24)
    const double    kNaN = std::numeric_limits<double>::quiet_NaN();

    juce::String allKeepMenuLabel (int nReady)
    {
        return juce::String ("All Keep: ") + juce::String (nReady) + " ready POST"
             + (nReady == 1 ? "" : "s");
    }

    bool claimedByOtherPost (const juce::String& name,
                             const juce::String& ownInstanceId,
                             const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& c : claims)
            if (c.hasPairPreName && c.pairPreName == name && c.instanceId != ownInstanceId)
                return true;
        return false;
    }

    bool hasDuplicateCandidateName (const juce::String& name,
                                    const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates)
    {
        int matches = 0;
        for (const auto& c : candidates)
            if (c.hasName && c.name.isNotEmpty() && c.name == name && ++matches > 1)
                return true;
        return false;
    }
}

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), isPost (p.isPostRole())
{
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setSize (300, 200); // egui EguiState::from_size(300, 200) — identical for PRE and POST

    addAndMakeVisible (led);
    for (auto& c : cells)
        addAndMakeVisible (c);

    addAndMakeVisible (nameField);
    nameField.onCommit = [this] (const juce::String& n)
    {
        if (isPost) processorRef.setPairName (n);
        else        processorRef.setPreName (n);
    };

    if (isPost)
    {
        nameField.setPrefix ("pair: ");
        nameField.setFallback ("___");
        nameField.setLockedTooltip (juce::CharPointer_UTF8 ("Pair selection is locked during playback"));
        nameField.setModelName (processorRef.pairName());

        pairRecLabel.setFont (hypha::monoFont (11.0f));
        pairRecLabel.setColour (juce::Label::textColourId, COL_MUTED);
        pairRecLabel.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (pairRecLabel);

        postControls = std::make_unique<hypha::PostControls>();
        addAndMakeVisible (*postControls);
        postControls->onKeep = [this] {
            if (processorRef.keepPair()) return;
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            // B-118 (①): keep 失敗 = 非Os か no-PRE（egui trigger_keep の LicenseDenied / None と同文言）。
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        };
        postControls->onStop = [this] { processorRef.stopPair(); };
        postControls->onNote = [this] (const juce::String& tag)
        {
            // B-111: addAnnotation の bool 戻り値を分岐（onKeep と同様）。失敗（非Os / 未 enable /
            // 追記先 .json 不在）時は成功偽装せず明示エラーを出す。egui 殻と parity。
            if (processorRef.addAnnotation (tag))
                showToast ("Note saved: " + tag);
            else
                showToast ("Note save failed");
        };
        postControls->onSenseHint = [this]
        {
            if (! juce::URL ("https://kirinmastering.com").launchInDefaultBrowser())
                showToast ("Could not open browser");
        };

        // B-102: ▼ dropdown beside the pair field — All Keep / All Stop / candidate list.
        // The free-text pair field (nameField) is retained; this only adds the egui ComboBox.
        pairDropdown.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x25BC)); // ▼
        pairDropdown.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        pairDropdown.setColour (juce::TextButton::textColourOnId,  COL_FLORA);
        pairDropdown.setColour (juce::TextButton::textColourOffId, COL_FLORA);
        pairDropdown.onClick = [this] { showCandidateMenu(); };
        addAndMakeVisible (pairDropdown);
    }
    else
    {
        nameField.setModelName (processorRef.preName());
        nameField.setFallback (instanceId8());
    }

    bannerLabel.setFont (hypha::monoFont (13.0f));
    bannerLabel.setColour (juce::Label::textColourId, COL_FLORA);
    bannerLabel.setText ("Keeping", juce::dontSendNotification);
    bannerLabel.setJustificationType (juce::Justification::centredLeft);
    bannerLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (bannerLabel);

    toastLabel.setFont (hypha::monoFont (11.0f));
    toastLabel.setColour (juce::Label::textColourId, COL_MUTED);
    toastLabel.setJustificationType (juce::Justification::centredLeft);
    toastLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (toastLabel);

    // B-118 (③): io_thread 連続失敗の永続 status label（toast とは別・R-26 で文言ありの間表示）。
    recordErrorLabel.setFont (hypha::monoFont (11.0f));
    recordErrorLabel.setColour (juce::Label::textColourId, COL_MUTED);
    recordErrorLabel.setJustificationType (juce::Justification::centredLeft);
    recordErrorLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (recordErrorLabel);

    configureForKind (Kind::WatchAbs6); // current | MAX, three rows
    resized();                     // finalise positions now that postControls exists
    startTimerHz (30);             // smooth LED breathing (egui repaints at 10–30Hz)
}

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
}

juce::String KirinHyphaEditor::instanceId8() const
{
    return processorRef.instanceId().substring (0, 8);
}

void KirinHyphaEditor::paint (juce::Graphics& g)
{
    bg.draw (g, getLocalBounds()); // mycelium PNG over BG (R-12: pure chrome)

    g.setColour (COL_NORMAL);
    g.setFont (hypha::labelFont (20.0f));
    g.drawText (isPost ? "POST" : "PRE", titleArea, juce::Justification::centredLeft);

    // flora line (#d4a043, 1px) — the mycelium tip joining Hypha to Kirin OS.
    g.setColour (COL_FLORA);
    g.fillRect ((float) kMargin, (float) floraY, (float) (getWidth() - 2 * kMargin), 1.0f);
}

void KirinHyphaEditor::resized()
{
    const int w = getWidth();
    titleArea = { kMargin, kTopSpace, 58, kTitleH };
    led.setBounds (w - kMargin - kLedPx, kTopSpace + (kTitleH - kLedPx) / 2, kLedPx, kLedPx);

    const int fieldLeft  = kMargin + 58 + 6;            // after the title text
    const int fieldRight = w - kMargin - kLedPx - 6;    // before the LED

    if (isPost)
    {
        pairRecLabel.setBounds (fieldLeft, kTopSpace, juce::jmax (0, fieldRight - fieldLeft), kTitleH);
        const int nameY = kTopSpace + kTitleH + 4;      // 38
        const int ddW = 22;                             // ▼ dropdown width (B-102)
        nameField.setBounds (kMargin, nameY, w - 2 * kMargin - ddW - 4, 22);
        pairDropdown.setBounds (w - kMargin - ddW, nameY, ddW, 22);
        floraY    = nameY + 22 + 4;                     // 64 (B-118 追補 2-2: POST を 6px 引上げ recordErrorLabel を 200px 内へ)
        metricTop = floraY + 1 + 4;                     // 69
    }
    else
    {
        nameField.setBounds (fieldLeft, kTopSpace, juce::jmax (0, fieldRight - fieldLeft), kTitleH);
        floraY    = kTopSpace + kTitleH + 4;            // 38
        metricTop = floraY + 1 + 6;                     // 45
    }

    layoutMetrics (currentSix);

    const int afterMetric = metricTop + kMetricH;
    int bannerY = afterMetric + 4;
    if (isPost && postControls != nullptr)
    {
        postControls->setBounds (kMargin, afterMetric + 4, w - 2 * kMargin, 26);
        bannerY = afterMetric + 4 + 26 + 1;             // B-118 追補 2-2: +1 gap（recordErrorLabel 行ぶん詰め）
    }
    bannerLabel.setBounds (kMargin, bannerY, w - 2 * kMargin, 16);
    toastLabel .setBounds (kMargin, bannerY, w - 2 * kMargin, 16); // same slot (per-role only one shows)
    // B-118 (③/追補 2-2): 永続 status row（toast の直下）。h14 で POST=186..200 / PRE=135..149 に収め画面外を解消。
    recordErrorLabel.setBounds (kMargin, bannerY + 16, w - 2 * kMargin, 14);
}

void KirinHyphaEditor::layoutMetrics (bool six)
{
    const int areaW = getWidth() - 2 * kMargin;
    const int rowH = 22, pitch = 24;

    if (! six)
    {
        for (int i = 0; i < 3; ++i)
            cells[(size_t) i].setBounds (kMargin, metricTop + i * pitch, areaW, rowH);
    }
    else
    {
        const int gap = 6;
        const int cw  = (areaW - gap) / 2;
        for (int i = 0; i < 6; ++i)
        {
            const int r = i / 2, c = i % 2;
            cells[(size_t) i].setBounds (kMargin + c * (cw + gap), metricTop + r * pitch, cw, rowH);
        }
    }
}

void KirinHyphaEditor::configureForKind (Kind k)
{
    const bool watch = (k == Kind::WatchAbs6 || k == Kind::WatchDelta6);
    const bool six = true;
    const bool dlt = (k == Kind::WatchDelta6 || k == Kind::Delta6);

    const float lblSz = six ? 11.0f : 13.0f;
    const float valSz = six ? 14.0f : 16.0f;
    const float unitSz = six ? 10.0f : 11.0f;
    const float minCol = six ? 40.0f : 58.0f;
    const juce::String d = hypha::delta();

    auto cfg = [&] (int i, const juce::String& label, const juce::String& unit, const juce::String& help)
    {
        cells[(size_t) i].configure (label, unit, help, lblSz, valSz, unitSz, minCol);
        cells[(size_t) i].setVisible (true);
    };

    if (watch)
    {
        cfg (0, dlt ? d + "LUFS"  : juce::String ("LUFS-M"), dlt ? "LU" : "LUFS", hypha::helpLufsM());
        cfg (1, "MAX", "LUFS", hypha::helpLufsM());
        cfg (2, dlt ? d + "TP"    : juce::String ("TP"), dlt ? "dB" : "dBTP", hypha::helpTp());
        cfg (3, "MAX", "dBTP", hypha::helpTp());
        cfg (4, dlt ? d + "Crest" : juce::String ("Crest"), "dB", hypha::helpCrest());
        cfg (5, "MAX", "dB", hypha::helpCrest());
    }
    else
    {
        // Record 2×3: row0 LUFS|PSR, row1 TP|N, row2 Crest|Sharp.
        cfg (0, dlt ? d + "LUFS"  : juce::String ("LUFS-M"), dlt ? "LU" : "LUFS", hypha::helpLufsM());
        cfg (1, dlt ? d + "PSR"   : juce::String ("PSR"),    "dB",                hypha::helpPsr());
        cfg (2, dlt ? d + "TP"    : juce::String ("TP"),     dlt ? "dB" : "dBTP", hypha::helpTp());
        cfg (3, dlt ? d + "N"     : juce::String ("N"),      "sone",              hypha::helpN());
        cfg (4, dlt ? d + "Crest" : juce::String ("Crest"),  "dB",                hypha::helpCrest());
        cfg (5, dlt ? d + "Sharp" : juce::String ("Sharp"),  "acum",              hypha::helpSharp());
    }
    currentKind = k;
    currentSix  = six;
    layoutMetrics (six);
}

void KirinHyphaEditor::fillAbs (int cell, double v, bool isTp, bool muted)
{
    const juce::Colour col = std::isnan (v) ? COL_MUTED
                                            : (muted ? COL_MUTED
                                                     : (isTp ? hypha::tpColour (v) : hypha::valColour (v)));
    cells[(size_t) cell].setValue (hypha::fmtVal (v), col);
}

void KirinHyphaEditor::fillDelta (int cell, double v, bool isTp, juce::Colour deltaBase, bool tpWarn, bool muted)
{
    const juce::Colour col = std::isnan (v) ? COL_MUTED
                                            : (muted ? COL_MUTED
                                                     : ((isTp && tpWarn) ? COL_FLORA_BR : deltaBase));
    cells[(size_t) cell].setValue (hypha::fmtDelta (v), col);
}

void KirinHyphaEditor::showToast (const juce::String& msg)
{
    toastUntil = nowSecs() + 3.0; // TOAST_DURATION_SECS
    toastLabel.setText (msg, juce::dontSendNotification);
    toastLabel.setVisible (true);
}

void KirinHyphaEditor::showCandidateMenu()
{
    // B-102: egui draw_pair_pre_combo parity (scope = new↔new). Built on click (no per-tick FFI):
    //   [All Keep: N ready POST(s)] (Watch, N>=1) / [All Stop: recording POSTs] (Record) /
    //   candidate rows ("Can Keep/Keep ready/In use/Duplicate: name").
    // select_target_pre matches by name, so only named candidates are offered (egui skips empty).
    const bool rec = processorRef.isRecording();
    const bool playing = processorRef.isPlaying(); // W-280: pair change locked during playback
    // B-115: lock only when playing AND live (processBlock running). A frozen `playing` with a
    // stalled heartbeat does not lock (false-release prevention; signal_state is silence-conflated).
    const bool pairLocked = playing && processorRef.heartbeatLive();
    const auto cands = processorRef.enumeratePreCandidates();
    const auto claims = processorRef.enumeratePostPairClaims();
    const juce::String currentPairName = processorRef.pairName();
    const juce::String ownInstanceId = processorRef.instanceId();

    menuCandidateNames.clearQuick();
    juce::StringArray labels;
    juce::Array<bool> labelEnabled;
    juce::Array<bool> labelChecked;
    for (const auto& c : cands)
        if (c.hasName && c.name.isNotEmpty())
        {
            menuCandidateNames.add (c.name);
            const bool keepReady = (c.name == currentPairName);
            const bool inUse = claimedByOtherPost (c.name, ownInstanceId, claims);
            const bool duplicateName = hasDuplicateCandidateName (c.name, cands);
            const juce::String prefix = duplicateName ? "Duplicate: "
                                      : (inUse ? "In use: " : (keepReady ? "Keep ready: " : "Can Keep: "));
            labels.add (prefix + c.name);
            labelEnabled.add (! duplicateName && ! inUse);
            labelChecked.add (! duplicateName && keepReady && ! inUse);
        }

    // egui parity: "N ready" = pair-set POST instances (keepReadyCount), NOT the PRE candidate
    // count — the All Keep broadcast acts on POSTs (hypha_post editor.rs:938-944). Candidate rows
    // below are the PRE list (menuCandidateNames), matching egui's separate pre_candidates source.
    const int nReady = processorRef.keepReadyCount();
    juce::PopupMenu menu;
    if (! rec && processorRef.licenseIsOs() && nReady >= 1)
        menu.addItem (1, allKeepMenuLabel (nReady));
    if (rec)
        menu.addItem (2, "All Stop: recording POSTs");
    if (menu.getNumItems() > 0)
        menu.addSeparator();
    menu.addItem (4, "Pair choices (not Keep targets)", false, false);
    if (menuCandidateNames.isEmpty())
        menu.addItem (3, "No pair choices", false, false); // disabled (R-26: silent when nothing)
    else
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem (100 + i, labels[i], ! pairLocked && labelEnabled[i], labelChecked[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pairDropdown),
                        [this] (int result) { handleCandidateMenu (result); });
}

void KirinHyphaEditor::handleCandidateMenu (int result)
{
    if (result == 1)
    {
        if (! processorRef.keepAll())
        {
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 2)
        processorRef.stopAll();
    else if (result >= 100)
    {
        const int idx = result - 100;
        if (idx >= 0 && idx < menuCandidateNames.size())
        {
            const juce::String name = menuCandidateNames[idx];
            processorRef.setPairName (name);     // -> kirin_hypha_set_pair_target
            nameField.setModelName (name);       // reflect immediately in the field
        }
    }
}

void KirinHyphaEditor::timerCallback()
{
    if (isPost) updatePost();
    else        updatePre();

    led.repaint(); // breathing animation (state colour evolves with the monotonic clock)
}

void KirinHyphaEditor::updatePre()
{
    const bool alive  = processorRef.measureAlive();
    const int  sig    = processorRef.signalStateLive(); // 0=Inactive 1=Active 2=Bypassed (B-113: heartbeat-aware)
    const bool rec    = processorRef.isRecording();    // record_sm (PRE autonomous record too)
    const bool ack    = processorRef.recordAcknowledged();
    const bool preset = processorRef.presetAvailable(); // PRE: always false

    nameField.setModelName (processorRef.preName());
    nameField.setFallback (instanceId8());

    // Keeping banner: 3s on the false→true ack edge (PRE acked a POST's record signal).
    const double t = nowSecs();
    if (ack && ! prevAck)
        bannerUntil = t + 3.0; // RECORD_BANNER_DURATION_SECS
    prevAck = ack;
    bannerLabel.setVisible (t < bannerUntil);

    // B-128 (G-115-371 D3): restore identity anomaly を drain して 5s latch（toast 相当の寿命）。
    const juce::String anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty()) { pathAnomalyText = anomaly; pathAnomalyUntil = t + 5.0; }
    const bool anomalyActive = (t < pathAnomalyUntil) && pathAnomalyText.isNotEmpty();

    // B-118 追補 (2-2): PRE 殻も io_thread 連続失敗の固定文言を永続表示（egui hypha_pre editor.rs:278
    // と parity / R-26: 文言ありの間のみ・"Keeping" banner の下）。従来 updatePre は recordErrorLabel を
    // refresh しておらず PRE では不可視だった（POST のみ wired のバグ）。anomaly 在中は anomaly を優先表示。
    const juce::String recErr = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText : recErr;
    recordErrorLabel.setVisible (status.isNotEmpty());
    if (status.isNotEmpty())
        recordErrorLabel.setText (status, juce::dontSendNotification);

    const Kind want = rec ? Kind::Abs6 : Kind::WatchAbs6;
    if (want != currentKind)
        configureForKind (want);

    KirinMeasureResult raw {};
    KirinMeasureResult r {};
    bool have = false;
    bool muted = false;
    KirinWatchDisplay watch {};
    const bool haveWatch = ! rec && sig == 1 && processorRef.pollWatchDisplay (watch);
    if (sig == 1 && (haveWatch || (rec && processorRef.pollMeasureResult (raw))))
    {
        if (haveWatch)
        {
            raw = watch.current;
            watchMaximum = watch.maximum;
            haveWatchMaximum = true;
        }
        r = displaySmoother.smoothMeasure (raw, t);
        have = true;
    }
    else if (sig == 0)
    {
        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        if (displaySmoother.heldMeasureDisplay (held, t))
        {
            r = held.value;
            have = true;
            muted = held.muted;
        }
    }
    else if (sig == 2)
    {
        displaySmoother.reset();
        haveWatchMaximum = false;
    }
    auto V = [&] (double x) { return have ? x : kNaN; };

    const int ledSig = (! rec && sig == 0 && have && ! muted) ? 1 : sig;
    led.setState (hypha::deriveLedState (alive, ledSig, rec, ack, preset));

    if (rec)
    {
        fillAbs (0, V (r.lufs_m), false, muted);
        fillAbs (1, V (r.psr), false, muted);
        fillAbs (2, V (r.true_peak), true, muted);
        fillAbs (3, V (r.n_prime_total), false, muted);
        fillAbs (4, V (r.crest), false, muted);
        fillAbs (5, V (r.sharpness), false, muted);
    }
    else
    {
        fillAbs (0, V (r.lufs_m), false, muted);
        fillAbs (1, haveWatchMaximum ? watchMaximum.lufs_m : kNaN, false, muted);
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
    const bool ack    = processorRef.recordAcknowledged(); // POST: always false (egui parity)
    const bool preset = processorRef.presetAvailable();
    const bool playing = processorRef.isPlaying();
    // B-115: lock only when playing AND live (processBlock running) — false-release prevention.
    const bool pairLocked = playing && processorRef.heartbeatLive();

    const juce::String pairName = processorRef.pairName();
    const bool pairNonEmpty = pairName.isNotEmpty();

    nameField.setModelName (pairName);
    nameField.setEditingEnabled (! pairLocked); // W-280 + B-115 playback pair lock (playing AND live)

    pairRecLabel.setVisible (rec);
    if (rec)
        pairRecLabel.setText ("pair: " + (pairNonEmpty ? pairName : instanceId8()),
                              juce::dontSendNotification);

    const double t = nowSecs();
    if (ack && ! prevAck) bannerUntil = t + 3.0; // harmless (POST ack never true)
    prevAck = ack;
    bannerLabel.setVisible (t < bannerUntil);
    toastLabel.setVisible (t < toastUntil);

    // B-128 (G-115-371 D3): restore identity anomaly を drain して 5s latch。
    const juce::String anomaly = processorRef.pathAnomalyMessage();
    if (anomaly.isNotEmpty()) { pathAnomalyText = anomaly; pathAnomalyUntil = t + 5.0; }
    const bool anomalyActive = (t < pathAnomalyUntil) && pathAnomalyText.isNotEmpty();

    // B-118 (③): io_thread 連続失敗の固定文言を永続表示（R-26: 文言ありの間のみ表示・toast とは独立寿命）。
    const juce::String recErr = processorRef.recordErrorMessage();
    const juce::String status = anomalyActive ? pathAnomalyText : recErr;
    recordErrorLabel.setVisible (status.isNotEmpty());
    if (status.isNotEmpty())
        recordErrorLabel.setText (status, juce::dontSendNotification);

    postControls->update (rec, processorRef.licenseCode(), pairNonEmpty);

    // ── display-branch tree: raw Record/TRACE stays untouched, but paired Watch keeps the
    //    delta grid through short PRE idle/stale gaps so a latched pair does not look released.
    KirinMeasureResult rawM {};
    KirinMeasureResult m {};
    bool haveM = false;
    bool mutedM = false;
    KirinWatchDisplay watch {};
    const bool haveWatch = ! rec && sig == 1 && processorRef.pollWatchDisplay (watch);
    if (sig == 1 && (haveWatch || (rec && processorRef.pollMeasureResult (rawM))))
    {
        if (haveWatch)
        {
            rawM = watch.current;
            watchMaximum = watch.maximum;
            haveWatchMaximum = true;
        }
        m = displaySmoother.smoothMeasure (rawM, t);
        haveM = true;
    }
    else if (sig == 0)
    {
        hypha::DisplaySmoother::HeldDisplay<KirinMeasureResult> held {};
        if (displaySmoother.heldMeasureDisplay (held, t))
        {
            m = held.value;
            haveM = true;
            mutedM = held.muted;
        }
    }
    else if (sig == 2)
    {
        displaySmoother.reset();
        haveWatchMaximum = false;
    }
    const bool tpWarn = ! mutedM && hypha::tpOver (haveM ? m.true_peak : kNaN);
    bool watchHeldNormal = false;

    if (rec) // Record owns the six-row layout even while the host is preparing/offline-stalling.
    {
        if (currentKind != Kind::Delta6) configureForKind (Kind::Delta6);
        KirinDelta rawD {};
        KirinDelta d {};
        bool haveD = false;
        bool mutedD = false;
        if (sig == 1 && processorRef.pollDelta (rawD))
        {
            const bool preExplicitBypassed = rawD.mode == 3;
            if (rawD.mode == 0)
            {
                d = displaySmoother.smoothDelta (rawD, t);
                haveD = true;
            }
            else if (! preExplicitBypassed && pairNonEmpty)
            {
                hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
                if (displaySmoother.heldDeltaDisplay (held, t))
                {
                    d = held.value;
                    haveD = true;
                    mutedD = held.muted;
                }
                else
                {
                    d = rawD;
                    haveD = true;
                    mutedD = true;
                }
            }
            else
            {
                d = rawD;
                haveD = true;
                mutedD = true;
            }
        }
        else if (sig == 0)
        {
            hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
            if (displaySmoother.heldDeltaDisplay (held, t))
            {
                d = held.value;
                haveD = true;
                mutedD = held.muted;
            }
        }
        const juce::Colour base = (mutedD || (haveD && d.mode == 1)) ? COL_MUTED : COL_NORMAL;
        auto D = [&] (double x) { return haveD ? x : kNaN; };
        fillDelta (0, D (d.lufs),          false, base, tpWarn, mutedD);
        fillDelta (1, D (d.psr),           false, base, tpWarn, mutedD);
        fillDelta (2, D (d.true_peak),     true,  base, tpWarn, mutedD);
        fillDelta (3, D (d.n_prime_total), false, base, tpWarn, mutedD);
        fillDelta (4, D (d.crest),         false, base, tpWarn, mutedD);
        fillDelta (5, D (d.sharpness),     false, base, tpWarn, mutedD);
    }
    else if (sig != 1) // Bypassed / Inactive -> "---"
    {
        KirinDelta heldD {};
        bool haveHeldD = false;
        bool mutedHeldD = false;
        if (sig == 0 && pairNonEmpty)
        {
            hypha::DisplaySmoother::HeldDisplay<KirinDelta> held {};
            if (displaySmoother.heldDeltaDisplay (held, t))
            {
                heldD = held.value;
                haveHeldD = true;
                mutedHeldD = held.muted;
            }
        }
        if (haveHeldD)
        {
            if (currentKind != Kind::WatchDelta6) configureForKind (Kind::WatchDelta6);
            const juce::Colour base = mutedHeldD ? COL_MUTED : COL_NORMAL;
            watchHeldNormal = ! mutedHeldD;
            fillDelta (0, heldD.lufs,      false, base, false, mutedHeldD);
            fillAbs (1, haveWatchMaximum ? watchMaximum.lufs_m : kNaN, false, true);
            fillDelta (2, heldD.true_peak, true,  base, false, mutedHeldD);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, true);
            fillDelta (4, heldD.crest,     false, base, false, mutedHeldD);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, true);
        }
        else
        {
            if (currentKind != Kind::WatchAbs6) configureForKind (Kind::WatchAbs6);
            watchHeldNormal = haveM && ! mutedM;
            fillAbs (0, haveM ? m.lufs_m : kNaN, false, mutedM);
            fillAbs (1, haveWatchMaximum ? watchMaximum.lufs_m : kNaN, false, true);
            fillAbs (2, haveM ? m.true_peak : kNaN, true, mutedM);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, true);
            fillAbs (4, haveM ? m.crest : kNaN, false, mutedM);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, true);
        }
    }
    else // Active + Watch
    {
        KirinDelta rawD {};
        KirinDelta d {};
        const bool haveRawD = processorRef.pollDelta (rawD);
        const bool preExplicitBypassed = haveRawD && rawD.mode == 3;
        bool haveD = false;
        bool mutedD = false;
        if (haveRawD && rawD.mode == 0)
        {
            d = displaySmoother.smoothDelta (rawD, t);
            haveD = true;
        }
        else if (! preExplicitBypassed && pairNonEmpty)
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

        if (pairNonEmpty && ! preExplicitBypassed)
        {
            if (currentKind != Kind::WatchDelta6) configureForKind (Kind::WatchDelta6);
            const bool liveDelta = haveD && d.mode == 0 && ! mutedD;
            const juce::Colour base = liveDelta ? COL_NORMAL : COL_MUTED;
            const bool warn = liveDelta ? tpWarn : false;
            fillDelta (0, haveD ? d.lufs      : kNaN, false, base, warn, ! liveDelta);
            fillAbs (1, haveWatchMaximum ? watchMaximum.lufs_m : kNaN, false, ! liveDelta);
            fillDelta (2, haveD ? d.true_peak : kNaN, true,  base, warn, ! liveDelta);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true, ! liveDelta);
            fillDelta (4, haveD ? d.crest     : kNaN, false, base, warn, ! liveDelta);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false, ! liveDelta);
        }
        else // pair empty or PRE explicitly bypassed -> POST absolute
        {
            if (currentKind != Kind::WatchAbs6) configureForKind (Kind::WatchAbs6);
            auto V = [&] (double x) { return haveM ? x : kNaN; };
            fillAbs (0, V (m.lufs_m), false);
            fillAbs (1, haveWatchMaximum ? watchMaximum.lufs_m : kNaN, false);
            fillAbs (2, V (m.true_peak), true);
            fillAbs (3, haveWatchMaximum ? watchMaximum.true_peak : kNaN, true);
            fillAbs (4, V (m.crest), false);
            fillAbs (5, haveWatchMaximum ? watchMaximum.crest : kNaN, false);
        }
    }

    const int ledSig = (! rec && sig == 0 && watchHeldNormal) ? 1 : sig;
    led.setState (hypha::deriveLedState (alive, ledSig, rec, ack, preset));
}
