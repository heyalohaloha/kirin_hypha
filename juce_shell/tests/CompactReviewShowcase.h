#pragma once

#include "../src/HyphaAbsoluteComponent.h"
#include "../src/HyphaAttackComponent.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaTimePageNavigation.h"
#include "SpectrumTerrainShowcase.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

// Look review renders of every page as the editor composes it, with steady data, at DPI 2: the
// compact sizes (100% and 125%) the editor is mostly used at, and 200% beside them. Written only
// when KIRIN_HYPHA_COMPACT_REVIEW_DIR names a directory.
namespace hypha::tests
{
namespace compact_review
{
constexpr float dpi = 2.0f;

inline KirinObservatoryFrame frame()
{
    KirinObservatoryFrame result {};
    result.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    result.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    result.lra_state = KIRIN_LRA_READY;
    result.delta_available = 1u;
    result.comparison_state = KIRIN_COMPARISON_STATE_ACTIVE;
    result.comparison_reason = KIRIN_COMPARISON_REASON_NONE;
    result.comparison_generation = result.comparison_identity = 1u;
    result.lra_elapsed_seconds = 272.0;
    auto& meter = result.meter;
    meter.generation = 7;
    meter.active_frames = meter.observed_frames = 48'000ull * 272ull;
    meter.sample_rate = 48'000;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -13.8;
    meter.lufs_s = -14.2;
    meter.lufs_i = -14.3;
    meter.lra = 6.4;
    meter.true_peak = -3.6;
    meter.max_true_peak = -1.2;
    meter.max_lufs_m = -10.6;
    meter.plr = 13.1;
    meter.channels = 2;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.balance_db = 0.4;
    meter.correlation = 0.74;
    for (int channel = 0; channel < 2; ++channel)
    {
        meter.sample_peak_dbfs[channel] = -5.1f - 0.4f * (float) channel;
        meter.sample_peak_hold_dbfs[channel] = -2.8f;
        meter.channel_true_peak_dbtp[channel] = -3.9f;
        meter.channel_max_true_peak_dbtp[channel] = -1.2f - 0.3f * (float) channel;
    }
    meter.field_size = KIRIN_STEREO_FIELD_SIZE;
    meter.field_observation_count = 30;
    constexpr auto centre = (int) KIRIN_STEREO_FIELD_SIZE / 2;
    for (int y = 0; y < (int) KIRIN_STEREO_FIELD_SIZE; ++y)
        for (int x = 0; x < (int) KIRIN_STEREO_FIELD_SIZE; ++x)
        {
            // A mix that is mostly centred with some width: an ellipse along the MID axis.
            const auto side = (float) (x - centre) / 6.0f;
            const auto mid = (float) (y - centre) / 16.0f;
            const auto density = 255.0f * std::exp (-(side * side + mid * mid));
            meter.field_density[(size_t) y * KIRIN_STEREO_FIELD_SIZE + (size_t) x]
                = (uint8_t) juce::jlimit (0, 255, (int) density);
        }
    result.delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    result.delta.lufs = 1.1;
    result.delta.lufs_s = 0.8;
    result.delta.true_peak = 0.4;
    result.delta.crest = -1.6;
    return result;
}

inline KirinWatchDisplay watch()
{
    KirinWatchDisplay result {};
    result.current.lufs_m = -13.8;
    result.current.lufs_s = -14.2;
    result.current.true_peak = -3.6;
    result.current.crest = 12.7;
    result.maximum.lufs_m = -9.4;
    result.maximum.lufs_s = -10.1;
    result.maximum.true_peak = -1.2;
    result.maximum.crest = 16.3;
    return result;
}

// Thirty seconds of 10 Hz history: a verse, a louder chorus and a few hot peaks.
inline std::vector<KirinMeterHistoryEntry> history()
{
    std::vector<KirinMeterHistoryEntry> result (300);
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
        const auto chorus = t > 14.0 ? 3.0 : 0.0;
        const auto beat = std::sin (t * 2.0 * juce::MathConstants<double>::pi * 2.0);
        entry.lufs_m.min = entry.lufs_m.max = entry.lufs_m.mean = -17.0 + chorus + 2.2 * beat;
        entry.lufs_s.min = entry.lufs_s.max = entry.lufs_s.mean = -16.5 + chorus * std::min (1.0, (t - 14.0) / 3.0 + 1.0);
        const auto peak = (index % 47 == 13) ? -1.3 : -4.5 + chorus * 0.6 + 0.8 * beat;
        entry.true_peak.min = entry.true_peak.max = entry.true_peak.mean = peak;
        entry.correlation.min = entry.correlation.max = entry.correlation.mean = 0.74 + 0.05 * beat;
        entry.plr.min = entry.plr.max = entry.plr.mean = 12.5 - chorus * 0.4;
    }
    return result;
}

inline KirinPerceptualBatch sharpness()
{
    KirinPerceptualBatch batch {};
    batch.count = 60u;
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        auto& view = batch.frames[index];
        const auto t = (double) index / 10.0;
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = 1u;
        view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
        view.channels = 2u;
        view.sample_rate = 48'000u;
        view.aperture_samples = 4'800u;
        view.pre_sharpness = 1.25 + 0.25 * std::sin (t * 3.1);
        view.post_sharpness = view.pre_sharpness + 0.32 + 0.12 * std::sin (t * 1.7);
        view.delta_sharpness = view.post_sharpness - view.pre_sharpness;
        view.presentation_end_samples = 288'000 + (int64_t) index * 4'800;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

inline KirinAbsoluteBatch live()
{
    KirinAbsoluteBatch batch {};
    batch.count = 60u;
    for (uint32_t index = 0; index < batch.count; ++index)
    {
        auto& view = batch.frames[index];
        const auto t = (double) index / 10.0;
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = 1u;
        view.channels = 2u;
        view.sample_rate = 48'000u;
        view.aperture_samples = 4'800u;
        view.lufs_m = -14.5 + 2.5 * std::sin (t * 2.4);
        view.true_peak = -3.5 + 2.0 * std::sin (t * 2.4 + 0.6);
        view.sharpness = 1.55 + 0.3 * std::sin (t * 3.1);
        view.presentation_end_samples = 288'000 + (int64_t) index * 4'800;
        view.generation = 11;
    }
    batch.latest = batch.frames[batch.count - 1u];
    return batch;
}

inline std::unique_ptr<AttackComponent> drum()
{
    auto events = std::make_unique<KirinAttackEventBatch>();
    auto details = std::make_unique<KirinAttackDetailBatch>();
    auto preDetails = std::make_unique<KirinAttackDetailBatch>();
    auto pairs = std::make_unique<KirinAttackPairEventBatch>();
    auto waveform = std::make_unique<KirinAttackWaveformBatch>();
    auto preWaveform = std::make_unique<KirinAttackWaveformBatch>();
    constexpr uint32_t hits = 12u;
    events->count = details->count = pairs->count = hits;
    pairs->status = KIRIN_SPECTRUM_ACTIVE;
    for (uint32_t index = 0; index < hits; ++index)
    {
        const auto at = 12'000 + (int64_t) index * 24'000;
        auto& d = details->details[index];
        d.generation = 7; d.sample_rate = 48'000u; d.channels = 2u;
        d.event_sample = at; d.bin_frames = 48u;
        d.shape_start_sample = at - 20 * 48; d.shape_end_sample = at + 130 * 48; d.body_end_sample = at + 130 * 48;
        d.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY; d.complete = 1u;
        d.transient_available = 1u; d.transient_db = 7.0f + (float) (index % 3);
        d.attack_rms_dbfs = -16.0f + (float) (index % 4); d.body_rms_dbfs = -26.0f;
        d.sample_peak_dbfs = -3.0f; d.crest_db = 7.0f;
        d.sharpness_available = 1u; d.sharpness_acum = 1.4f + 0.1f * (float) (index % 3);
        for (uint32_t bin = 0; bin < d.shape_count; ++bin)
            d.shape[bin] = bin < 13u ? 0.03f : 0.82f * std::exp (-(float) (bin - 13u) / 8.0f) + 0.02f;
        auto& e = events->events[index]; e.generation = 7; e.sample_rate = 48'000u; e.event_sample = at;
        auto& p = pairs->events[index]; p.pre_generation = p.post_generation = 7;
        p.pre_available = p.post_available = 1u; p.sample_rate = 48'000u;
        p.event_sample = p.pre_event_sample = p.post_event_sample = at;
    }
    *preDetails = *details;
    for (uint32_t index = 0; index < hits; ++index)
    {
        auto& pre = preDetails->details[index];
        pre.transient_db -= 2.5f; pre.attack_rms_dbfs -= 3.0f; pre.crest_db += 1.0f; pre.sharpness_acum -= 0.25f;
    }
    waveform->count = preWaveform->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (uint32_t index = 0; index < waveform->count; ++index)
    {
        auto& p = waveform->points[index];
        p.generation = 7; p.sample_rate = 48'000u; p.channels = 2u;
        p.start_sample = (int64_t) index * 480; p.end_sample = p.start_sample + 480;
        const auto sinceHit = (int64_t) ((p.start_sample + 12'000) % 24'000);
        p.rms_dbfs = -40.0f + 30.0f * std::exp (-(float) sinceHit / 6'000.0f);
        preWaveform->points[index] = p;
        preWaveform->points[index].rms_dbfs -= 3.0f;
    }
    KirinAttackStats stats {}; stats.available = stats.enabled = stats.worker_running = 1u;
    auto component = std::make_unique<AttackComponent>();
    component->setSnapshot (*events, *waveform, *details, *preWaveform, *preDetails, *pairs,
                            288'000, 48'000u, 7u, stats);
    component->setOverlayMode (false);
    return component;
}

inline juce::Image renderShell (observatory::View& shell)
{
    juce::Image image (juce::Image::ARGB, (int) std::ceil ((float) shell.getWidth() * dpi),
                       (int) std::ceil ((float) shell.getHeight() * dpi), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (dpi));
    shell.paintEntireComponent (g, true);
    return image;
}

inline void paintInto (juce::Image& image, juce::Component& body, juce::Rectangle<int> bounds)
{
    body.setSize (bounds.getWidth(), bounds.getHeight());
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::translation ((float) bounds.getX(), (float) bounds.getY())
                        .scaled (dpi));
    body.paintEntireComponent (g, true);
}

inline void presentAt (juce::Component& body, int width, int height)
{
    const auto context = presentation::forEditor (width, height);
    if (auto* attack = dynamic_cast<AttackComponent*> (&body)) attack->setPresentationContext (context);
    else if (auto* absolute = dynamic_cast<AbsoluteComponent*> (&body)) absolute->setPresentationContext (context);
    else if (auto* perceptual = dynamic_cast<PerceptualComponent*> (&body)) perceptual->setPresentationContext (context);
    else if (auto* spectrum = dynamic_cast<SpectrumComponent*> (&body)) spectrum->setPresentationContext (context);
}

// The TIME analysis pages sit under the TIME page navigation, as in the editor.
inline juce::Image compose (observatory::View& shell, juce::Component& body,
                            analysis_navigation::Page page)
{
    presentAt (body, shell.getWidth(), shell.getHeight());
    shell.setExternalAnalysisBodyActive (true);
    if (shell.domain() == observatory::Domain::time)
    {
        shell.setAnalysisPage (page);
        if (auto* attack = dynamic_cast<AttackComponent*> (&body))
            shell.setAttackPaired (attack->pairedObservation());
    }
    auto image = renderShell (shell);
    if (shell.domain() == observatory::Domain::time)
    {
        TimePageNavigation navigation;
        navigation.setPresentationContext (presentation::forEditor (shell.getWidth(), shell.getHeight()));
        navigation.setDirect (shell.getWidth() >= 450);
        navigation.setPage (page);
        paintInto (image, navigation, shell.timeNavigationBounds());
    }
    paintInto (image, body, shell.analysisBodyBounds());
    shell.setExternalAnalysisBodyActive (false);
    return image;
}
}

inline bool writeCompactReview()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_COMPACT_REVIEW_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    using namespace compact_review;
    material_cache::Lifetime editorMaterial;
    bool written = true;
    const auto write = [&] (const juce::String& name, const juce::Image& image) {
        written = written && freq_showcase::writePng (directory.getChildFile (name + ".png"), image);
    };
    for (const auto& [width, height] : { std::array<int, 2> { 300, 200 }, std::array<int, 2> { 375, 250 },
                                         std::array<int, 2> { 600, 400 } })
    {
        const auto prefix = juce::String (width) + "_";
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
        {
            observatory::View shell (role);
            shell.setSize (width, height);
            shell.setObservatoryFrame (frame(), true);
            shell.setWatchDisplay (watch(), true);
            shell.setHistory (history());
            const bool post = role == observatory::Role::post;
            shell.setConnection (post ? "PAIR DRUM" : "SOURCE PRE", COL_LED_BLUE,
                                 observatory::ConnectionState::paired);
            const auto rolePrefix = prefix + (post ? "post_" : "pre_");
            for (const auto& [domain, name] : { std::pair { observatory::Domain::level, "level" },
                                                std::pair { observatory::Domain::time, "time_history" },
                                                std::pair { observatory::Domain::space, "space" },
                                                std::pair { observatory::Domain::reference, "ref" } })
            {
                if (! post && domain == observatory::Domain::reference)
                    continue;
                shell.setDomain (domain);
                write (rolePrefix + name, renderShell (shell));
            }
            if (! post)
                continue;
            shell.setDomain (observatory::Domain::level);
            shell.setTarget (observatory::ObservationTarget::delta);
            write (rolePrefix + "level_delta", renderShell (shell));
            shell.setTarget (observatory::ObservationTarget::absolute);

            shell.setDomain (observatory::Domain::frequency);
            {
                SpectrumComponent delta;
                delta.setSignalActive (true);
                for (int index = 0; index < freq_showcase::frameCount; ++index)
                    delta.setSnapshot (freq_showcase::frame (index));
                write (rolePrefix + "freq_delta", compose (shell, delta, analysis_navigation::Page::spectrum));
                SpectrumComponent absolute;
                absolute.setAbsoluteObservation (true);
                absolute.setSignalActive (true);
                for (int index = 0; index < freq_showcase::frameCount; ++index)
                    absolute.setSnapshot (freq_showcase::frame (index));
                write (rolePrefix + "freq_post", compose (shell, absolute, analysis_navigation::Page::spectrum));
            }
            shell.setDomain (observatory::Domain::time);
            {
                PerceptualComponent sharp;
                sharp.setSignalActive (true);
                sharp.setBatch (sharpness());
                sharp.presentationTickAt (60'000.0);
                write (rolePrefix + "time_sharp", compose (shell, sharp, analysis_navigation::Page::perceptual));
                AbsoluteComponent timeline;
                timeline.setSignalActive (true);
                timeline.setBatchAt (live(), 60'000.0);
                write (rolePrefix + "time_live", compose (shell, timeline, analysis_navigation::Page::absolute));
                auto attack = drum();
                attack->presentationTickAt (60'000.0);
                write (rolePrefix + "time_drum", compose (shell, *attack, analysis_navigation::Page::attack));
            }
        }
        // The status and feedback lines, which share the cycle's row where the footer folds.
        observatory::View waiting (observatory::Role::post);
        waiting.setSize (width, height);
        write (prefix + "post_waiting", renderShell (waiting));
        waiting.setObservatoryFrame (frame(), true);
        waiting.setFeedback ("Jungle Mode changed for this session only");
        write (prefix + "post_feedback", renderShell (waiting));
        // A Keep in progress adds STOP beside VU and MENU.
        waiting.setFeedback ({});
        waiting.setKeepActive (true);
        write (prefix + "post_keep", renderShell (waiting));
    }
    return written;
}
}
