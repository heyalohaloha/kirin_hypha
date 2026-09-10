#pragma once
#include "AttackUiOverviewContract.h"
#include "../src/HyphaObservatoryContract.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>

namespace hypha::attack_ui_test
{
// Explicit diagnostic, not a pass/fail performance gate. The existing lifecycle/render
// assertions still run. No host, audio output, source file, or product state is modified.
inline void profileDenseAttackIfRequested()
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_PAINT_PROFILE", {}).isEmpty())
        return;
    const bool layers = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_ATTACK_PAINT_PROFILE_LAYERS", {}).isNotEmpty();
    const auto dpi = juce::jlimit (1.0f, 4.0f, juce::SystemStats::getEnvironmentVariable (
        "KIRIN_ATTACK_PAINT_PROFILE_DPI", "1").getFloatValue());
    std::cout << std::unitbuf;
    constexpr std::int64_t latest = 288'000;
    constexpr std::uint32_t rate = 48'000;
    const auto layout = observatory::shellLayout (observatory::Role::post,
        observatory::sizePresets.back(), observatory::GuidePresence::absent);
    for (std::uint32_t count : { 31u, KIRIN_ATTACK_DETAIL_BATCH_CAPACITY })
    {
        if (layers && count != 31u) continue;
        auto component = std::make_unique<AttackComponent>();
        auto events = std::make_unique<KirinAttackEventBatch>();
        auto details = std::make_unique<KirinAttackDetailBatch>();
        auto pairs = std::make_unique<KirinAttackPairEventBatch>();
        auto waveform = std::make_unique<KirinAttackWaveformBatch>();
        events->count = details->count = pairs->count = count;
        events->capacity = KIRIN_ATTACK_EVENT_BATCH_CAPACITY;
        details->capacity = KIRIN_ATTACK_DETAIL_BATCH_CAPACITY;
        pairs->capacity = KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY;
        pairs->status = KIRIN_SPECTRUM_ACTIVE;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto at = 1'600 + static_cast<std::int64_t> (i) * (latest - 3'200) / count;
            auto& detail = details->details[i];
            detail = overviewDetail(); detail.generation = 7;
            detail.event_sample = at;
            detail.shape_start_sample = at - 4'800; detail.shape_end_sample = at + 1'440;
            auto& event = events->events[i];
            event.generation = 7; event.sample_rate = rate; event.channels = 2;
            event.event_sample = at;
            auto& pair = pairs->events[i];
            pair.pair_generation = pair.pre_generation = pair.post_generation = 7;
            pair.sample_rate = rate; pair.channels = 2;
            pair.pre_available = pair.post_available = pair.delta_available = 1;
            pair.event_sample = pair.pre_event_sample = pair.post_event_sample = at;
        }
        waveform->capacity = waveform->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
        for (std::uint32_t i = 0; i < waveform->count; ++i)
        {
            auto& point = waveform->points[i];
            point.generation = 7; point.sample_rate = rate; point.channels = 2;
            point.start_sample = static_cast<std::int64_t> (i) * 480;
            point.end_sample = point.start_sample + 480;
            point.rms_dbfs = -20.0f + 9.0f * std::sin (static_cast<float> (i) * 0.08f);
            point.peak_linear = 0.5f;
        }
        KirinAttackStats stats {};
        stats.available = stats.enabled = stats.worker_running = 1;
        component->setPresentationContext (presentation::forEditor (
            observatory::sizePresets.back().width,
            observatory::sizePresets.back().height));
        component->setSize (layout.body.width, layout.body.height
            - observatory::timeNavigationHeight (observatory::sizePresets.back().density));
        component->setSnapshot (*events, *waveform, *details, *waveform, *details,
                                *pairs, latest, rate, 7, stats);
        component->presentationTick (true);
        juce::Image image (juce::Image::ARGB, static_cast<int> (std::ceil (component->getWidth() * dpi)),
                            static_cast<int> (std::ceil (component->getHeight() * dpi)), true);
        // Separate the expensive draw responsibilities without modifying production rendering.
        // The full fixture is synthetic, not the recorded audio or the user's event magnitudes.
        auto emptyEvents = std::make_unique<KirinAttackEventBatch>();
        auto emptyDetails = std::make_unique<KirinAttackDetailBatch>();
        auto emptyWaveform = std::make_unique<KirinAttackWaveformBatch>();
        auto emptyPairs = std::make_unique<KirinAttackPairEventBatch>();
        emptyPairs->status = KIRIN_SPECTRUM_ACTIVE;
        for (int layer = 0; layer < (layers ? 4 : 1); ++layer)
        for (bool overlay : { false, true })
        {
            const bool flowOnly = layer == 1, eventsOnly = layer == 2;
            component->setSnapshot (flowOnly ? *emptyEvents : *events,
                eventsOnly ? *emptyWaveform : *waveform, flowOnly ? *emptyDetails : *details,
                eventsOnly ? *emptyWaveform : *waveform, flowOnly ? *emptyDetails : *details,
                flowOnly ? *emptyPairs : *pairs, latest, rate, 7, stats);
            component->presentationTick (layer != 3);
            component->setOverlayMode (overlay);
            const auto draw = [&] {
                juce::Graphics graphics (image);
                graphics.addTransform (juce::AffineTransform::scale (dpi));
                component->paintEntireComponent (graphics, true);
            };
            const auto cold = juce::Time::getMillisecondCounterHiRes();
            draw();
            const auto firstMs = juce::Time::getMillisecondCounterHiRes() - cold;
            std::array<double, 3> samples {};
            const int repeats = layers ? 3 : 10;
            for (auto& sample : samples)
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                for (int repeat = 0; repeat < repeats; ++repeat) draw();
                sample = (juce::Time::getMillisecondCounterHiRes() - start) / repeats;
            }
            std::sort (samples.begin(), samples.end());
            constexpr const char* layerNames[] { "full", "flow-only", "events-only", "inactive" };
            std::cout << "DRUM paint diagnostic: events=" << count
                      << " overlay=" << overlay << " size=" << component->getWidth()
                      << "x" << component->getHeight() << " first_ms=" << firstMs
                      << " dpi=" << dpi
                      << " warm_median_ms=" << samples[1] << " layer=" << layerNames[layer]
                      << " repeats=" << repeats << '\n';
        }
    }
}
}
