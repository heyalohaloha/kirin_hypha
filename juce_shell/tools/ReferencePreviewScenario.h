#pragma once

// Optional, deterministic demonstration for comparing C layouts. No source is played. The C
// spectrum, A and the time-series are illustrative synthetic data, not a real song.
#include "../src/HyphaReferenceComponent.h"
#include "../src/reference_audition/ReferenceVisualTimeline.h"
#include <cmath>

#include "ReferencePreviewRound2.h"

inline int previewWidth()
{
    const auto size = juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_SIZE", {});
    return size == "100" ? 300 : size == "200" ? 600 : 900;
}
inline bool previewDetails()
{
    return juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_DETAILS", {}) == "1";
}
inline hypha::reference_ui::State previewScenario (hypha::reference_ui::State state)
{
    using namespace hypha;
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_GUIDE", {}) == "listening")
    {
        state.viewBindings.clear();
        state.listeningChecks.insert (state.checkId.upToFirstOccurrenceOf ("/", false, false));
        return state;
    }
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_GUIDE", {}) == "no-check")
    {
        state.viewBindings.clear(); state.checks.clear(); state.checkId.clear();
        state.checkStep = reference_ui::SourceStep::enableCheck;
        state.versionStep = reference_ui::SourceStep::chooseVersion;
        state.checkReady = state.checkArmable = false;
        state.readiness = reference_ui::Readiness::waiting;
        state.osAccess = os_access::State::connectedUnprepared;
        state.status = "ADD SONGS TO A CHECK SET IN KIRIN OS";
        return state;
    }
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_RANGES", {});
    if (path.isEmpty()) return state;
    const auto fixture = juce::JSON::parse (juce::File (path).loadFileAsString());
    const auto* centers = fixture["spectrum_band_centers_hz"].getArray();
    const auto* ranges = fixture["ranges"].getArray();
    if (centers == nullptr || ranges == nullptr || ranges->isEmpty()) return state;
    const auto range = ranges->getReference (0);
    const auto spectrum = range["spectrum"];
    const auto* medians = spectrum["median_millidbfs"].getArray();
    const auto* lowValues = spectrum["p10_millidbfs"].getArray();
    const auto* highValues = spectrum["p90_millidbfs"].getArray();
    const auto* balances = range["balance_millidbfs"].getArray();
    if (medians == nullptr || lowValues == nullptr || highValues == nullptr || balances == nullptr
        || centers->size() != medians->size() || centers->size() != lowValues->size()
        || centers->size() != highValues->size() || balances->size() != 4) return state;
    const bool bExample = juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_B", {}).isNotEmpty();
    auto reference = std::make_shared<reference_audition::KirinSpectrumWindow>();
    for (int i = 0; i < centers->size(); ++i)
    {
        reference->centersHz.push_back (static_cast<double> ((*centers)[i]));
        const auto octave = std::log2 (static_cast<double> ((*centers)[i]) / 100.0);
        const auto highOctaves = std::max (0.0, std::log2 (static_cast<double> ((*centers)[i]) / 2000.0));
        const auto level = bExample ? -21.0 - 3.0 * octave + 2.2 * std::sin (octave * 1.3) + 0.7 * std::sin (octave * 3.1)
                                    : -23.0 - 2.5 * octave - 10.0 * highOctaves + 1.8 * std::sin (octave * 1.3);
        reference->medianDb.push_back (static_cast<float> (level));
        reference->p10Db.push_back (static_cast<float> (level - (bExample ? 3.2 : 7.0)));
        reference->p90Db.push_back (static_cast<float> (level + (bExample ? 3.2 : 7.0)));
    }
    reference->balanceDb = { -14.0, -18.0, -25.0, -33.0 };
    reference->frames = reference->wantedFrames = 160;
    auto live = std::make_shared<reference_audition::KirinSpectrumWindow> (*reference);
    for (size_t i = 0; i < live->medianDb.size(); ++i)
    {
        const auto octave = std::log2 (live->centersHz[i] / 100.0);
        const auto blend = juce::jlimit (0.0, 1.0, (octave - 1.0) / 5.0);
        const auto delta = static_cast<float> (bExample ? 4.0 + 2.0 * std::sin (octave * 0.8)
                                                         : 3.3 + 2.5 - 4.0 * blend + 0.3 * std::sin (octave));
        live->medianDb[i] += delta;
        live->p10Db[i] += delta - 0.7f;
        live->p90Db[i] += delta + 0.7f;
    }
    live->balanceDb = { -8.9, -13.8, -21.0, -30.5 };
    state.cueKirin = reference; state.aKirin = live;
    state.sourceDurationSeconds = 186.0; state.cueStartSeconds = 0.0; state.cueEndSeconds = 16.0;
    state.cuePlayheadSeconds = 0.0; state.cuePart = reference_audition::CuePart::chorus;
    state.followA = true; state.aWindowLoudness = -5.01; state.wholeSongLoudness = -14.11;
    state.cueLoudness = -14.11; state.aWindowBlocks = state.aWindowNeededBlocks = 160;
    state.aIntegratedLoudness = -5.01; state.adjustedBIntegratedLoudness = -10.81;
    state.aMaximumTruePeakDbtp = -1.0; state.adjustedBMaximumTruePeakDbtp = -1.0;
    state.transportPlaying = state.bSelected = state.audibleHeard = true;
    state.audibleComparisonSlot = 2; state.tracking = reference_audition::TrackingState::fixed;
    const bool matched = state.comparisonMode == "loudness_match";
    state.appliedGainDb = matched ? 3.3 : 0.0;
    state.originalAudition = ! matched;
    if (matched)
    {
        state.gainLimited = true; state.peakShortfallDb = 5.8;
        state.action.kind = reference_ui::ActionKind::lowerAAndPlay;
        state.action.offer.db = -9.1;
        state.actionText = "LOWER A 9.1 DB";
    }
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->audio = { 48'000, 2, 16 * 48'000 };
    reference_audition::RuntimeNullableIntegerSeries crest, momentary, shortTerm;
    for (int i = 0; i < 80; ++i)
    {
        const auto wave = std::sin (static_cast<double> (i) * 0.37);
        crest.push_back (std::llround ((9.5 + 1.5 * wave) * 1000.0));
        momentary.push_back (std::llround ((-11.4 + 2.0 * wave) * 1000.0));
        shortTerm.push_back (std::llround ((-11.4 + 0.4 * wave) * 1000.0));
    }
    measurement->dynamics = reference_audition::RuntimeMeasurementTimeline { 9600, { { "crest_millidb", crest } } };
    measurement->loudness = reference_audition::RuntimeMeasurementTimeline { 9600, { { "lufs_m_millilu", momentary }, { "lufs_s_millilu", shortTerm } } };
    state.cueMeasurement = measurement;
    auto timeline = std::make_shared<reference_audition::VisualTimeline>();
    timeline->binding.matchWindowBlocks = 160; timeline->aTickChannels = 2;
    auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
    for (int i = 0; i < 160; ++i)
    {
        const auto wave = std::sin (static_cast<double> (i) * 0.21);
        KirinReferenceVisualBin bin {}; bin.frames = 4800;
        const auto mid = 48.0 * (1.0 + 0.3 * wave), side = mid * 0.16;
        bin.mid = mid; bin.side = side; bin.cross = mid - side;
        bin.rms[0] = bin.rms[1] = std::sqrt ((mid + side) / 4800.0);
        bin.peak[0] = bin.peak[1] = bin.rms[0] * 2.4;
        bin.true_peak = bin.rms[0] * 2.5;
        bin.momentary_lufs = -5.1 + 2.0 * wave; bin.short_lufs = -5.3 + 1.5 * wave;
        ticks->push_back (bin);
    }
    timeline->aTicks = ticks; state.visualTimeline = timeline;
    state.targetsEnabled = true;
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_CHECK_CHANGED", {}) == "1")
    {
        for (auto& option : state.checks)
            option.sourceIdentityKey = option.id == state.checkId ? "source-a" : "source-b";
        state.bSelected = false; state.audibleComparisonSlot = 0; state.action = {}; state.actionText.clear();
    }
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_PREVIEW_CHECK_SONG", {}) == "1")
    { state.song.chosen = true; state.bSelected = false; state.action = {}; state.actionText.clear(); }
    previewRound2 (state);
    return state;
}
