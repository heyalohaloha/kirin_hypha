#include "PluginEditor.h"
#include "HyphaDisplayContract.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
 #include "HyphaAttackUiContract.h"
#endif

#include <cmath>
#include <limits>

using hypha::COL_FLORA;
using hypha::COL_FLORA_BR;
using hypha::COL_MUTED;
using hypha::COL_NORMAL;
using hypha::COL_SPECTRUM_DELTA;
using hypha::COL_SPECTRUM_POST;

namespace
{
    namespace ui = hypha::ui_contract;
    namespace display = hypha::display_contract;
    const double    kNaN = std::numeric_limits<double>::quiet_NaN();

    juce::Rectangle<int> juceRect (ui::Rect rect)
    {
        return { rect.x, rect.y, rect.width, rect.height };
    }

    juce::String metricHelp (ui::Metric metric)
    {
        switch (metric)
        {
            case ui::Metric::lufs:      return hypha::helpLufsM();
            case ui::Metric::truePeak:  return hypha::helpTp();
            case ui::Metric::maxTruePeak:return hypha::helpTp();
            case ui::Metric::crest:     return hypha::helpCrest();
            case ui::Metric::psr:       return hypha::helpPsr();
            case ui::Metric::integrated:return hypha::helpLufsI();
            case ui::Metric::sharpness: return hypha::helpSharp();
        }
        return {};
    }

    juce::String allKeepMenuLabel (int nReady)
    {
        return juce::String ("All Keep: ") + juce::String (nReady) + " ready POST"
             + (nReady == 1 ? "" : "s");
    }

