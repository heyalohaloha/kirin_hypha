#pragma once

#include "AttackUiImageHelpers.h"
#include "../src/HyphaAttackLanePainter.h"
#include "../src/HyphaObservatoryContract.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace hypha::attack_ui_test
{
inline KirinAttackDetail laneDetail (std::int64_t sample, std::uint64_t generation = 7)
{
    KirinAttackDetail detail {};
    detail.generation = generation;
    detail.sample_rate = 48'000;
    detail.channels = 2;
    detail.event_sample = sample;
    detail.bin_frames = 48;
    // Windows start at the onset's 1 ms bin; the shape runs from 20 ms before to the body end.
    const auto start = sample / 48 * 48;
    detail.shape_start_sample = start - 20 * 48;
    detail.shape_end_sample = start + 130 * 48;
    detail.body_end_sample = start + 130 * 48;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.complete = 1;
    detail.transient_available = 1;
    detail.transient_db = 8.0f;
    detail.body_rms_dbfs = -22.0f;
    detail.attack_rms_dbfs = -14.0f;
    detail.sample_peak_dbfs = -6.0f;
    detail.crest_db = 8.0f;
    detail.sharpness_available = 1;
    detail.sharpness_acum = 1.4f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
        detail.shape[index] = index < 13 ? 0.03f
            : 0.82f * std::exp (-static_cast<float> (index - 13) / 8.0f) + 0.02f;
    return detail;
}

struct LaneFixture
{
    std::unique_ptr<KirinAttackEventBatch> events = std::make_unique<KirinAttackEventBatch>();
    std::unique_ptr<KirinAttackWaveformBatch> waveform = std::make_unique<KirinAttackWaveformBatch>();
    std::unique_ptr<KirinAttackDetailBatch> post = std::make_unique<KirinAttackDetailBatch>();
    std::unique_ptr<KirinAttackDetailBatch> pre = std::make_unique<KirinAttackDetailBatch>();
    std::unique_ptr<KirinAttackPairEventBatch> pairs = std::make_unique<KirinAttackPairEventBatch>();
    KirinAttackStats stats {};
    static constexpr std::int64_t latest = 288'000;

    bool submit (AttackComponent& component, std::int64_t at = latest) const
    {
        return component.setSnapshot (*events, *waveform, *post, *waveform, *pre, *pairs,
                                      at, 48'000, 7, stats);
    }
};

// Matched PRE/POST hits at one exact onset each, over a measured pulse envelope.
inline LaneFixture laneFixture (std::initializer_list<std::int64_t> samples)
{
    LaneFixture fixture;
    fixture.stats.available = fixture.stats.enabled = fixture.stats.worker_running = 1;
    fixture.pairs->status = KIRIN_SPECTRUM_ACTIVE;
    for (const auto sample : samples)
    {
        const auto item = fixture.events->count++;
        fixture.events->events[item].generation = 7;
        fixture.events->events[item].sample_rate = 48'000;
        fixture.events->events[item].event_sample = sample;
        fixture.post->details[fixture.post->count++] = laneDetail (sample);
        fixture.pre->details[fixture.pre->count++] = laneDetail (sample, 5);
        auto& pair = fixture.pairs->events[fixture.pairs->count++];
        pair.sample_rate = 48'000;
        pair.channels = 2;
        pair.pre_generation = 5;
        pair.post_generation = 7;
        pair.pre_available = pair.post_available = pair.delta_available = 1;
        pair.event_sample = pair.pre_event_sample = pair.post_event_sample = sample;
    }
    fixture.waveform->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (std::uint32_t index = 0; index < fixture.waveform->count; ++index)
    {
        auto& point = fixture.waveform->points[index];
        point.generation = 7;
        point.sample_rate = 48'000;
        point.channels = 2;
        point.start_sample = static_cast<std::int64_t> (index) * 480;
        point.end_sample = point.start_sample + 480;
        float pulse = 0.0f;
        for (const auto sample : samples)
            if (point.start_sample >= sample)
                pulse = std::max (pulse, std::exp (-static_cast<float> (point.start_sample - sample)
                                                   / 6'000.0f));
        point.rms_dbfs = -58.0f + 46.0f * pulse;
    }
    return fixture;
}

inline bool near (float value, float expected) { return std::abs (value - expected) < 1.0e-4f; }

inline bool verifyLaneModel()
{
    using namespace attack_lanes;
    auto fixture = laneFixture ({ 48'000, 96'000, 144'000, 192'000, 240'000 });
    auto& post = *fixture.post;
    auto& pre = *fixture.pre;
    auto& pairs = *fixture.pairs;
    post.details[0].transient_db = 11.0f;
    post.details[0].attack_rms_dbfs = -10.0f;
    post.details[0].crest_db = 5.0f;
    post.details[0].sharpness_acum = 1.7f;
    // Matched POST is measured at the PRE onset; windows elsewhere are never differenced.
    pairs.events[1].post_event_sample += 256;
    post.details[1].event_sample += 256;
    pre.details[2].body_rms_dbfs = -80.0f; // the PRE body is below the HISTORY floor
    pre.details[3].sharpness_available = 0;
    post.details[3].crest_db = std::numeric_limits<float>::quiet_NaN();
    post.details[3].transient_available = 0; // the next onset left no 20 ms body
    pairs.events[4].kind = 1; // PRE-only common event
    pairs.events[4].post_available = 0;
    auto model = std::make_unique<Model>();
    build (*model, pairs, post, pre, 7, 48'000);
    const auto& hits = model->hits;
    const auto reason = [&hits] (std::size_t hit, Lane lane) { return hits[hit].cells[index (lane)].reason; };
    const auto value = [&hits] (std::size_t hit, Lane lane) { return hits[hit].cells[index (lane)].value; };
    if (! model->delta || model->count != 5
        || ! near (value (0, Lane::transient), 3.0f) || ! near (value (0, Lane::strength), 4.0f)
        || ! near (value (0, Lane::crest), -3.0f) || ! near (value (0, Lane::sharpness), 0.3f))
        return false;
    for (const auto lane : lanes)
        if (reason (1, lane) != Reason::missing || reason (4, lane) != Reason::noMatch)
            return false;
    if (! hits[1].selectable || hits[4].selectable
        || attack_lane_painter::reasonText (hits[4], Reason::noMatch) != "PRE ONLY"
        || reason (2, Lane::transient) != Reason::quietBody
        || reason (2, Lane::strength) != Reason::value
        || reason (3, Lane::sharpness) != Reason::missing
        || reason (3, Lane::crest) != Reason::missing
        || reason (3, Lane::transient) != Reason::nextHit
        || attack_lane_painter::reasonText (hits[3], Reason::nextHit) != "NEXT HIT")
        return false;
    // The count is the hits on the six-second axis, the same columns the lanes draw.
    if (visibleCount (*model, 288'000, 48'000) != 5 || visibleCount (*model, 384'000, 48'000) != 4)
        return false;

    auto late = std::make_unique<KirinAttackDetailBatch> (post);
    late->details[0].generation = 6; // not delivered for this generation yet
    build (*model, pairs, *late, pre, 7, 48'000);
    if (hits[0].selectable || reason (0, Lane::strength) != Reason::missing)
        return false;

    pairs.status = KIRIN_SPECTRUM_NO_PAIR;
    post.details[2].body_rms_dbfs = -80.0f; // quiet room tone after the hit, not digital silence
    post.details[4].generation = 6; // stale POST detail never becomes a hit
    build (*model, pairs, post, pre, 7, 48'000);
    // Without a pair every lane is a POST value; per-hit Sharpness now follows the onset.
    return ! model->delta && model->count == 4
        && near (value (0, Lane::transient), 11.0f) && near (value (0, Lane::strength), -10.0f)
        && near (value (0, Lane::sharpness), 1.7f)
        && reason (2, Lane::transient) == Reason::quietBody
        && reason (3, Lane::transient) == Reason::nextHit
        && attack_lane_painter::reasonText (hits[2], Reason::quietBody) == "QUIET AFTER"
        && attack_lane_painter::valueText (Lane::strength, -10.0f, false, true) == "-10.0 dBFS"
        && attack_lane_painter::valueText (Lane::sharpness, -0.004f, true, true) == "+0.00 acum";
}

struct LaneScene
{
    std::unique_ptr<AttackComponent> component = std::make_unique<AttackComponent>();
    attack_ui::Layout layout;
};

inline LaneScene laneScene (int width, int height, presentation::Context context)
{
    LaneScene scene;
    scene.component->setPresentationContext (context);
    scene.component->setSize (width, height);
    scene.layout = attack_ui::layoutFor (width, height, context);
    return scene;
}

// The DRUM body of one editor preset: POST, Guide absent, TIME navigation removed.
inline LaneScene presetScene (const observatory::SizePreset& preset)
{
    const auto shell = observatory::shellLayout (observatory::Role::post, preset,
                                                 observatory::GuidePresence::absent);
    return laneScene (shell.body.width,
                      shell.body.height - observatory::timeNavigationHeight (preset.density),
                      presentation::forEditor (preset.width, preset.height));
}

inline bool verifyLaneRendering()
{
    auto scene = laneScene (872, 412, presentation::forEditor (900, 600));
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    auto zero = laneFixture ({ 96'000, 192'000, 240'000 });
    auto& post = *fixture.post;
    post.details[0].transient_db += 6.0f;  post.details[1].transient_db -= 6.0f;
    post.details[0].attack_rms_dbfs += 6.0f;  post.details[1].attack_rms_dbfs -= 6.0f;
    post.details[0].crest_db += 6.0f;  post.details[1].crest_db -= 6.0f;
    post.details[0].sharpness_acum += 0.5f;  post.details[1].sharpness_acum -= 0.5f;
    for (auto* batch : { &fixture, &zero })
    {
        batch->pairs->events[2].post_event_sample += 256;
        batch->post->details[2].event_sample += 256;
    }
    post.details[2].transient_db += 6.0f; // withheld: must draw no bar at all
    zero.submit (*scene.component);
    const auto reference = renderAttack (*scene.component);
    fixture.submit (*scene.component);
    const auto image = renderAttack (*scene.component);
    for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
    {
        const auto plot = laneRect (scene.layout, lane);
        const auto inner = plot.toFloat().reduced (static_cast<float> (attack_ui::laneInsetX),
                                                   static_cast<float> (attack_ui::laneInsetY));
        const auto zeroY = juce::roundToInt (inner.getCentreY());
        const auto column = [&] (std::int64_t sample, bool above) {
            const auto x = plot.getX() + attack_ui::eventX (sample, LaneFixture::latest, 48'000,
                                                            plot.getWidth());
            return above ? juce::Rectangle<int> (x - 3, plot.getY(), 7, zeroY - 2 - plot.getY())
                         : juce::Rectangle<int> (x - 3, zeroY + 3, 7, plot.getBottom() - zeroY - 3); };
        if (differences (image, reference, column (96'000, true)) == 0
            || differences (image, reference, column (96'000, false)) != 0
            || differences (image, reference, column (192'000, false)) == 0
            || differences (image, reference, column (192'000, true)) != 0
            || differences (image, reference, column (240'000, true)) != 0
            || differences (image, reference, column (240'000, false)) != 0)
        {
            std::cerr << "lane rendering: sign or withheld bar failed in lane " << lane << '\n';
            return false;
        }
    }
    return writePreviewTo ("KIRIN_ATTACK_UI_LANES_PREVIEW_PATH", image);
}

// Only the envelope reaches HISTORY; only event details reach the lanes.
inline bool verifyHistoryIsolation()
{
    auto scene = laneScene (872, 412, presentation::forEditor (900, 600));
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    fixture.submit (*scene.component);
    const auto base = renderAttack (*scene.component);
    for (std::uint32_t index = 0; index < fixture.post->count; ++index)
    {
        auto& detail = fixture.post->details[index];
        detail.transient_db += 4.0f; detail.attack_rms_dbfs -= 5.0f;
        detail.crest_db += 3.0f; detail.sharpness_acum += 0.4f;
        for (auto& point : detail.shape) point *= 0.5f;
    }
    fixture.submit (*scene.component);
    const auto detailsChanged = renderAttack (*scene.component);
    const auto history = historyRect (scene.layout);
    const auto lanes = lanesArea (scene.layout);
    if (differences (base, detailsChanged, history) != 0
        || differences (base, detailsChanged, lanes) < 40)
        return false;
    for (std::uint32_t index = 0; index < fixture.waveform->count; ++index)
        fixture.waveform->points[index].rms_dbfs -= 9.0f;
    fixture.submit (*scene.component);
    const auto envelopeChanged = renderAttack (*scene.component);
    return differences (detailsChanged, envelopeChanged, history) > 40
        && differences (detailsChanged, envelopeChanged, lanes) == 0;
}

// Without a pair, lanes are POST values, per-hit Sharpness included.
inline bool verifyPostOnlyLanes()
{
    auto scene = laneScene (580, 248, presentation::forEditor (600, 400));
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    fixture.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
    fixture.pairs->count = 0;
    fixture.submit (*scene.component);
    if (scene.component->pairedObservation())
        return false;
    const auto base = renderAttack (*scene.component);
    // An event whose detail has not arrived draws no lane column, so it is not counted either.
    auto& pending = fixture.events->events[fixture.events->count++];
    pending = fixture.events->events[0];
    pending.event_sample = 264'000;
    fixture.submit (*scene.component);
    if (differences (base, renderAttack (*scene.component)) != 0)
        return false;
    for (std::uint32_t index = 0; index < fixture.post->count; ++index)
        fixture.post->details[index].sharpness_acum += 1.5f;
    fixture.submit (*scene.component);
    const auto sharper = renderAttack (*scene.component);
    for (std::uint32_t index = 0; index < fixture.post->count; ++index)
        fixture.post->details[index].transient_db += 6.0f;
    fixture.submit (*scene.component);
    const auto stronger = renderAttack (*scene.component);
    return differences (base, sharper, laneRect (scene.layout, 3)) > 0
        && differences (base, sharper, laneRect (scene.layout, 0)) == 0
        && differences (sharper, stronger, laneRect (scene.layout, 0)) > 0
        && writePreviewTo ("KIRIN_ATTACK_UI_POST_ONLY_PREVIEW_PATH", stronger);
}

// Paired lanes and their readouts show POST - PRE only. Moving PRE and POST together leaves every
// lane and one-row readout unchanged at each editor preset and Capture layout; for SHARPNESS,
// which is a per-hit difference only, nothing on screen changes at all.
inline bool verifyLanesShowDifferencesOnly()
{
    std::vector<LaneScene> scenes;
    for (const auto& preset : observatory::sizePresets)
        scenes.push_back (presetScene (preset));
    for (const auto& [pixelWidth, pixelHeight] : { std::pair { 1200, 630 }, std::pair { 1080, 1080 } })
    {
        const auto width = juce::roundToInt (static_cast<float> (pixelWidth) / observatory::captureRenderScale);
        const auto height = juce::roundToInt (static_cast<float> (pixelHeight) / observatory::captureRenderScale);
        const auto body = observatory::shellLayout (observatory::Role::post,
            { width, height, observatory::densityForWidth (width), "CAPTURE" },
            observatory::GuidePresence::absent).body;
        scenes.push_back (laneScene (body.width, body.height, presentation::forOutput (
            width, height, presentation::OutputTarget::capture)));
    }
    for (auto& scene : scenes)
    {
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (*scene.component);
        const auto base = renderAttack (*scene.component);
        const auto both = [&fixture] (auto&& move) {
            for (auto* batch : { fixture.post.get(), fixture.pre.get() })
                for (std::uint32_t index = 0; index < batch->count; ++index)
                    move (batch->details[index]);
        };
        both ([] (KirinAttackDetail& detail) { detail.sharpness_acum += 0.5f; });
        fixture.submit (*scene.component);
        const auto sharper = renderAttack (*scene.component);
        both ([] (KirinAttackDetail& detail) {
            detail.transient_db += 3.0f; detail.attack_rms_dbfs += 3.0f; detail.crest_db += 3.0f; });
        fixture.submit (*scene.component);
        const auto louder = renderAttack (*scene.component);
        const auto values = scene.layout.arrangement == attack_ui::Arrangement::lanes
            ? lanesArea (scene.layout) : rectangle (scene.layout.line);
        if (differences (base, sharper) != 0 || differences (base, louder, values) != 0)
        {
            std::cerr << "lanes show PRE / POST operands at " << scene.component->getWidth()
                      << 'x' << scene.component->getHeight() << '\n';
            return false;
        }
    }
    return true;
}

// The loupe exists only in the 300% Inspection body and draws the measured hit shape.
inline bool verifyLoupe()
{
    for (const bool inspection : { true, false })
    {
        auto scene = inspection ? laneScene (872, 412, presentation::forEditor (900, 600))
                                : laneScene (580, 248, presentation::forEditor (600, 400));
        if (scene.layout.loupe != inspection)
            return false;
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (*scene.component);
        const auto base = renderAttack (*scene.component);
        for (std::uint32_t index = 0; index < fixture.post->count; ++index)
            for (auto& point : fixture.post->details[index].shape) point *= 0.25f;
        fixture.submit (*scene.component);
        const auto quieter = renderAttack (*scene.component);
        const auto changed = differences (base, quieter, historyReadoutRect (scene.layout));
        if (inspection ? changed < 40 : differences (base, quieter) != 0)
            return false;
        if (inspection && ! writePreviewTo ("KIRIN_ATTACK_UI_LOUPE_PREVIEW_PATH", base))
            return false;
    }
    return true;
}

// 100% and 125% keep HISTORY and state the selected hit's four values in one row.
inline bool verifyCompactLine()
{
    for (const auto preset : { observatory::sizePresets[0], observatory::sizePresets[1] })
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto layout = observatory::shellLayout (observatory::Role::post, preset,
                                                      observatory::GuidePresence::absent);
        auto scene = laneScene (layout.body.width,
                                layout.body.height - observatory::timeNavigationHeight (preset.density),
                                context);
        // 100% is the glance (one-row HISTORY over four large values); 125% the one-line readout.
        const bool glance = preset.density == observatory::Density::compact;
        if (scene.layout.arrangement != (glance ? attack_ui::Arrangement::glance : attack_ui::Arrangement::line)
            || scene.layout.history.empty())
            return false;
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (*scene.component);
        const auto base = renderAttack (*scene.component);
        fixture.post->details[2].transient_db += 5.0f;
        fixture.post->details[2].crest_db -= 2.0f;
        fixture.submit (*scene.component);
        const auto changed = renderAttack (*scene.component);
        if (differences (base, changed, rectangle (scene.layout.line)) < 10
            || differences (base, changed, historyRect (scene.layout)) != 0)
            return false;
        if (preset.density == observatory::Density::compact
            && ! writePreviewTo ("KIRIN_ATTACK_UI_COMPACT_PREVIEW_PATH", changed))
            return false;
    }
    return true;
}

// The selected hit is one thin hypha through HISTORY and every lane, never a highlight bar.
inline bool verifySelectionHypha()
{
    auto scene = laneScene (580, 248, presentation::forEditor (600, 400));
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    fixture.submit (*scene.component);
    scene.component->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    const auto image = renderAttack (*scene.component);
    const auto target = juce::Colour (attack_ui::selectionColour);
    const auto history = historyRect (scene.layout);
    const auto x = history.getX() + attack_ui::eventX (96'000, LaneFixture::latest, 48'000,
                                                       history.getWidth());
    // The hypha is drawn at 90% opacity; 40 still separates it from gold bars and white text.
    constexpr int tolerance = 40;
    const auto near = [&] (juce::Rectangle<int> row) {
        return countColour (image, row.withX (x - 3).withWidth (7), target, tolerance) > 0; };
    if (! near (history) || ! near (laneRect (scene.layout, 0)) || ! near (laneRect (scene.layout, 3)))
        return false;
    for (int y = history.getY(); y < scene.layout.lanes.back().bottom(); ++y)
        if (countColour (image, { history.getX(), y, history.getWidth(), 1 }, target, tolerance) > 6)
            return false;
    return writePreviewTo ("KIRIN_ATTACK_UI_SELECTION_PREVIEW_PATH", image);
}
}
