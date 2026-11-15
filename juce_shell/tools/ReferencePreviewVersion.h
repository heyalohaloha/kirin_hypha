#pragma once

// Synthetic aligned Versions for off-screen layout review; no audio is loaded or played.
inline void previewVersion (hypha::reference_ui::State& state,
                            const std::shared_ptr<hypha::reference_audition::KirinSpectrumWindow>& live,
                            const std::shared_ptr<hypha::reference_audition::KirinSpectrumWindow>& reference)
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_V", {}).isEmpty()) return;
    using namespace hypha;
    state.comparisonSlot = 1;
    state.bSelected = false;
    state.audibleComparisonSlot = 0;
    state.action = {};
    state.actionText.clear();
    state.versionId = "version-1";
    state.versions = { { "version-1", "v1" }, { "version-2", "v2" } };
    state.versionReady = state.versionArmable = true;
    state.versionStep = reference_ui::SourceStep::ready;
    state.blindPhase = reference_ui::BlindPhase::unavailable;
    auto timeline = std::make_shared<reference_audition::VisualTimeline> (*state.visualTimeline);
    timeline->aPairKirin = live;
    timeline->vPairKirin = reference;
    timeline->binding.aligned = timeline->binding.matched = true;
    timeline->binding.gainDb = 0.0;
    auto source = std::make_shared<reference_audition::RuntimeSource>();
    source->audio = { 48000, 2, 48000 * 20 };
    auto overview = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    overview->waveform.emplace(); overview->waveform->framesPerBin = 4800;
    overview->waveform->samplePeakMillidbfs.resize (2); overview->waveform->rmsMillidbfs.resize (2);
    timeline->binding.source = source; timeline->binding.overview = overview;
    timeline->binding.key = "preview-version"; timeline->binding.channels = 2; timeline->binding.hostRate = 48000;
    timeline->hop = 4800; timeline->pass = timeline->revision = 1;
    auto aTicks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
    auto vTicks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
    for (int index = 0; index < 200; ++index)
    {
        const double amplitude = 0.35 + 0.12 * std::sin (index * 0.21) * std::sin (index * 0.037);
        reference_audition::VisualPairBin pair;
        pair.pass = 1; pair.a.frames = pair.b.frames = 4800;
        pair.a.short_lufs = -14.0 + std::sin (index * 0.05); pair.b.short_lufs = pair.a.short_lufs + 0.5;
        pair.a.crest_db = 7.0; pair.b.crest_db = 6.8;
        for (size_t channel = 0; channel < 2; ++channel)
        {
            pair.a.peak[channel] = amplitude * 0.9; pair.b.peak[channel] = amplitude;
            pair.a.rms[channel] = amplitude * 0.4; pair.b.rms[channel] = amplitude * 0.45;
            overview->waveform->samplePeakMillidbfs[channel].push_back (std::llround (20000.0 * std::log10 (amplitude)));
            overview->waveform->rmsMillidbfs[channel].push_back (std::llround (20000.0 * std::log10 (amplitude * 0.45)));
        }
        timeline->bins.push_back (pair); aTicks->push_back (pair.a); vTicks->push_back (pair.b);
    }
    timeline->aPairTicks = aTicks; timeline->vPairTicks = vTicks; timeline->pairTickChannels = 2;
    state.visualPositionSeconds = 8;
    state.visualTimeline = timeline;
}