    bool claimedByOtherPost (const KirinHyphaProcessorBase::PreCandidate& candidate,
                             const juce::String& ownInstanceId,
                             const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& c : claims)
            if (c.instanceId != ownInstanceId
                && ((c.hasPairedPreInstanceId && c.pairedPreInstanceId == candidate.instanceId)
                    || (! c.hasPairedPreInstanceId && c.hasPairPreName && candidate.hasName
                        && candidate.name.isNotEmpty() && c.pairPreName == candidate.name)))
                return true;
        return false;
    }

    juce::String resolvedOwnPreInstanceId (
        const juce::String& ownInstanceId,
        const juce::String& latchedPreInstanceId,
        const juce::Array<KirinHyphaProcessorBase::PostPairClaim>& claims)
    {
        for (const auto& claim : claims)
            if (claim.instanceId == ownInstanceId && claim.hasPairedPreInstanceId)
                return claim.pairedPreInstanceId;
        return latchedPreInstanceId;
    }

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

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), isPost (p.isPostRole()),
      observatoryView (isPost ? hypha::observatory::Role::post
                              : hypha::observatory::Role::pre)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const bool openAttackAtLaunch = isPost
        && juce::SystemStats::getEnvironmentVariable (
            hypha::attack_ui::activationEnvironmentVariable, {})
               .trim() == hypha::attack_ui::activationValue;
   #endif
    tooltip.setLookAndFeel (&tooltipLookAndFeel);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    // One opaque Observatory root lets Windows present a completed frame instead of compositing
    // intermediate transformed children.
    scaleRoot.setOpaque (true);
    addAndMakeVisible (scaleRoot);
    observatorySizeIndex = juce::jmin (
        (size_t) processorRef.spectrumSizePreference(),
        hypha::observatory::sizePresets.size() - 1u);
    observatoryDomain = hypha::observatory::domainFromState (
        isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre,
        processorRef.observatoryDomainPreference());
    observatoryView.setTarget (hypha::observatory::targetFromState (
        isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre,
        processorRef.observatoryTargetPreference()));
    observatoryView.setTimeRange (hypha::observatory::timeRangeFromState (
        processorRef.observatoryTimeRangePreference()));
    configureMeterContext(); setResizable (true, false);
    setResizeLimits (300, 200, 900, 600);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (1.5);
    const auto storedEditorSize = hypha::observatory::unpackEditorSize (
        processorRef.observatoryEditorSizePreference());
    auto initialWidth = storedEditorSize.width;
    auto initialHeight = storedEditorSize.height;
    if (! hypha::observatory::validEditorSize (initialWidth, initialHeight))
    {
        const auto fallback = hypha::observatory::sizePresets[observatorySizeIndex];
        initialWidth = fallback.width;
        initialHeight = fallback.height;
    }
    setSize (initialWidth, initialHeight);
    observatoryView.onDomainChange = [this] (hypha::observatory::Domain domain)
    {
        setObservatoryDomain (domain);
    };
    observatoryView.onTargetChange = [this] (hypha::observatory::ObservationTarget target)
    {
        observatoryView.setTarget (target);
        processorRef.setObservatoryTargetPreference (hypha::observatory::stateValue (target));
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        spectrumView.setAbsoluteObservation (
            target == hypha::observatory::ObservationTarget::absolute);
       #endif
    };
    observatoryView.onTimeRangeChange = [this] (hypha::observatory::TimeRange range)
    {
        processorRef.setObservatoryTimeRangePreference (hypha::observatory::stateValue (range));
    };
    observatoryView.onSizeChange = [this] (hypha::observatory::SizePreset preset)
    {
        for (size_t index = 0; index < hypha::observatory::sizePresets.size(); ++index)
            if (hypha::observatory::sizePresets[index].width == preset.width)
            {
                observatorySizeIndex = index;
                processorRef.setSpectrumSizePreference ((uint8_t) index);
            }
        setSize (preset.width, preset.height);
    };
    observatoryView.onCapture = [this] { beginObservatoryCapture(); };
    scaleRoot.addAndMakeVisible (observatoryView);

    scaleRoot.addAndMakeVisible (led);
    for (auto& c : cells)
        scaleRoot.addAndMakeVisible (c);
    loudnessSelector.setShortTerm (processorRef.useShortTermLoudness());
    loudnessSelector.onChange = [this] (bool shortTerm)
    {
        processorRef.setUseShortTermLoudness (shortTerm);
        observatoryView.setShortTermLoudness (shortTerm);
        configureForKind (currentKind);
    };
    observatoryView.onLoudnessChange = [this] (bool shortTerm)
    {
        processorRef.setUseShortTermLoudness (shortTerm);
        loudnessSelector.setShortTerm (shortTerm);
        observatoryView.setShortTermLoudness (shortTerm);
    };
    scaleRoot.addAndMakeVisible (loudnessSelector);

    scaleRoot.addAndMakeVisible (nameField);
    nameField.onCommit = [this] (const juce::String& n)
    {
        if (isPost)
        {
            pairedPreExplicitlyBypassed = false;
            processorRef.setPairName (n);
            if (n.isEmpty()) nameField.setFallback ("___");
        }
        else        processorRef.setPreName (n);
    };

    pairStatusLabel.setFont (hypha::monoFont (ui::pairStatusFontHeight));
    pairStatusLabel.setJustificationType (juce::Justification::centredRight);
    pairStatusLabel.setInterceptsMouseClicks (true, false);
    scaleRoot.addAndMakeVisible (pairStatusLabel);

    if (isPost)
    {
        nameField.setPrefix ("PAIR ");
        nameField.setFallback ("___");
        nameField.setLockedTooltip (juce::CharPointer_UTF8 ("Pair selection is locked during playback"));
        nameField.setEnabledTooltip ("Click to edit the PRE pair name.");
        nameField.setModelName (processorRef.pairName());

        postControls = std::make_unique<hypha::PostControls>();
        scaleRoot.addAndMakeVisible (*postControls);
        postControls->onKeep = [this] {
            if (processorRef.keepPair()) return;
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            // B-118 (①): keep 失敗 = 非Os か no-PRE（egui trigger_keep の LicenseDenied / None と同文言）。
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        };
        postControls->onStop = [this] { processorRef.stopPair(); };
        postControls->onSenseHint = [this]
        {
            if (! juce::URL ("https://kirinmastering.com").launchInDefaultBrowser())
                showToast ("Could not open browser");
        };

        // B-102/B-492: vector-arrow dropdown beside the pair field — All Keep / All Stop / candidates.
        // The free-text pair field (nameField) is retained; this only adds the egui ComboBox.
        pairDropdown.setTitle ("Pair, Keep, and display menu");
        pairDropdown.setDescription (
            "Choose an exact PRE pair, control Keep, or change hover help");
        pairDropdown.setTooltip ("Pair, Keep, and display options.");
        pairDropdown.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        pairDropdown.setColour (juce::TextButton::textColourOnId,  COL_FLORA);
        pairDropdown.setColour (juce::TextButton::textColourOffId, COL_FLORA);
        pairDropdown.onClick = [this] { showCandidateMenu(); };
        scaleRoot.addAndMakeVisible (pairDropdown);

       #if ! KIRIN_HYPHA_PRE_DISPLAY
        observatorySizeIndex = juce::jmin (
            (size_t) processorRef.spectrumSizePreference(),
            ui::spectrumSizePresets.size() - 1u);
        spectrumToggle.setButtonText ("ANALYSIS");
        spectrumToggle.setTooltip (ui::spectrumTooltip (false));
        spectrumToggle.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        spectrumToggle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
        spectrumToggle.setColour (juce::TextButton::textColourOffId, COL_MUTED);
        spectrumToggle.onClick = [this]
        {
            setAnalysisPage (analysisPage == AnalysisPage::meters
                               ? AnalysisPage::attack : AnalysisPage::meters);
        };
        scaleRoot.addAndMakeVisible (spectrumToggle);
        timePageNavigation.onPageChange = [this] (AnalysisPage page)
        {
            if (observatoryDomain == hypha::observatory::Domain::time)
                setAnalysisPage (page);
        };
        spectrumSizeToggle.setTitle ("Spectrum size");
        spectrumSizeToggle.setDescription (
            "Cycle POST Analysis between 100, 125, 150, and 200 percent");
        spectrumSizeToggle.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        spectrumSizeToggle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
        spectrumSizeToggle.setColour (juce::TextButton::textColourOffId, COL_SPECTRUM_DELTA);
        spectrumSizeToggle.onClick = [this] { cycleSpectrumSize(); };
        spectrumView.onChannelModeChange = [this] (uint8_t channelMode)
        {
            return processorRef.setSpectrumChannelMode (channelMode);
        };
        perceptualView.onChannelModeChange = [this] (uint8_t channelMode)
        {
            return processorRef.setSpectrumChannelMode (channelMode);
        };
        updateSpectrumSizeControl();
        scaleRoot.addChildComponent (timePageNavigation);
        scaleRoot.addChildComponent (spectrumSizeToggle);
        scaleRoot.addChildComponent (spectrumView);
        scaleRoot.addChildComponent (perceptualView);
        scaleRoot.addChildComponent (absoluteView);
        scaleRoot.addChildComponent (attackView);
        configureReferenceAudition();
       #endif
    }
    else
    {
        nameField.setPrefix ("SOURCE ");
        nameField.setEnabledTooltip ("Click to edit this PRE name.");
        nameField.setModelName (processorRef.preName());
        nameField.setFallback (instanceId8());
    }

    // One role-independent slot owns feedback priority and the only bottom-row rectangle.
    feedbackLabel.setFont (hypha::monoFont (ui::feedbackFontHeight));
    feedbackLabel.setJustificationType (juce::Justification::centredLeft);
    feedbackLabel.setMinimumHorizontalScale (1.0f);
    feedbackLabel.setInterceptsMouseClicks (false, false);
    scaleRoot.addChildComponent (feedbackLabel);

    guideConnectButton.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
    guideConnectButton.setColour (juce::TextButton::buttonOnColourId, hypha::kFieldFill);
    guideConnectButton.setColour (juce::TextButton::textColourOffId, COL_FLORA_BR);
    guideConnectButton.setColour (juce::TextButton::textColourOnId, COL_FLORA_BR);
    guideConnectButton.onClick = [this]
    {
        if (! processorRef.acceptPreDisplayConnection())
            showToast ("Connection request is no longer available");
    };
    scaleRoot.addChildComponent (guideConnectButton);

    configureForKind (Kind::WatchAbs6); // retained display compatibility; Observatory owns chrome
    for (auto& cell : cells)
        cell.setVisible (false);
    loudnessSelector.setVisible (false);
    pairStatusLabel.setVisible (false);
    if (postControls != nullptr)
        postControls->setVisible (false);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    spectrumToggle.setVisible (false);
   #endif
    setObservatoryDomain (observatoryDomain);
    resized();
    refreshObservatory();
    // Producer JSON and meter snapshots advance at 100 ms. Spectrum raises this same timer to
    // 30 Hz only while its exact-pair exchange is active; every normal meter stays at 10 Hz.
    startTimerHz (ui::preDisplayPresentationHz);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (openAttackAtLaunch)
    {
        observatorySizeIndex = ui::spectrumSizePresets.size() - 1u;
        observatoryDomain = hypha::observatory::Domain::time;
        observatoryView.setDomain (observatoryDomain);
        setAnalysisPage (AnalysisPage::attack);
    }
   #endif
}

