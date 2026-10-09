#include "PluginEditor.h"
#include "HyphaSnapshotSource.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
namespace
{
namespace ui = hypha::ui_contract;

}

void KirinHyphaEditor::configureSpectrumCallbacks()
{
    spectrumView.onChannelModeChange = [this] (uint8_t channelMode)
    {
        const bool accepted = processorRef.setSpectrumDisplaySelection (
            channelMode,
            observatoryView.target() == hypha::observatory::ObservationTarget::absolute);
        if (accepted)
        {
            observatoryView.setDeltaTargetEnabled (
                channelMode != KIRIN_SPECTRUM_SELECTION_MID_SIDE);
            syncAnalysisDemand();
        }
        return accepted;
    };
    spectrumView.onSubviewChange = [this]
    {
        observatoryView.setDeltaTargetEnabled (spectrumView.isPsbObservation()
            || processorRef.spectrumDisplaySelection() != KIRIN_SPECTRUM_SELECTION_MID_SIDE);
        configureSpectrumAnalysis();
    };
    // The DRUM band is the view's own state; the engine follows it and PRE follows the engine.
    attackView.onBandChange = [this] (std::uint8_t band) {
        nextDrumSummaryMs = 0.0;
        processorRef.setAttackBand (band);
    };
    attackView.singleRequestSource = [this] (const KirinAttackSingleV2Request& request, std::uint64_t& token) {
        return hypha::snapshots::Source (processorRef).requestSingle (request, token);
    };
    attackView.singleCancelSource = [this] (std::uint64_t token) {
        hypha::snapshots::Source (processorRef).cancelSingle (token);
    };
    attackView.bandEnvelopeSource = [this] (std::int64_t sample, KirinAttackBandHitEnvelope& out)
    { return processorRef.pollAttackBandEnvelope (sample, out); };
}

void KirinHyphaEditor::setAnalysisPage (AnalysisPage page)
{
    tooltip.hideTip();
    if (page == AnalysisPage::attack
        && ! hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()))
        page = AnalysisPage::meters;
    if (! isPost || analysisPage == page)
        return;
    spectrumView.clearSnapshot();
    perceptualView.clearSnapshot();
    absoluteView.clearSnapshot();
    attackView.retireV2();
    nextDrumSummaryMs = 0.0;
    attackView.clearSnapshot();
    cachedAttackEvents = {};
    cachedAttackWaveform = {};
    cachedAttackDetails = {};
    cachedAttackPreWaveform = {};
    cachedAttackPreDetails = {};
    cachedAttackPairEvents = {};
    cachedAttackStats = {};
    cachedAttackBand = {};
    cachedAttackLatest = -1;
    cachedAttackRate = 0;
    cachedAttackGeneration = 0;
    analysisPage = page;
    // A reopened editor starts at ALL while the engine may still hold an earlier choice.
    if (page == AnalysisPage::attack)
    {
        attackView.beginSnapshotV2();
        processorRef.setAttackBand (attackView.band());
    }
    sharpnessUsesAbsolute = page == AnalysisPage::perceptual
        && processorRef.pairStatus() != KIRIN_PAIR_STATUS_PAIRED;
    absoluteView.setSharpnessOnly (sharpnessUsesAbsolute);
    observatoryView.setAnalysisPage (page);
    const bool analysisOpen = hypha::analysis_navigation::isAnalysis (page);
    observatoryView.setRunSummaryMode (page == AnalysisPage::run);
    updateAnalysisBodyPresentation();
    spectrumSizeToggle.setVisible (false);
    spectrumToggle.setVisible (false);
    timePageNavigation.setPage (page);
    updateTimePageNavigation();
    startTimerHz (page == AnalysisPage::absolute || sharpnessUsesAbsolute
                    ? ui::absoluteTimelineSourceHz
                    : analysisOpen ? ui::spectrumPresentationHz
                                   : ui::preDisplayPresentationHz);
    resized();
    repaint();
    if (page == AnalysisPage::spectrum)
        configureSpectrumAnalysis();
    else
        syncAnalysisDemand();
}

