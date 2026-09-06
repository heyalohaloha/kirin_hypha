#pragma once
#include "../src/HyphaObservatoryContract.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>

namespace hypha::attack_ui_test
{
inline bool verifyAttackFrameBudget()
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_FRAME_BUDGET", {}).isEmpty())
        return true;
    std::cout << std::unitbuf;
    bool withinBudget = true;
    auto events = std::make_unique<KirinAttackEventBatch>();
    auto details = std::make_unique<KirinAttackDetailBatch>();
    auto preDetails = std::make_unique<KirinAttackDetailBatch>();
    auto pairs = std::make_unique<KirinAttackPairEventBatch>();
    auto waveform = std::make_unique<KirinAttackWaveformBatch>();
    auto preWaveform = std::make_unique<KirinAttackWaveformBatch>();
    KirinAttackStats stats {}; stats.available = stats.enabled = stats.worker_running = 1;
    for (auto count : { 31u, KIRIN_ATTACK_DETAIL_BATCH_CAPACITY })
    {
        events->count = details->count = pairs->count = count;
        pairs->status = KIRIN_SPECTRUM_ACTIVE;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto at = 1600 + static_cast<std::int64_t> (i) * 284800 / count;
            auto& d = details->details[i]; d = overviewDetail(); d.generation = 7;
            d.event_sample = at; d.shape_start_sample = at - 4800; d.shape_end_sample = at + 1440;
            d.attack_rms_dbfs = -32 + static_cast<float> (i) * 24 / static_cast<float> (count);
            d.sharpness_acum = .7f + static_cast<float> (i) * 1.5f / static_cast<float> (count);
            auto& e = events->events[i]; e.generation = 7; e.sample_rate = 48000; e.event_sample = at;
            auto& p = pairs->events[i]; p.pre_generation = p.post_generation = 7;
            p.pre_available = p.post_available = 1; p.sample_rate = 48000;
            p.event_sample = p.pre_event_sample = p.post_event_sample = at;
        }
        *preDetails = *details;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            auto& pre = preDetails->details[i];
            pre.attack_rms_dbfs -= 5.3f; pre.sharpness_acum *= .77f;
            pre.contrast_db *= .65f; pre.sample_edge_ratio_db -= 3.7f;
        }
        waveform->count = preWaveform->count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
        for (int instances : { 1, 2 })
        for (auto dpi : { 1.0f, 1.25f, 2.0f })
        for (bool overlay : { false, true })
        for (const auto& preset : observatory::sizePresets)
        {
            const auto layout = observatory::shellLayout (observatory::Role::post,
                preset, observatory::GuidePresence::absent);
            const auto width = layout.body.width;
            const auto height = layout.body.height - observatory::timeNavigationHeight (preset.density);
            auto component = std::make_unique<AttackComponent>(); component->setSize (width, height);
            component->setOverlayMode (overlay);
            auto second = std::make_unique<AttackComponent>(); second->setSize (width, height);
            second->setOverlayMode (overlay);
            juce::Image image (juce::Image::ARGB, static_cast<int> (std::ceil (width * dpi)),
                                static_cast<int> (std::ceil (height * dpi)), true);
            const auto frame = [&] (int frameIndex) {
                const auto offset = frameIndex * 480;
                for (std::uint32_t i = 0; i < waveform->count; ++i)
                {
                    auto& p = waveform->points[i]; p.generation = 7; p.sample_rate = 48000; p.channels = 2;
                    p.start_sample = static_cast<std::int64_t> (i) * 480 + offset; p.end_sample = p.start_sample + 480;
                    p.rms_dbfs = -20 + 9 * std::sin (static_cast<float> (i) * .08f + frameIndex * .04f);
                    preWaveform->points[i] = p;
                    preWaveform->points[i].rms_dbfs -= 5.3f;
                }
                // Correct an interior observation too; endpoint-only invalidation would miss this.
                details->details[count / 2].sharpness_acum = 1.1f + frameIndex * .013f;
                const auto now = juce::Time::getMillisecondCounterHiRes();
                component->setSnapshot (*events, *waveform, *details, *preWaveform, *preDetails,
                                        *pairs, 288000 + offset, 48000, 7, stats);
                component->presentationTickAt (now + 101);
                juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (dpi));
                component->paintEntireComponent (g, true);
                if (instances == 2) {
                    second->setSnapshot (*events, *waveform, *preDetails, *preWaveform, *details,
                                         *pairs, 288000 + offset, 48000, 7, stats);
                    second->presentationTickAt (now + 101);
                    second->paintEntireComponent (g, true);
                }
            };
            const auto coldStart = juce::Time::getMillisecondCounterHiRes(); frame (0);
            const auto cold = juce::Time::getMillisecondCounterHiRes() - coldStart;
            std::array<double, 5> samples {};
            for (int i = 0; i < 5; ++i)
            {
                const auto start = juce::Time::getMillisecondCounterHiRes(); frame (i + 1);
                samples[static_cast<std::size_t> (i)] = juce::Time::getMillisecondCounterHiRes() - start;
            }
            std::sort (samples.begin(), samples.end());
            std::cout << "DRUM changing frame: size=" << width << 'x' << height << " dpi=" << dpi
                      << " instances=" << instances << " events=" << count << " overlay=" << overlay << " cold_ms=" << cold
                      << " median_ms=" << samples[2] << " max_ms=" << samples[4] << '\n';
#if ! JUCE_DEBUG
            withinBudget = withinBudget && samples[2] <= (instances == 1 ? 12.0 : 16.0) && samples[4] <= 24.0 && cold <= 80.0;
#endif
        }
    }
#if JUCE_DEBUG
    std::cout << "DRUM frame budget: reported only (Debug)\n";
#else
    std::cout << "DRUM frame budget: " << (withinBudget ? "PASS" : "FAIL") << '\n';
#endif
    return withinBudget;
}
}