KirinHyphaEditor::~KirinHyphaEditor()
{
    stopTimer();
    commitEditorSizeStateIfSettled (true);
    tooltip.setLookAndFeel (nullptr);
    if (isPost)
    {
        processorRef.selectReferenceA();
        processorRef.setSpectrumVisible (false);
        processorRef.setPerceptualVisible (false);
        processorRef.setAbsoluteVisible (false);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        processorRef.setAttackEnabled (false);
       #endif
    }
}

KirinHyphaEditor::PairMenuLookAndFeel& KirinHyphaEditor::pairMenuLookAndFeel()
{
    static PairMenuLookAndFeel lookAndFeel;
    return lookAndFeel;
}

juce::String KirinHyphaEditor::instanceId8() const
{
    return processorRef.instanceId().substring (0, 8);
}

void KirinHyphaEditor::paint (juce::Graphics& g)
{
    bg.draw (g, getLocalBounds()); // mycelium PNG over BG (R-12: pure chrome)

    g.setColour (COL_NORMAL);
    g.setFont (hypha::labelFont (ui::titleFontHeight));
    g.drawText (isPost ? ui::postTitle : ui::preTitle,
                titleArea,
                juce::Justification::centredLeft);

    // Spectrum page changes only this separator to its cool accent; meter pages keep flora.
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    g.setColour (analysisPage != AnalysisPage::meters
                   ? COL_SPECTRUM_DELTA : COL_FLORA);
   #else
    g.setColour (COL_FLORA);
   #endif
    g.fillRect ((float) ui::margin, (float) floraY,
                (float) (getWidth() - 2 * ui::margin), 1.0f);
}