void KirinHyphaEditor::configureSharpnessAnalysis (int pairStatus)
{
    if (analysisPage != AnalysisPage::perceptual)
        return;
    const bool useAbsolute = pairStatus != KIRIN_PAIR_STATUS_PAIRED;
    if (useAbsolute == sharpnessUsesAbsolute)
        return;

    sharpnessUsesAbsolute = useAbsolute;
    perceptualView.clearSnapshot();
    absoluteView.clearSnapshot();
    absoluteView.setSharpnessOnly (sharpnessUsesAbsolute);
    updateAnalysisBodyPresentation();
    startTimerHz (sharpnessUsesAbsolute ? ui::absoluteTimelineSourceHz
                                        : ui::spectrumPresentationHz);
    syncAnalysisDemand();
    repaint();
}

void KirinHyphaEditor::updateTimePageNavigation()
{
    const bool time = observatoryDomain == hypha::observatory::Domain::time;
    const bool direct = time && observatoryView.experienceFamily()
        == hypha::observatory::ExperienceFamily::observatory;
    timePageNavigation.setDirect (direct);
    timePageNavigation.setDrumAvailable (
        hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()));
    timePageNavigation.setRunAvailable (true);
    timePageNavigation.setPage (analysisPage);
    timePageNavigation.setVisible (time);
}

void KirinHyphaEditor::configureSpectrumAnalysis()
{
    if (analysisPage != AnalysisPage::spectrum) return;
    const bool absolute = observatoryView.target() == hypha::observatory::ObservationTarget::absolute;
    if (! spectrumView.isPsbObservation() && ! absolute
        && processorRef.spectrumDisplaySelection() == KIRIN_SPECTRUM_SELECTION_MID_SIDE)
    {
        const auto single = processorRef.spectrumSingleChannelMode();
        processorRef.setSpectrumDisplaySelection (single, false);
        spectrumView.setDisplaySelection (single);
    }
    const bool midSide = ! spectrumView.isPsbObservation() && absolute
        && processorRef.spectrumDisplaySelection() == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
    if (! spectrumView.isPsbObservation())
        spectrumView.setDisplaySelection (processorRef.spectrumDisplaySelection());
    observatoryView.setDeltaTargetEnabled (! midSide || spectrumView.isPsbObservation());
    spectrumView.setAbsoluteObservation (absolute);
    syncAnalysisDemand();
}

hypha::analysis::Demand KirinHyphaEditor::desiredAnalysisDemand() const noexcept
{
    return hypha::analysis::forSurface ({
        analysisSurfaceShowing(), analysisPage, spectrumView.isPsbObservation(),
        observatoryView.target() == hypha::observatory::ObservationTarget::absolute,
        processorRef.spectrumDisplaySelection() == KIRIN_SPECTRUM_SELECTION_MID_SIDE,
        sharpnessUsesAbsolute, processorRef.spectrumSingleChannelMode(),
        hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()) });
}

bool KirinHyphaEditor::analysisSurfaceShowing() const noexcept
{
    return isPost && analysisOwnerToken != 0 && isShowing() && ! localBlindOpen && ! liveBlindOpen
        && ! observatoryView.hybridVuVisible()
        && hypha::analysis_navigation::isAnalysis (analysisPage);
}

bool KirinHyphaEditor::externalAnalysisBodyShowing() const noexcept
{
    return isPost && hypha::analysis_navigation::isAnalysis (analysisPage)
        && ! observatoryView.recordBodyActive();
}

