#pragma once

#include "AttackUiImageHelpers.h"
#include "AttackUiLaneContract.h"
#include "../src/HyphaObservatoryContract.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

// Look review renders. A drum bus at 120 BPM (kick and snare on alternate quarter notes, hats in
// between) measured before and after a mastering chain, rendered at the shipped editor sizes.
// Written only when KIRIN_ATTACK_UI_SHOWCASE_DIR names a directory.
namespace hypha::attack_ui_test
{
namespace showcase
{
constexpr std::uint32_t rate = 48'000;
constexpr int hitCount = 12;

inline float jitter (int hit, int salt) noexcept
{
    auto h = static_cast<std::uint32_t> (hit) * 747'796'405u + static_cast<std::uint32_t> (salt) * 2'891'336'453u
           + 12'345u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return static_cast<float> (h & 0xffffu) / 32'767.5f - 1.0f;
}

inline std::int64_t onset (int hit) noexcept
{
    return 14'400 + static_cast<std::int64_t> (hit) * 24'000; // 0.3 s, then every 0.5 s
}

struct Voice
{
    float attackRms, bodyRms, transient, peak, crest, sharpness, amplitude, tau, ring;
};

inline Voice voice (int hit, bool post) noexcept
{
    const bool snare = hit % 2 == 1;
    auto v = snare ? Voice { -12.5f, -24.0f, 11.5f, -3.0f, 9.5f, 1.85f, 0.30f, 0.060f, 0.07f }
                   : Voice { -11.0f, -21.0f, 10.0f, -3.5f, 7.5f, 0.95f, 0.36f, 0.090f, 0.0f };
    v.attackRms += 0.8f * jitter (hit, 1);
    v.transient += 0.9f * jitter (hit, 2);
    v.crest += 0.7f * jitter (hit, 3);
    v.sharpness += 0.07f * jitter (hit, 4);
    if (! post)
    {
        // Before the chain. A transient shaper lifts the snare attack, a compressor holds the
        // kick attack back, the chain adds level and a brighter top.
        v.attackRms -= 3.4f + 0.5f * jitter (hit, 5);
        v.bodyRms -= 2.6f;
        v.transient -= snare ? 6.0f + 1.2f * jitter (hit, 6) : -(3.0f + 1.0f * jitter (hit, 6));
        v.peak -= 1.0f;
        v.crest -= snare ? 1.6f + 0.6f * jitter (hit, 7) : -(2.6f + 0.6f * jitter (hit, 7));
        v.sharpness -= 0.32f + 0.10f * jitter (hit, 8);
        v.amplitude *= snare ? 0.62f : 0.80f;
        v.tau *= 1.12f;
    }
    return v;
}

inline KirinAttackDetail detail (int hit, bool post)
{
    const auto v = voice (hit, post);
    KirinAttackDetail d {};
    d.generation = post ? 7 : 5;
    d.sample_rate = rate;
    d.channels = 2;
    d.event_sample = onset (hit);
    d.bin_frames = 48;
    const auto start = d.event_sample / 48 * 48;
    d.shape_start_sample = start - 20 * 48;
    d.shape_end_sample = d.body_end_sample = start + 130 * 48;
    d.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    d.complete = d.transient_available = d.sharpness_available = 1;
    d.transient_db = v.transient;
    d.body_rms_dbfs = v.bodyRms;
    d.attack_rms_dbfs = v.attackRms;
    d.sample_peak_dbfs = v.peak;
    d.crest_db = v.crest;
    d.sharpness_acum = v.sharpness;
    const auto peak = std::pow (10.0f, v.peak / 20.0f);
    for (std::uint32_t i = 0; i < d.shape_count; ++i)
    {
        const auto ms = static_cast<float> (i) * 150.0f / static_cast<float> (d.shape_count) - 20.0f;
        auto level = 0.018f + 0.004f * std::sin (static_cast<float> (i) * 1.7f);
        if (ms >= 0.0f)
        {
            const auto rise = std::min (1.0f, (ms + 0.8f) / 2.4f);
            level += peak * rise * std::exp (-ms / (v.tau * 550.0f));
            level += v.ring * peak * 1.6f * std::exp (-ms / 110.0f)
                   * (0.6f + 0.4f * std::cos (ms * 0.9f));
        }
        d.shape[i] = level;
    }
    return d;
}

inline void fillWaveform (KirinAttackWaveformBatch& batch, bool post)
{
    batch.count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (std::uint32_t i = 0; i < batch.count; ++i)
    {
        auto& p = batch.points[i];
        p.generation = post ? 7 : 5;
        p.sample_rate = rate;
        p.channels = 2;
        p.start_sample = static_cast<std::int64_t> (i) * 480;
        p.end_sample = p.start_sample + 480;
        const auto t = (static_cast<float> (p.start_sample) + 240.0f) / static_cast<float> (rate);
        auto power = post ? 0.0060f * 0.0060f : 0.0050f * 0.0050f;
        for (int hit = 0; hit < hitCount; ++hit)
        {
            const auto v = voice (hit, post);
            const auto dt = t - static_cast<float> (onset (hit)) / static_cast<float> (rate);
            if (dt >= 0.0f)
            {
                const auto body = v.amplitude * std::exp (-dt / v.tau)
                                + 0.5f * v.ring * std::exp (-dt / 0.18f);
                power += body * body;
            }
            const auto hat = dt - 0.25f; // the off-beat hat
            if (hat >= 0.0f)
            {
                const auto level = (post ? 0.030f : 0.024f) * std::exp (-hat / 0.030f);
                power += level * level;
            }
        }
        p.rms_dbfs = 10.0f * std::log10 (power);
    }
}

struct DrumScene
{
    LaneFixture fixture;
    std::unique_ptr<KirinAttackWaveformBatch> preWaveform = std::make_unique<KirinAttackWaveformBatch>();