void KirinHyphaEditor::resized()
{
    size_t nearestPreset = 0u;
    for (size_t index = 0u; index < hypha::observatory::sizePresets.size(); ++index)
        if (getWidth() >= hypha::observatory::sizePresets[index].width)
            nearestPreset = index;
    observatorySizeIndex = nearestPreset;
    processorRef.setSpectrumSizePreference ((uint8_t) nearestPreset);
    if (processorRef.setObservatoryEditorSizePreference (getWidth(), getHeight()))
    {
        editorSizeStateDirty = true;
        editorSizeLastChangedAt = nowSecs();
    }

    const auto viewport = hypha::observatory::displayViewport (getWidth(), getHeight());
    scaleRoot.setTransform (juce::AffineTransform());
    scaleRoot.setBounds (0, 0, viewport.width, viewport.height);
    // Keep the root live: Studio One may retain a stale cached peer surface after host resizing.
    scaleRoot.setBufferedToImage (false);
    scaleRoot.setTransform (juce::AffineTransform::scale (viewport.scale));
    observatoryView.setDisplayedEditorSize (getWidth(), getHeight());
    observatoryView.setBounds (scaleRoot.getLocalBounds());
    observatoryView.toBack();
    auto connection = observatoryView.connectionBounds().reduced (4, 2);
    led.setBounds (connection.removeFromLeft (10).withSizeKeepingCentre (7, 7));
    if (isPost)
    {
        nameField.setPrefix (getWidth() < 450 ? "" : "PAIR ");
        pairDropdown.setBounds (connection.removeFromRight (18));
    }
    const bool showName = true;
    nameField.setVisible (showName);
    observatoryView.setExternalConnectionLabelVisible (showName);
    if (showName)
        nameField.setBounds (connection);
    pairStatusLabel.setVisible (false);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost)
    {
        spectrumToggle.setVisible (false);
        spectrumSizeToggle.setVisible (false);
        updateTimePageNavigation();
        auto analysisBody = observatoryView.analysisBodyBounds();
        timePageNavigation.setBounds (observatoryView.timeNavigationBounds());
        spectrumView.setBounds (analysisBody);
        perceptualView.setBounds (analysisBody);
        absoluteView.setBounds (analysisBody);
        attackView.setBounds (analysisBody);
        layoutReferenceAudition (analysisBody);
        timePageNavigation.toFront (false);
    }
   #endif
    guideConnectButton.setBounds (observatoryView.guideBounds());
    feedbackLabel.setBounds (observatoryView.sessionBounds());
    feedbackLabel.toFront (false);
}

