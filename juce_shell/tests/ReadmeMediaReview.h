#pragma once

#include "CompactReviewShowcase.h"
#include "FreqHistoryReview.h"
#include "ReferenceGuideStates.h"
#include "../src/HyphaLiveBlindComponent.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

// The README's pictures: the shipping editor, drawn by its own code at 300% (900 x 600) and DPI 2,
// with invented data that moves like a song (intro, verse, a lift into the chorus). Written only when
// KIRIN_HYPHA_README_MEDIA_DIR names a directory. No real song, session or machine appears.
namespace hypha::tests::readme_media
{
constexpr double pi = juce::MathConstants<double>::pi;

// LUFS-M around which the 60 s history moves: intro, verse, a four-second lift, then the chorus.
inline double section (double t) noexcept
{
    if (t < 6.0) return -21.0 + 4.5 * t / 6.0;
    if (t < 24.0) return -16.0;
    if (t < 28.0) return -16.0 + 4.2 * (t - 24.0) / 4.0;
    return -11.8;
}

inline double groove (double t) noexcept
{
    return 1.0 * std::sin (t * 2.0 * pi * 2.0) * std::sin (t * 2.0 * pi * 0.25)
         + 0.55 * std::sin (t * 2.0 * pi * 0.53 + 1.3) + 0.3 * std::sin (t * 2.0 * pi * 3.7 + 0.4);
}

inline std::vector<KirinMeterHistoryEntry> history()
{
    std::vector<KirinMeterHistoryEntry> result (600);
    for (size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 7;
        entry.run_id = 1;
        entry.first_observed_frames = index * 4'800;
        entry.last_observed_frames = entry.first_observed_frames + 4'799;
        entry.first_timeline_endpoint_samples = (int64_t) entry.first_observed_frames;
        entry.last_timeline_endpoint_samples = (int64_t) entry.last_observed_frames;
        entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        const auto t = (double) index / 10.0;
        const auto momentary = section (t) + groove (t);
        const auto shortTerm = section (std::max (0.0, t - 1.5)) + 0.35 * std::sin (t * 2.0 * pi * 0.17);
        entry.lufs_m.min = entry.lufs_m.max = entry.lufs_m.mean = momentary;
        entry.lufs_s.min = entry.lufs_s.max = entry.lufs_s.mean = shortTerm;
        const bool hot = index == 331 || index == 412 || index == 497 || index == 566;
        const auto peak = hot ? -0.6 : std::min (-1.3, momentary + 8.4 + 0.4 * std::sin (t * 2.0 * pi * 1.1));
        entry.true_peak.min = entry.true_peak.max = entry.true_peak.mean = std::min (-0.4, peak);
        entry.correlation.min = entry.correlation.max = entry.correlation.mean = 0.79 + 0.06 * std::sin (t * 0.9);
        entry.plr.min = entry.plr.max = entry.plr.mean = t < 28.0 ? 12.9 : 11.6;
    }
    return result;
}

// The latest values: the chorus, the needles a little under 0 VU.
inline KirinObservatoryFrame frame()
{
    auto result = compact_review::frame();
    auto& meter = result.meter;
    meter.lufs_m = -11.6; meter.lufs_s = -11.9; meter.lufs_i = -13.2; meter.lra = 6.8;
    meter.true_peak = -1.4; meter.max_true_peak = -0.6; meter.max_lufs_m = -9.8; meter.plr = 12.6;
    meter.balance_db = 0.2; meter.correlation = 0.81;
    const double vu[] { -18.7, -18.3 };
    for (int channel = 0; channel < 2; ++channel)
    {
        meter.sample_peak_dbfs[channel] = -2.2f + 0.2f * (float) channel;
        meter.sample_peak_hold_dbfs[channel] = -1.1f;
        meter.channel_true_peak_dbtp[channel] = -1.6f + 0.2f * (float) channel;
        meter.channel_instant_true_peak_dbtp[channel] = -1.7f + 0.2f * (float) channel;
        meter.channel_max_true_peak_dbtp[channel] = -0.7f + 0.1f * (float) channel;
        meter.channel_vu_dbfs[channel] = vu[channel];
    }
    constexpr auto centre = (int) KIRIN_STEREO_FIELD_SIZE / 2;
    for (int y = 0; y < (int) KIRIN_STEREO_FIELD_SIZE; ++y)
        for (int x = 0; x < (int) KIRIN_STEREO_FIELD_SIZE; ++x)
        {
            const auto side = (float) (x - centre) / 8.5f, mid = (float) (y - centre) / 15.0f;
            const auto density = 255.0f * std::exp (-(side * side + mid * mid));
            meter.field_density[(size_t) y * KIRIN_STEREO_FIELD_SIZE + (size_t) x] = (uint8_t) juce::jlimit (0, 255, (int) density);
        }
    meter.mono_sum_band_count = (uint8_t) KIRIN_MONO_SUM_BAND_COUNT;
    meter.mono_sum_approximate_below_hz = 40.0f;
    for (int band = 0; band < (int) KIRIN_MONO_SUM_BAND_COUNT; ++band)
        meter.mono_sum_db[band] = band < 8 ? -0.1f : -0.3f - 1.6f * (float) (band - 8) / 24.0f + 0.4f * (float) std::sin (band * 0.9);
    return result;
}

inline KirinWatchDisplay watch()
{
    auto result = compact_review::watch();
    result.current.lufs_m = -11.6; result.current.lufs_s = -11.9; result.current.true_peak = -1.4;
    result.current.crest = 11.4; result.current.psr = 10.5;
    result.maximum.lufs_m = -9.8; result.maximum.lufs_s = -10.9; result.maximum.true_peak = -0.6;
    result.maximum.crest = 15.2; result.maximum.psr = 13.1;
    return result;
}

// Six seconds at 10 Hz: the end of a build with a noise riser, a half-bar break, then the chorus
// lands and the limiter catches every hit. LUFS-M follows through its 400 ms window.
struct Moment { double lufs, peak, sharpness, delta; };

inline Moment moment (uint32_t index) noexcept
{
    const auto t = (double) index / 10.0;
    const auto hit = std::exp (-std::fmod (t * 2.0, 1.0) * 5.0);
    const auto grain = std::sin ((double) index * 2.3) * std::sin ((double) index * 0.7 + 1.0);
    if (t < 3.4)
    {
        const auto rise = t / 3.4;
        return { -17.5 + 4.0 * rise + 0.5 * hit + 0.3 * grain, -6.0 + 2.8 * rise + 1.8 * hit + 0.4 * grain,
                 1.30 + 0.85 * rise * rise + 0.06 * hit, 0.18 + 0.34 * rise * std::sqrt (rise) - 0.10 * hit + 0.04 * grain };
    }
    if (t < 4.0)
    {
        const auto fall = (t - 3.4) / 0.6;
        return { -13.2 - 13.0 * fall, -16.0 - 5.0 * fall + 0.6 * grain, 1.55 - 0.45 * fall + 0.03 * grain,
                 0.40 - 0.26 * fall + 0.03 * grain };
    }
    const auto settle = 1.0 - std::pow (1.0 - std::min (1.0, (t - 4.0) / 0.4), 2.0);
    const auto chorus = -11.4 + 0.7 * hit + 0.3 * grain;
    return { -26.2 + (chorus + 26.2) * settle, -1.0 - 2.4 * (1.0 - hit) + 0.3 * (1.0 - hit) * grain,
             1.68 + 0.22 * hit + 0.05 * grain, 0.44 - 0.38 * hit + 0.04 * grain };
}

inline KirinPerceptualBatch sharpness()
{
    auto batch = compact_review::sharpness();
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        const auto value = moment (index);
        auto& view = batch.frames[index];
        view.post_sharpness = value.sharpness;
        view.pre_sharpness = value.sharpness - value.delta;
        view.delta_sharpness = value.delta;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

inline KirinAbsoluteBatch live()
{
    auto batch = compact_review::live();
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        const auto value = moment (index);
        auto& view = batch.frames[index];
        view.lufs_m = value.lufs;
        view.true_peak = value.peak;
        view.sharpness = value.sharpness;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

// A long-term music spectrum: low end around 70 Hz, about -4.2 dB per octave, a little presence
// near 3 kHz and an air roll-off. lift adds low end and air (the reference side).
inline float musicDb (double hz, double lift, double mids = 0.0) noexcept
{
    const auto octave = std::log2 (hz / 1'000.0);
    auto db = -27.0 - 4.2 * octave + (6.0 + lift) * std::exp (-std::pow (std::log2 (hz / 70.0), 2.0) / 0.72)
            + 1.8 * std::exp (-std::pow (std::log2 (hz / 3'000.0), 2.0) / 0.5) + 0.6 * std::sin (octave * 3.1)
            + mids * std::exp (-std::pow (std::log2 (hz / 1'400.0), 2.0) / 1.1);
    if (hz < 38.0) db -= 9.0 * std::log2 (38.0 / hz);
    if (hz > 12'000.0) db -= (10.0 - 4.0 * lift) * std::log2 (hz / 12'000.0);
    return (float) db;
}

inline std::shared_ptr<reference_audition::KirinSpectrumWindow> window (int bands, double first, double ratio,
                                                                         double lift, std::array<double, 4> balance,
                                                                         double mids = 0.0)
{
    auto result = std::make_shared<reference_audition::KirinSpectrumWindow>();
    for (int band = 0; band < bands; ++band)
    {
        const auto hz = first * std::pow (ratio, band);
        result->centersHz.push_back (hz);
        const auto level = musicDb (hz, lift, mids);
        result->medianDb.push_back (level);
        result->p10Db.push_back (level - 3.5f);
        result->p90Db.push_back (level + 3.5f);
    }
    result->balanceDb = balance;
    result->frames = result->wantedFrames = 300;
    return result;
}

inline std::shared_ptr<reference_audition::KirinSpectrumWindow> cueWindow (double lift, std::array<double, 4> balance,
                                                                            double mids = 0.0)
{
    return window (64, 20.0, std::pow (1'000.0, 1.0 / 63.0), lift, balance, mids);
}

// LUFS-S of a four-minute song: intro, verse, chorus, verse, chorus, bridge, last chorus, outro.
inline double song (double t) noexcept
{
    const auto base = t < 14.0 ? -19.0 : t < 46.0 ? -15.5 : t < 78.0 ? -11.4 : t < 110.0 ? -15.0
                    : t < 142.0 ? -11.1 : t < 158.0 ? -16.2 : t < 190.0 ? -10.7 : -10.7 - 1.3 * (t - 190.0);
    return base + 0.7 * std::sin (t * 2.0 * pi * 0.5) * std::sin (t * 2.0 * pi * 0.11) + 0.4 * std::sin (t * 1.7);
}

inline std::shared_ptr<const reference_audition::VisualTimeline> versionTimeline()
{
    constexpr int seconds = 200;
    auto source = std::make_shared<reference_audition::RuntimeSource>();
    source->audio = { 48'000, 2, 48'000LL * seconds };
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->waveform.emplace();
    measurement->waveform->framesPerBin = 4'800;
    measurement->waveform->samplePeakMillidbfs.resize (2);
    measurement->waveform->rmsMillidbfs.resize (2);
    auto result = std::make_shared<reference_audition::VisualTimeline>();
    result->binding.source = source; result->binding.overview = measurement;
    result->binding.key = "readme-version"; result->binding.channels = 2;
    result->binding.hostRate = 48'000; result->binding.aligned = true;
    result->pass = result->revision = 1; result->hop = 4'800;
    for (int index = 0; index < seconds * 10; ++index)
    {
        const auto t = index / 10.0;
        const auto v = song (t);
        const auto chorus = v > -12.5;
        const auto a = v + (chorus ? 0.6 : -0.3);
        reference_audition::VisualPairBin pair;
        pair.pass = 1; pair.a.frames = pair.b.frames = 4'800;
        pair.a.short_lufs = a; pair.b.short_lufs = v;
        pair.a.crest_db = chorus ? 9.6 : 11.8; pair.b.crest_db = chorus ? 10.4 : 12.3;
        const auto beat = 0.5 + 0.5 * std::abs (std::sin (t * 2.0 * pi * 2.0) * std::cos (t * 2.0 * pi * 0.37));
        const auto grain = 0.88 + 0.12 * std::sin (t * 47.3) * std::sin (t * 11.9);
        const auto amplitude = juce::jlimit (0.02, 0.98, std::pow (10.0, (v + 14.0) / 20.0) * (0.4 + 0.55 * beat) * grain);
        for (size_t channel = 0; channel < 2; ++channel)
        {
            const auto side = channel == 0 ? 1.0 : 0.94;
            pair.a.peak[channel] = amplitude * side * 1.04; pair.b.peak[channel] = amplitude * side;
            pair.a.rms[channel] = amplitude * side * 0.42; pair.b.rms[channel] = amplitude * side * 0.4;
            measurement->waveform->samplePeakMillidbfs[channel].push_back (std::llround (20'000.0 * std::log10 (amplitude * side)));
            measurement->waveform->rmsMillidbfs[channel].push_back (std::llround (20'000.0 * std::log10 (amplitude * side * 0.4)));
        }
        result->bins.push_back (pair);
    }
    return result;
}

inline reference_ui::State roles()
{
    auto state = reference_review::playing (reference_review::library());
    state.separateComparisons = true;
    state.presets = { { "mastering", "Mastering   1 / 2" }, { "mix", "MIX   2 / 2" } };
    state.presetId = "mastering";
    state.versions = { { "v4", "Mix v4" }, { "v3", "Mix v3" }, { "v2", "Mix v2" } };
    state.versionId = "v4";
    state.versionReady = state.checkReady = state.referenceReady = state.referenceArmable = true;
    state.versionStep = state.checkStep = state.referenceStep = reference_ui::SourceStep::ready;
    state.aWindowLoudness = -11.6;
    state.aIntegratedLoudness = -13.2;
    return state;
}

inline reference_ui::State checkPage()
{
    auto state = roles();
    state.comparisonSlot = 2;
    state.comparisonMode = "loudness_match";
    state.checks = { { "tone/ref-1", "Tonal balance  /  Reference 01" }, { "tone/ref-2", "Tonal balance  /  Reference 02" },
                     { "loud/ref-1", "Loudness  /  Reference 01" }, { "peak/ref-1", "True Peak  /  Reference 01" },
                     { "dyn/ref-1", "Dynamics  /  Reference 01" } };
    state.checkId = "tone/ref-1";
    state.viewBindings = { "spectrum_full" };
    state.aKirin = cueWindow (-1.5, { -21.2, -12.4, -16.9, -30.6 }, 2.2);
    state.cueKirin = cueWindow (2.5, { -18.6, -13.5, -17.1, -27.8 }, -0.5);
    state.cueLoudness = -9.1;
    state.cueStartSeconds = 62.0; state.cueEndSeconds = 92.0; state.sourceDurationSeconds = 236.0; state.cueLoops = true;
    state.bSelected = true; state.audibleComparisonSlot = 2;
    state.appliedGainDb = -2.5; state.cuePlayheadSeconds = 74.0;
    state.status = "C AUDITION / PRE " + juce::String (juce::CharPointer_UTF8 ("\xce\x94")) + " PAUSED";
    return state;
}

inline reference_ui::State songPage()
{
    auto state = roles();
    state.comparisonSlot = 3;
    state.songSets = { { "set-1", "Mastering refs   1 / 2" }, { "set-2", "Album context   2 / 2" } };
    state.songSetId = "set-1";
    state.songs.clear();
    state.songFacts.clear();
    const double loudness[] { -9.2, -10.1, -8.8, -11.4, -9.7 };
    const double lift[] { 1.2, 0.4, 1.8, -0.6, 0.9 };
    for (int index = 0; index < 5; ++index)
    {
        state.songs.push_back ({ "set-1/song-" + juce::String (index + 1), "Reference 0" + juce::String (index + 1) });
        reference_ui::SongFact fact;
        fact.lufsI = loudness[index]; fact.prepared = true;
        for (int band = 0; band < 12; ++band)
        {
            const auto hz = 25.0 * std::pow (1.8, band);
            fact.centersHz.push_back (hz);
            fact.medianDb.push_back (musicDb (hz, lift[index]));
        }
        fact.part = reference_audition::CuePart::chorus;
        fact.partStartSeconds = 62.0 + index * 4.0; fact.partEndSeconds = 84.0 + index * 4.0;
        state.songFacts.push_back (fact);
    }
    state.songId = state.songs.front().id;
    state.aKirin = window (12, 25.0, 1.8, -1.5, { -21.2, -12.4, -16.9, -30.6 }, 2.2);
    state.aWindowBlocks = state.aWindowNeededBlocks = 300;
    state.status = "READY / A REMAINS LIVE";
    return state;
}

inline reference_ui::State versionPage()
{
    auto state = roles();
    state.comparisonSlot = 1;
    state.visualTimeline = versionTimeline();
    state.visualPositionSeconds = 126.0;
    state.bSelected = true; state.audibleComparisonSlot = 1; state.appliedGainDb = -0.6;
    state.status = "V AUDITION / PRE " + juce::String (juce::CharPointer_UTF8 ("\xce\x94")) + " PAUSED";
    return state;
}

inline bool write()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_README_MEDIA_DIR");
    if (path == nullptr) return true;
    const juce::File directory { path };
    if (! directory.createDirectory()) return false;
    const i18n::ScopedLanguage language (i18n::Language::english);
    using namespace compact_review;
    material_cache::Lifetime editorMaterial;
    bool written = true;
    const auto save = [&] (const char* name, const juce::Image& image) {
        written = written && freq_showcase::writePng (directory.getChildFile (juce::String (name) + ".png"), image); };
    constexpr int width = 900, height = 600;
    observatory::View shell (observatory::Role::post);
    shell.setSize (width, height);
    shell.setObservatoryFrame (frame(), true);
    shell.setWatchDisplay (watch(), true);
    shell.setHistory (history());
    shell.setConnection ("PAIR MIX BUS", COL_LED_BLUE, observatory::ConnectionState::paired);
    shell.setMeterContext (meter_context::MeterContext::twoMix);
    shell.setDomain (observatory::Domain::level);
    shell.setManualHybridVuVisible (true);
    save ("vu", renderShell (shell));
    shell.setManualHybridVuVisible (false);
    save ("level", renderShell (shell));
    {
        observatory::LiveCompareFooter listen;
        listen.entryEnabled = listen.active = listen.preSelected = listen.matched = true;
        listen.preGainTenthsDb = 18;
        shell.setLiveCompareFooter (listen);
        save ("listen", renderShell (shell));
        shell.setLiveCompareFooter ({});
    }
    shell.setDomain (observatory::Domain::time);
    save ("time", renderShell (shell));
    {
        // DRUM is offered on a track or stem, not on the 2MIX.
        shell.setMeterContext (meter_context::MeterContext::trackStem);
        shell.setConnection ("PAIR DRUM BUS", COL_LED_BLUE, observatory::ConnectionState::paired);
        auto attack = drum();
        attack->presentationTickAt (60'000.0);
        save ("drum", compose (shell, *attack, analysis_navigation::Page::attack));
        shell.setMeterContext (meter_context::MeterContext::twoMix);
        shell.setConnection ("PAIR MIX BUS", COL_LED_BLUE, observatory::ConnectionState::paired);
        PerceptualComponent sharp;
        sharp.setSignalActive (true);
        sharp.setBatch (sharpness());
        sharp.presentationTickAt (60'000.0);
        save ("sharp", compose (shell, sharp, analysis_navigation::Page::perceptual));
        AbsoluteComponent timeline;
        timeline.setSignalActive (true);
        timeline.setBatchAt (live(), 60'000.0);
        save ("live", compose (shell, timeline, analysis_navigation::Page::absolute));
    }
    shell.setDomain (observatory::Domain::frequency);
    {
        SpectrumComponent absolute;
        absolute.setAbsoluteObservation (true);
        absolute.setSignalActive (true);
        for (int index = 0; index < 240; ++index)
            absolute.setSnapshot (freq_history_review::frame (index, freq_history_review::mixDbfs));
        save ("freq", compose (shell, absolute, analysis_navigation::Page::spectrum));
    }
    shell.setDomain (observatory::Domain::space);
    save ("space", renderShell (shell));
    shell.setDomain (observatory::Domain::reference);
    {
        reference_ui::Component reference;
        reference.setPresentationContext (presentation::forEditor (width, height));
        reference.setState (songPage());
        save ("ref_b", compose (shell, reference, analysis_navigation::Page::meters));
        reference.setState (checkPage());
        save ("ref_c", compose (shell, reference, analysis_navigation::Page::meters));
        reference.setState (versionPage());
        save ("ref_v", compose (shell, reference, analysis_navigation::Page::meters));
    }
    juce::Component root;
    root.getProperties().set (key_light::rootProperty, true);
    root.setSize (width, height);
    live_blind_ui::Component blind;
    root.addAndMakeVisible (blind);
    blind.setBounds (root.getLocalBounds());
    const auto paintBlind = [&] (const char* name) {
        juce::Image image (juce::Image::ARGB, width * 2, height * 2, true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        blind.paintEntireComponent (g, true);
        save (name, image);
    };
    for (const bool revealed : { false, true })
    {
        live_compare::LiveBlindStatus state;
        state.stage = live_compare::BlindStage::active;
        state.trial.active = true;
        state.trial.audible = 2;
        state.trial.played = 3;
        state.trial.firstPre = true;
        state.trial.revealed = revealed;
        blind.setState (state, true, 1.0f);
        paintBlind (revealed ? "blind_result" : "blind");
    }
    return written;
}
}