    void submit (AttackComponent& component) const
    {
        component.setSnapshot (*fixture.events, *fixture.waveform, *fixture.post, *preWaveform,
                               *fixture.pre, *fixture.pairs, LaneFixture::latest, rate, 7,
                               fixture.stats);
    }
};

inline DrumScene drumScene()
{
    DrumScene scene;
    auto& f = scene.fixture;
    f.stats.available = f.stats.enabled = f.stats.worker_running = 1;
    f.pairs->status = KIRIN_SPECTRUM_ACTIVE;
    for (int hit = 0; hit < hitCount; ++hit)
    {
        auto& event = f.events->events[f.events->count++];
        event.generation = 7;
        event.sample_rate = rate;
        event.event_sample = onset (hit);
        f.post->details[f.post->count++] = detail (hit, true);
        f.pre->details[f.pre->count++] = detail (hit, false);
        auto& pair = f.pairs->events[f.pairs->count++];
        pair.sample_rate = rate;
        pair.channels = 2;
        pair.pre_generation = 5;
        pair.post_generation = 7;
        pair.pre_available = pair.post_available = pair.delta_available = 1;
        pair.event_sample = pair.pre_event_sample = pair.post_event_sample = onset (hit);
    }
    fillWaveform (*f.waveform, true);
    fillWaveform (*scene.preWaveform, false);
    return scene;
}

// The Observatory body of one POST editor size, as the shell lays it out without a Guide.
inline juce::Image render (const DrumScene& scene, int editorWidth, bool overlay, float dpi)
{
    const observatory::SizePreset preset { editorWidth, editorWidth * 2 / 3,
                                           observatory::densityForWidth (editorWidth), "" };
    const auto body = observatory::shellLayout (observatory::Role::post, preset,
                                                observatory::GuidePresence::absent).body;
    auto component = std::make_unique<AttackComponent>();
    component->setPresentationContext (presentation::forEditor (preset.width, preset.height));
    component->setSize (body.width, body.height - observatory::timeNavigationHeight (preset.density));
    component->setOverlayMode (overlay);
    scene.submit (*component);
    // Inspecting the eighth hit (3.8 s): the hypha stands inside the six seconds.
    component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    for (int step = 0; step < 7; ++step)
        component->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    return renderAttack (*component, dpi);
}

inline bool writePng (const juce::File& file, const juce::Image& image)
{
    file.deleteFile();
    juce::FileOutputStream output { file };
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}
}

inline bool writeAttackShowcase()
{
    const auto* path = std::getenv ("KIRIN_ATTACK_UI_SHOWCASE_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    const auto scene = showcase::drumScene();
    struct Shot { const char* name; int width; bool overlay; };
    constexpr std::array shots { Shot { "900_overlay", 900, true }, Shot { "900_rows", 900, false },
                                 Shot { "600_overlay", 600, true }, Shot { "450_overlay", 450, true },
                                 Shot { "300_overlay", 300, true } };
    bool written = true;
    for (const auto& shot : shots)
        for (const auto dpi : { 1.0f, 2.0f })
            written = written && showcase::writePng (
                directory.getChildFile (juce::String (shot.name) + (dpi > 1.0f ? "@2x.png" : ".png")),
                showcase::render (scene, shot.width, shot.overlay, dpi));
    return written;
}
}