void KirinHyphaEditor::layoutMetrics (bool)
{
    for (int i = 0; i < 6; ++i)
        cells[(size_t) i].setBounds (juceRect (ui::metricCellBounds (i, metricTop, getWidth())));
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::setAnalysisPage (AnalysisPage page)
{
    if (! isPost || analysisPage == page)
        return;
    const auto previousPage = analysisPage;
    spectrumView.clearSnapshot();
    perceptualView.clearSnapshot();
    absoluteView.clearSnapshot();
    attackView.clearSnapshot();
    cachedAttackEvents = {};
    cachedAttackWaveform = {};
    cachedAttackDetails = {};
    cachedAttackPreWaveform = {};
    cachedAttackPreDetails = {};
    cachedAttackPairEvents = {};
    cachedAttackStats = {};
    cachedAttackLatest = -1;
    cachedAttackRate = 0;
    cachedAttackGeneration = 0;
    cachedAttackPairStatus = -1;
    // ATTACK / FREQ / SHARP / LIVE share the current Analysis lease. Their `true` edge changes
    // the isolated analyzer while post_visible remains set. Only METERS releases the slot.
    if (hypha::analysis_navigation::releasesSlot (previousPage, page))
    {
        if (previousPage == AnalysisPage::spectrum)
            processorRef.setSpectrumVisible (false);
        else if (previousPage == AnalysisPage::perceptual)
            processorRef.setPerceptualVisible (false);
        else if (previousPage == AnalysisPage::absolute)
            processorRef.setAbsoluteVisible (false);
        else if (previousPage == AnalysisPage::attack)
            processorRef.setAttackEnabled (false);
    }
    analysisPage = page;
    observatoryView.setExternalAnalysisBodyActive (page != AnalysisPage::meters);
    const bool analysisOpen = page != AnalysisPage::meters;
    for (auto& cell : cells)
        cell.setVisible (false);
    loudnessSelector.setVisible (false);
    spectrumView.setVisible (page == AnalysisPage::spectrum);
    perceptualView.setVisible (page == AnalysisPage::perceptual);
    absoluteView.setVisible (page == AnalysisPage::absolute);
    attackView.setVisible (page == AnalysisPage::attack);
    spectrumSizeToggle.setVisible (false);
    spectrumToggle.setVisible (false);
    timePageNavigation.setPage (page);
    updateTimePageNavigation();
    startTimerHz (page == AnalysisPage::absolute
                    ? ui::absoluteTimelineSourceHz
                    : analysisOpen ? ui::spectrumPresentationHz
                                   : ui::preDisplayPresentationHz);
    resized();
    repaint();
    if (page == AnalysisPage::attack)
        processorRef.setAttackEnabled (true);
    else if (page == AnalysisPage::spectrum)
        processorRef.setSpectrumVisible (true);
    else if (page == AnalysisPage::perceptual)
        processorRef.setPerceptualVisible (true);
    else if (page == AnalysisPage::absolute)
        processorRef.setAbsoluteVisible (true);
}

void KirinHyphaEditor::updateTimePageNavigation()
{
    const bool time = observatoryDomain == hypha::observatory::Domain::time;
    const bool direct = time && observatoryView.experienceFamily()
        == hypha::observatory::ExperienceFamily::observatory;
    timePageNavigation.setDirect (direct);
    timePageNavigation.setPage (analysisPage);
    timePageNavigation.setVisible (time);
}

void KirinHyphaEditor::cycleSpectrumSize()
{
    if (analysisPage == AnalysisPage::meters)
        return;
    observatorySizeIndex = (observatorySizeIndex + 1u) % ui::spectrumSizePresets.size();
    processorRef.setSpectrumSizePreference ((uint8_t) observatorySizeIndex);
    updateSpectrumSizeControl();
    const auto preset = ui::spectrumSizePresets[observatorySizeIndex];
    setSize (preset.width, preset.height);
}

void KirinHyphaEditor::updateSpectrumSizeControl()
{
    const auto preset = ui::spectrumSizePresets[observatorySizeIndex];
    spectrumSizeToggle.setButtonText (preset.buttonText);
    spectrumSizeToggle.setTooltip (preset.tooltip);
}
#endif

void KirinHyphaEditor::configureForKind (Kind k)
{
    const bool watch = (k == Kind::WatchAbs6 || k == Kind::WatchDelta6);
    const bool dlt = (k == Kind::WatchDelta6 || k == Kind::Delta6);
    const juce::String d = hypha::delta();

    const auto& specs = watch ? ui::watchMetrics : ui::recordMetrics;
    for (int i = 0; i < (int) specs.size(); ++i)
    {
        const auto spec = specs[(size_t) i];
        const auto text = ui::metricText (spec.metric);
        const bool deltaCell = dlt && spec.deltaEligible;
        const juce::String label = i == 0
                                     ? juce::String()
                                     : spec.maximum
                                     ? juce::String (ui::maximumLabel)
                                     : (deltaCell ? d + text.deltaSuffix
                                                  : juce::String (text.absoluteLabel));
        const juce::String unit = deltaCell ? text.deltaUnit : text.absoluteUnit;
        const auto help = spec.metric == ui::Metric::lufs
                            && processorRef.useShortTermLoudness()
                              ? hypha::helpLufsS()
                              : metricHelp (spec.metric);
        cells[(size_t) i].configure (label, unit, help,
                                     ui::metricLabelFontHeight,
                                     ui::metricValueFontHeight,
                                     ui::metricUnitFontHeight,
                                     ui::metricMinimumLabelWidth);
        cells[(size_t) i].setVisible (false);
    }
    currentKind = k;
    currentSix  = true;
    loudnessSelector.setShortTerm (processorRef.useShortTermLoudness());
    loudnessSelector.setDeltaMode (dlt);
    loudnessSelector.setVisible (false);
    layoutMetrics (true);
    loudnessSelector.toFront (false);
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
    toastText = msg;
    updateFeedback (nowSecs(), false, {});
}

void KirinHyphaEditor::updateFeedback (
    double now, bool keeping, const juce::String& persistentError)
{
    juce::String text;
    juce::Colour colour = COL_MUTED;

    // Direct user-action feedback must remain visible even while a persistent producer error is
    // present (R-28). After the three-second toast, the persistent error automatically returns;
    // the short acknowledgement is the lowest-priority informational state.
    if (now < toastUntil && toastText.isNotEmpty())
    {
        text = toastText;
        colour = COL_NORMAL;
    }
    else if (persistentError.isNotEmpty())
    {
        text = persistentError;
        colour = hypha::COL_LED_YELLOW;
    }
    else if (keeping)
    {
        text = "Keeping";
        colour = COL_FLORA;
    }

    if (now >= toastUntil)
        toastText.clear();

    feedbackLabel.setVisible (text.isNotEmpty());
    if (text.isNotEmpty())
    {
        feedbackLabel.setText (text, juce::dontSendNotification);
        feedbackLabel.setColour (juce::Label::textColourId, colour);
    }
}

void KirinHyphaEditor::showCandidateMenu()
{
    processorRef.refreshLicenseForUserAction();
    // B-102: egui draw_pair_pre_combo parity (scope = new↔new). Built on click (no per-tick FFI):
    //   [All Keep: N ready POST(s)] (Watch, N>=1) / [All Stop: recording POSTs] (Record) /
    //   candidate rows ("Can Keep/Keep ready/In use: name-or-id8").
    // Every live PRE is offered, including unnamed instances. Selection commits its exact
    // instance_id; the name remains only the convenient persisted display/search value.
    const bool rec = processorRef.isRecording();
    const bool keepActive = rec
        || processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE;
    const bool playing = processorRef.isPlaying(); // W-280: pair change locked during playback
    // B-115: lock only when playing AND live (processBlock running). A frozen `playing` with a
    // stalled heartbeat does not lock (false-release prevention; signal_state is silence-conflated).
    const bool pairLocked = playing && processorRef.heartbeatLive();
    const auto cands = processorRef.enumeratePreCandidates();
    const auto claims = processorRef.enumeratePostPairClaims();
    const juce::String ownInstanceId = processorRef.instanceId();

    juce::StringArray labels;
    juce::Array<bool> labelEnabled;
    juce::Array<bool> labelChecked;
    const juce::String currentPreInstanceId = resolvedOwnPreInstanceId (
        ownInstanceId, processorRef.pairedPreInstanceId(), claims);
    for (const auto& c : cands)
    {
        const bool keepReady = currentPreInstanceId.isNotEmpty()
                                 && c.instanceId == currentPreInstanceId;
        const bool inUse = claimedByOtherPost (c, ownInstanceId, claims);
        int sameNameCount = 0;
        if (c.hasName && c.name.isNotEmpty())
            for (const auto& other : cands)
                if (other.hasName && other.name == c.name)
                    ++sameNameCount;
        const juce::String shown = c.hasName && c.name.isNotEmpty()
                                     ? c.name + (sameNameCount > 1
                                                     ? " · " + c.instanceId.substring (0, 8)
                                                     : juce::String())
                                     : c.instanceId.substring (0, 8);
        labels.add ((inUse ? "In use: " : (keepReady ? "Keep ready: " : "Can Keep: ")) + shown);
        labelEnabled.add (! inUse);
        labelChecked.add (keepReady && ! inUse);
    }

    // egui parity: "N ready" = pair-set POST instances (keepReadyCount), NOT the PRE candidate
    // count — the All Keep broadcast acts on POSTs (hypha_post editor.rs:938-944). Candidate rows
    // below are exact PRE rows, matching egui's separate pre_candidates source.
    const int nReady = processorRef.keepReadyCount();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Display");
    menu.addItem (10, "Show hover help", true,
                  hypha::HoverHelpPreference::shared().isEnabled());
    menu.addSeparator();
    const bool pairSelected = processorRef.pairStatus() != KIRIN_PAIR_STATUS_UNPAIRED;
    if (! keepActive && pairSelected)
        menu.addItem (4, "Keep selected pair", processorRef.licenseIsOs());
    if (keepActive)
        menu.addItem (5, "Stop selected pair");
    if (! keepActive && processorRef.licenseIsOs() && nReady >= 1)
        menu.addItem (1, allKeepMenuLabel (nReady));
    if (keepActive)
        menu.addItem (2, "All Stop: active POSTs");
    if (menu.getNumItems() > 0)
        menu.addSeparator();
    menu.addSectionHeader ("Pair choices (not Keep targets)");
    if (cands.isEmpty())
        menu.addItem (3, "No pair choices", false, false); // disabled (R-26: silent when nothing)
    else
        for (int i = 0; i < labels.size(); ++i)
            menu.addItem (100 + i, labels[i], ! pairLocked && labelEnabled[i], labelChecked[i]);

    const auto options = juce::PopupMenu::Options()
                             .withTargetComponent (&pairDropdown)
                             .withDeletionCheck (*this)
                             .withMinimumWidth (ui::pairMenuMinimumWidth)
                             .withMaximumNumColumns (ui::pairMenuMaximumColumns)
                             .withStandardItemHeight (ui::pairMenuItemHeight);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis, candidates = cands] (int result)
    {
        if (safeThis != nullptr)
            safeThis->handleCandidateMenu (result, candidates);
    });
}

