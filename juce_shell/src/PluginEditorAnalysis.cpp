#include "PluginEditor.h"

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
            observatoryView.setDeltaTargetEnabled (
                channelMode != KIRIN_SPECTRUM_SELECTION_MID_SIDE);
        return accepted;
    };
    spectrumView.onSubviewChange = [this]
    {
        observatoryView.setDeltaTargetEnabled (spectrumView.isPsbObservation()
            || processorRef.spectrumDisplaySelection() != KIRIN_SPECTRUM_SELECTION_MID_SIDE);
        configureSpectrumAnalysis();
    };
}

void KirinHyphaEditor::setAnalysisPage (AnalysisPage page)
{
    tooltip.hideTip();
    if (page == AnalysisPage::attack
        && ! hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()))
        page = AnalysisPage::meters;
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
    // ATTACK / FREQ / SHARP / LIVE share the current Analysis lease. HISTORY and RUN are
    // read-only meter projections and release it.
    if (hypha::analysis_navigation::releasesSlot (previousPage, page))
    {
        if (previousPage == AnalysisPage::spectrum)
        {
            processorRef.setSpectrumVisible (false);
            processorRef.setPsbVisible (false);
        }
        else if (previousPage == AnalysisPage::perceptual)
        {
            if (sharpnessUsesAbsolute) processorRef.setAbsoluteVisible (false);
            else processorRef.setPerceptualVisible (false);
        }
        else if (previousPage == AnalysisPage::absolute)
            processorRef.setAbsoluteVisible (false);
        else if (previousPage == AnalysisPage::attack)
            processorRef.setAttackEnabled (false);
    }
    analysisPage = page;
    sharpnessUsesAbsolute = page == AnalysisPage::perceptual
        && processorRef.pairStatus() != KIRIN_PAIR_STATUS_PAIRED;
    absoluteView.setSharpnessOnly (sharpnessUsesAbsolute);
    observatoryView.setAnalysisPage (page);
    const bool analysisOpen = hypha::analysis_navigation::isAnalysis (page);
    observatoryView.setRunSummaryMode (page == AnalysisPage::run);
    observatoryView.setExternalAnalysisBodyActive (analysisOpen);
    for (auto& cell : cells)
        cell.setVisible (false);
    loudnessSelector.setVisible (false);
    spectrumView.setVisible (page == AnalysisPage::spectrum);
    perceptualView.setVisible (page == AnalysisPage::perceptual && ! sharpnessUsesAbsolute);
    absoluteView.setVisible (page == AnalysisPage::absolute || sharpnessUsesAbsolute);
    attackView.setVisible (page == AnalysisPage::attack);
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
    if (page == AnalysisPage::attack)
        processorRef.setAttackEnabled (true);
    else if (page == AnalysisPage::spectrum)
        configureSpectrumAnalysis();
    else if (page == AnalysisPage::perceptual)
    {
        if (sharpnessUsesAbsolute) processorRef.setAbsoluteVisible (true);
        else processorRef.setPerceptualVisible (true);
    }
    else if (page == AnalysisPage::absolute)
        processorRef.setAbsoluteVisible (true);
}

void KirinHyphaEditor::configureSharpnessAnalysis (int pairStatus)
{
    if (analysisPage != AnalysisPage::perceptual)
        return;
    const bool useAbsolute = pairStatus != KIRIN_PAIR_STATUS_PAIRED;
    if (useAbsolute == sharpnessUsesAbsolute)
        return;

    if (sharpnessUsesAbsolute) processorRef.setAbsoluteVisible (false);
    else processorRef.setPerceptualVisible (false);
    sharpnessUsesAbsolute = useAbsolute;
    perceptualView.clearSnapshot();
    absoluteView.clearSnapshot();
    absoluteView.setSharpnessOnly (sharpnessUsesAbsolute);
    perceptualView.setVisible (! sharpnessUsesAbsolute);
    absoluteView.setVisible (sharpnessUsesAbsolute);
    if (sharpnessUsesAbsolute) processorRef.setAbsoluteVisible (true);
    else processorRef.setPerceptualVisible (true);
    startTimerHz (sharpnessUsesAbsolute ? ui::absoluteTimelineSourceHz
                                        : ui::spectrumPresentationHz);
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
    if (spectrumView.isPsbObservation())
    {
        processorRef.setSpectrumVisible (false);
        processorRef.setPsbVisible (! absolute);
    }
    else
    {
        processorRef.setPsbVisible (false);
        processorRef.setSpectrumVisible (true);
    }
}

bool KirinHyphaEditor::refreshAnalysisViews (
    bool alive, int signalState, bool recording, bool armed,
    bool acknowledged, bool presetAvailable, int pairStatus)
{
    if (! hypha::analysis_navigation::isAnalysis (analysisPage))
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
        KirinAttackStats stats {};
        const bool statsReady = processorRef.attackStats (stats);
        if (statsReady)
            cachedAttackStats = stats;

        KirinAttackBatch raw {};
        if (processorRef.pollAttackBatch (raw))
        {
            if (raw.count > 0)
            {
                const auto count = juce::jmin (
                    raw.count, static_cast<std::uint32_t> (KIRIN_ATTACK_BATCH_CAPACITY));
                const auto& newest = raw.frames[count - 1];
                cachedAttackLatest = newest.support_end_samples;
                cachedAttackRate = newest.sample_rate;
                cachedAttackGeneration = newest.generation;
            }
            else if (cachedAttackLatest >= 0)
            {
                cachedAttackLatest = -1;
                cachedAttackRate = 0;
                cachedAttackGeneration = 0;
            }
        }
        // Event detail is published after its raw endpoint. Poll every bounded presentation tick;
        // endpoint-only gating loses late detail whenever transport stops on that endpoint.
        {
            KirinAttackEventBatch events {};
            KirinAttackWaveformBatch waveform {}, preWaveform {};
            KirinAttackDetailBatch details {}, preDetails {};
            KirinAttackPairEventBatch pairEvents {};
            if (processorRef.pollAttackEvents (events)) cachedAttackEvents = events;
            if (processorRef.pollAttackWaveform (waveform)) cachedAttackWaveform = waveform;
            if (processorRef.pollAttackDetails (details)) cachedAttackDetails = details;
            if (processorRef.pollAttackPreWaveform (preWaveform))
                cachedAttackPreWaveform = preWaveform;
            if (processorRef.pollAttackPreDetails (preDetails))
                cachedAttackPreDetails = preDetails;
            if (processorRef.pollAttackPairEvents (pairEvents))
                cachedAttackPairEvents = pairEvents;
        }
        attackView.setSnapshot (
                cachedAttackEvents, cachedAttackWaveform, cachedAttackDetails,
                cachedAttackPreWaveform, cachedAttackPreDetails, cachedAttackPairEvents,
                cachedAttackLatest, cachedAttackRate, cachedAttackGeneration, cachedAttackStats);
        observatoryView.setAttackPaired (attackView.pairedObservation());
        attackView.presentationTick (liveInput);
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