void KirinHyphaEditor::updateAnalysisBodyPresentation()
{
    const bool external = externalAnalysisBodyShowing();
    observatoryView.setExternalAnalysisBodyActive (external);
    spectrumView.setVisible (external && analysisPage == AnalysisPage::spectrum);
    perceptualView.setVisible (
        external && analysisPage == AnalysisPage::perceptual && ! sharpnessUsesAbsolute);
    absoluteView.setVisible (
        external && (analysisPage == AnalysisPage::absolute || sharpnessUsesAbsolute));
    attackView.setVisible (external && analysisPage == AnalysisPage::attack);
}

void KirinHyphaEditor::syncAnalysisDemand()
{
    const auto demand = desiredAnalysisDemand();
    if (hypha::analysis::active (demand))
        processorRef.setAnalysisDemand (analysisOwnerToken, demand);
    else
        processorRef.releaseAnalysisDemand (analysisOwnerToken);
}

bool KirinHyphaEditor::refreshAnalysisViews (
    bool alive, int signalState, bool recording, bool armed,
    bool acknowledged, bool presetAvailable, int pairStatus)
{
    if (! analysisSurfaceShowing())
        return false;

    const bool liveInput = signalState == KIRIN_SIGNAL_STATE_ACTIVE && processorRef.hasLiveInput();
    configureSharpnessAnalysis (pairStatus);

    const auto updateLed = [this, alive, signalState, recording, armed,
                            acknowledged, presetAvailable]
    {
        led.setState (hypha::deriveLedState (
            alive, signalState, recording && armed, acknowledged, presetAvailable));
    };
    if (analysisPage == AnalysisPage::attack)
    {
        refreshDrumSnapshots (liveInput);
        updateLed();
        return true;
    }

    juce::String ownerNames;
    const bool haveOwners = processorRef.pollAnalysisOwnerNames (ownerNames);
    if (analysisPage == AnalysisPage::spectrum)
    {
        spectrumView.setGuideFrequencyOverlay (hypha::guide_frequency::fromGuidePresentation (
            processorRef.guidePresentationSnapshot()));
        if (haveOwners) spectrumView.setAnalysisOwnerNames (ownerNames);
        spectrumView.presentationTick();
        spectrumView.setSignalActive (liveInput);
        if (spectrumView.isPsbObservation())
        {
            KirinPsbView frame {};
            if (processorRef.pollPsb (frame)) spectrumView.setPsbSnapshot (frame);
        }
        else
        {
            if (processorRef.spectrumDisplaySelection() == KIRIN_SPECTRUM_SELECTION_MID_SIDE)
            {
                KirinMidSideSpectrumView frame {};
                if (processorRef.pollMidSideSpectrum (frame))
                    spectrumView.setMidSideSnapshot (frame);
            }
            else
            {
                KirinSpectrumBatch batch {};
                if (processorRef.pollSpectrumBatch (batch)) spectrumView.setBatch (batch);
            }
        }
    }
    else if (analysisPage == AnalysisPage::perceptual)
    {
        if (sharpnessUsesAbsolute)
        {
            absoluteView.setSignalActive (liveInput);
            if (haveOwners) absoluteView.setAnalysisOwnerNames (ownerNames);
            KirinAbsoluteBatch batch {};
            if (processorRef.pollAbsoluteBatch (batch)) absoluteView.setBatch (batch);
        }
        else
        {
            perceptualView.setSignalActive (liveInput);
            if (haveOwners) perceptualView.setAnalysisOwnerNames (ownerNames);
            perceptualView.presentationTick();
            KirinPerceptualBatch batch {};
            if (processorRef.pollPerceptualBatch (batch)) perceptualView.setBatch (batch);
        }
    }
    else if (analysisPage == AnalysisPage::absolute)
    {
        absoluteView.setSignalActive (liveInput);
        if (haveOwners) absoluteView.setAnalysisOwnerNames (ownerNames);
        KirinAbsoluteBatch batch {};
        if (processorRef.pollAbsoluteBatch (batch)) absoluteView.setBatch (batch);
    }
    updateLed();
    return true;
}
#endif