void KirinHyphaEditor::handleCandidateMenu (
    int result, const juce::Array<KirinHyphaProcessorBase::PreCandidate>& candidates)
{
    if (result == 1)
    {
        if (! processorRef.keepAll())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 2)
        processorRef.stopAll();
    else if (result == 4)
    {
        if (! processorRef.keepPair())
        {
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            showToast (processorRef.licenseIsOs()
                           ? "No PRE Paired" : "Record requires Kirin OS license");
        }
    }
    else if (result == 5)
        processorRef.stopPair();
    else if (result == 10)
    {
        auto& preference = hypha::HoverHelpPreference::shared();
        const bool enabled = ! preference.isEnabled();
        const bool persisted = preference.setEnabled (enabled);
        if (! enabled)
            tooltip.hideTip();
        if (! persisted)
            showToast ("Hover help changed for this session only");
    }
    else if (result >= 100)
    {
        const int idx = result - 100;
        if (idx >= 0 && idx < candidates.size())
        {
            const auto candidate = candidates.getReference (idx);
            const juce::String name = candidate.hasName ? candidate.name : juce::String();
            if (processorRef.setPairCandidate (candidate.instanceId, name))
            {
                pairedPreExplicitlyBypassed = false;
               #if ! KIRIN_HYPHA_PRE_DISPLAY
                spectrumView.clearSnapshot();
                perceptualView.clearSnapshot();
               #endif
                nameField.setModelName (name);
                nameField.setFallback (name.isEmpty() ? candidate.instanceId.substring (0, 8)
                                                      : juce::String ("___"));
            }
            else
            {
                const juce::String notice = processorRef.drainKeepActionNotice();
                showToast (notice.isNotEmpty() ? notice : juce::String ("PRE no longer available"));
            }
        }
    }
}

