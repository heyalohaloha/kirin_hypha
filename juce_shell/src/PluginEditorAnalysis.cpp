#include "PluginEditor.h"

#if ! KIRIN_HYPHA_PRE_DISPLAY
namespace
{
namespace ui = hypha::ui_contract;

bool sameStats (const KirinAttackStats& left, const KirinAttackStats& right) noexcept
{
    return left.available == right.available && left.enabled == right.enabled
        && left.worker_running == right.worker_running && left.channels == right.channels
        && left.pushed_blocks == right.pushed_blocks
        && left.dropped_blocks == right.dropped_blocks
        && left.analyzed_frames == right.analyzed_frames;
}
}

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
    // ATTACK / FREQ / SHARP / LIVE share the current Analysis lease. HISTORY and RUN are
    // read-only meter projections and release it.
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
    if (page == AnalysisPage::run
        && observatoryView.target() == hypha::observatory::ObservationTarget::delta)
    {
        observatoryView.setTarget (hypha::observatory::ObservationTarget::absolute);
        processorRef.setObservatoryTargetPreference (hypha::observatory::stateValue (
            hypha::observatory::ObservationTarget::absolute));
        spectrumView.setAbsoluteObservation (true);
    }
    analysisPage = page;
    const bool analysisOpen = hypha::analysis_navigation::isAnalysis (page);
    observatoryView.setRunSummaryMode (page == AnalysisPage::run);
    observatoryView.setExternalAnalysisBodyActive (analysisOpen);
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
    timePageNavigation.setRunAvailable (observatoryView.runSummaryAvailable());
    timePageNavigation.setPage (analysisPage);
    timePageNavigation.setVisible (time);
}

bool KirinHyphaEditor::refreshAnalysisViews (
    bool alive, int signalState, bool recording, bool armed,
    bool acknowledged, bool presetAvailable, int pairStatus)
{
    if (! hypha::analysis_navigation::isAnalysis (analysisPage))
        return false;

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
        const bool statsChanged = statsReady && ! sameStats (stats, cachedAttackStats);
        if (statsReady)
            cachedAttackStats = stats;

        KirinAttackBatch raw {};
        bool endpointChanged = false;
        if (processorRef.pollAttackBatch (raw))
        {
            if (raw.count > 0)
            {
                const auto count = juce::jmin (
                    raw.count, static_cast<std::uint32_t> (KIRIN_ATTACK_BATCH_CAPACITY));
                const auto& newest = raw.frames[count - 1];
                endpointChanged = newest.support_end_samples != cachedAttackLatest
                    || newest.sample_rate != cachedAttackRate
                    || newest.generation != cachedAttackGeneration;
                cachedAttackLatest = newest.support_end_samples;
                cachedAttackRate = newest.sample_rate;
                cachedAttackGeneration = newest.generation;
            }
            else if (cachedAttackLatest >= 0)
            {
                cachedAttackLatest = -1;
                cachedAttackRate = 0;
                cachedAttackGeneration = 0;
                endpointChanged = true;
            }
        }
        const bool pairChanged = pairStatus != cachedAttackPairStatus;
        if (pairChanged)
            cachedAttackPairStatus = pairStatus;
        if (endpointChanged || pairChanged)
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
        if (endpointChanged || pairChanged || statsChanged)
            attackView.setSnapshot (
                cachedAttackEvents, cachedAttackWaveform, cachedAttackDetails,
                cachedAttackPreWaveform, cachedAttackPreDetails, cachedAttackPairEvents,
                cachedAttackLatest, cachedAttackRate, cachedAttackGeneration, cachedAttackStats);
        attackView.presentationTick (signalState == KIRIN_SIGNAL_STATE_ACTIVE);
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
        KirinSpectrumBatch batch {};
        if (processorRef.pollSpectrumBatch (batch)) spectrumView.setBatch (batch);
    }
    else if (analysisPage == AnalysisPage::perceptual)
    {
        if (haveOwners) perceptualView.setAnalysisOwnerNames (ownerNames);
        perceptualView.presentationTick();
        KirinPerceptualBatch batch {};
        if (processorRef.pollPerceptualBatch (batch)) perceptualView.setBatch (batch);
    }
    else if (analysisPage == AnalysisPage::absolute)
    {
        if (haveOwners) absoluteView.setAnalysisOwnerNames (ownerNames);
        KirinAbsoluteBatch batch {};
        if (processorRef.pollAbsoluteBatch (batch)) absoluteView.setBatch (batch);
    }
    updateLed();
    return true;
}
#endif