void KirinHyphaEditor::timerCallback()
{
    commitEditorSizeStateIfSettled (false);
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    const auto attachment = processorRef.takeCaptureWorkAttachmentResult();
    if (attachment.state == hypha::capture::WorkAttachmentResultState::attached)
        showToast ("Capture attached to Work");
    else if (attachment.state == hypha::capture::WorkAttachmentResultState::rejected)
    {
        if (attachment.code == "work_binding_changed")
            showToast ("Work connection changed; Capture was not attached");
        else if (attachment.code == "work_not_found")
            showToast ("Connected Work is no longer available");
        else if (attachment.code == "work_record_invalid"
                 || attachment.code == "work_write_failed"
                 || attachment.code == "destination_write_failed")
            showToast ("Work could not be updated");
        else if (attachment.code == "request_write_failed"
                 || attachment.code == "artifact_invalid"
                 || attachment.code == "request_invalid")
            showToast ("Capture could not be attached");
        else
            showToast ("Kirin OS could not attach this Capture");
    }
#endif
    if (isPost) updatePost();
    else        updatePre();
    refreshObservatory();
}

void KirinHyphaEditor::commitEditorSizeStateIfSettled (bool force)
{
    constexpr double settleSeconds = 0.2;
    if (! editorSizeStateDirty
        || (! force && nowSecs() - editorSizeLastChangedAt < settleSeconds))
        return;
    editorSizeStateDirty = false;
    processorRef.notifyObservatoryEditorSizeChanged();
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
    const bool ack    = processorRef.recordAcknowledged(); // POST: always false (egui parity)
    const bool preset = processorRef.presetAvailable();
    const bool playing = processorRef.isPlaying();
    // B-115: lock only when playing AND live (processBlock running) — false-release prevention.
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
