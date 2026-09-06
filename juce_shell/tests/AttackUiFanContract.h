#pragma once
#include "../src/HyphaAttackFanGeometry.h"
#include <limits>
#include <iostream>

namespace hypha::attack_ui_test
{
inline bool verifyFanContract()
{
    using attack_specimen::FeatureAmounts;
    const juce::Rectangle<float> area { 0, 0, 400, 120 };
    const auto base = attack_fan::geometry (area, { 0.5f, 0.5f, 0.5f, 0.5f });
    for (int i = 0; i <= 20; ++i)
    {
        const auto v = static_cast<float> (i) / 20.0f;
        const auto s = attack_fan::geometry (area, { v, 0.5f, 0.5f, 0.5f });
        const auto b = attack_fan::geometry (area, { 0.5f, v, 0.5f, 0.5f });
        const auto t = attack_fan::geometry (area, { 0.5f, 0.5f, v, 0.5f });
        const auto x = attack_fan::geometry (area, { 0.5f, 0.5f, 0.5f, v });
        if (s.ribs != base.ribs || s.front != base.front || s.branches != base.branches
            || b.root != base.root || b.front != base.front
            || t.root != base.root || t.ribs != base.ribs || t.branches != base.branches
            || x.root != base.root || x.ribs != base.ribs || x.front != base.front
            || x.offshoots > 28 || t.frontX < area.getWidth() * 0.75f) return false;
        for (const auto* shape : { &s, &b, &t, &x })
            for (const auto* path : { &shape->root, &shape->ribs, &shape->front, &shape->branches })
                if (! path->isEmpty() && ! area.contains (path->getBounds())) return false;
    }
    const auto lo = attack_fan::geometry (area, {});
    const auto hi = attack_fan::geometry (area, { 1, 1, 1, 1 });
    if (hi.rootHalfHeight <= lo.rootHalfHeight || hi.fanHalfHeight <= lo.fanHalfHeight
        || hi.frontX <= lo.frontX || hi.offshoots != 28 || lo.offshoots != 0) return false;
    auto invalid = std::numeric_limits<float>::quiet_NaN();
    const auto safe = attack_fan::geometry (area, { invalid, invalid, invalid, invalid });
    if (safe.root != lo.root || safe.front != lo.front || safe.ribs != lo.ribs) return false;
    KirinAttackWaveformBatch waveform {};
    waveform.count = 20;
    for (std::uint32_t i = 0; i < waveform.count; ++i)
    {
        auto& p = waveform.points[i];
        p.start_sample = i * 480; p.end_sample = (i + 1) * 480;
        p.generation = 7; p.sample_rate = 48'000; p.channels = 2;
        p.rms_dbfs = i < 14 ? -55.0f : -12.0f;
    }
    const auto motion = attack_fan::measuredMotion (waveform, 9'600, 48'000, 7);
    if (motion.bend == attack_fan::Motion {}.bend
        || attack_fan::measuredMotion (waveform, 9'600, 48'000, 8).bend != attack_fan::Motion {}.bend
        || attack_fan::measuredMotion (waveform, 9'600, 0, 7).bend != attack_fan::Motion {}.bend
        || attack_fan::measuredMotion (waveform, 90'000, 48'000, 7).bend != attack_fan::Motion {}.bend)
        return false;
    const auto moving = attack_fan::geometry (area, { .5f, .5f, .5f, .5f }, motion);
    if (moving.ribs == base.ribs || moving.root != base.root || moving.front != base.front) return false;
    // Crossing a source bin interpolates the presentation, never a fabricated beat.
    const auto before = attack_fan::measuredMotion (waveform, 14 * 480, 48'000, 7);
    const auto after = attack_fan::measuredMotion (waveform, 14 * 480 + 1, 48'000, 7);
    for (std::size_t i = 0; i < before.bend.size(); ++i)
        if (std::abs (before.bend[i] - after.bend[i]) > .001f) return false;
    for (std::uint32_t i = 0; i < waveform.count; ++i)
        waveform.points[i].rms_dbfs = -60.0f + static_cast<float> (i) * 2;
    if (attack_fan::measuredMotion (waveform, 9'600, 48'000, 7).bend[0] <= 0) return false;
    waveform.points[18].start_sample += 120; // Gap between two valid taps must break the derivative.
    if (std::abs (attack_fan::measuredMotion (waveform, 9'600, 48'000, 7).bend[0]) > .00001f)
        return false;
    waveform.points[18].start_sample -= 120;
    waveform.points[18].channels = 1;
    if (std::abs (attack_fan::measuredMotion (waveform, 9'600, 48'000, 7).bend[0]) > .00001f)
        return false;
    if (attack_fan::measuredMotion (waveform, 9'600, 44'100, 7).bend != attack_fan::Motion {}.bend
        || attack_fan::measuredMotion (waveform, std::numeric_limits<std::int64_t>::min(),
                                      48'000, 7).bend != attack_fan::Motion {}.bend) return false;
    waveform.points[18].channels = 2;
    for (auto& p : waveform.points) p.rms_dbfs = -120.0f;
    if (attack_fan::measuredMotion (waveform, 9'600, 48'000, 7).bend != attack_fan::Motion {}.bend)
        return false;
    for (const auto size : { juce::Point<int> { 180, 65 }, { 400, 120 }, { 600, 180 } })
    {
        juce::Image image (juce::Image::ARGB, size.x, size.y, true);
        { juce::Graphics g (image); attack_specimen::drawFan (g, image.getBounds(), {}); }
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
                if (image.getPixelAt (x, y).getAlpha() != 0) return false;
        { juce::Graphics g (image); attack_specimen::drawFan (g, image.getBounds(), { .7f, .7f, .7f, .7f }); }
        if (specimenLight (image) == 0) return false;
    }
    juce::Image faint (juce::Image::ARGB, 400, 120, true), full (juce::Image::ARGB, 400, 120, true);
    { juce::Graphics g (faint); attack_specimen::drawFan (g, faint.getBounds(), { .001f, 0, 0, 0 }); }
    { juce::Graphics g (full); attack_specimen::drawFan (g, full.getBounds(), { 1, 0, 0, 0 }); }
    if (specimenLight (faint) > specimenLight (full) / 100) return false;
    auto identity = comparisonDetail();
    for (auto feature : { ComparisonFeature::strength, ComparisonFeature::brightness,
                          ComparisonFeature::texture, ComparisonFeature::transient })
        setComparisonFeature (identity, feature, .5f);
    const auto pairedImage = renderComparison (identity, identity, motion);
    juce::Image expected (juce::Image::ARGB, 300, 100, true);
    { juce::Graphics g (expected); g.fillAll (juce::Colours::black);
      attack_specimen::drawFan (g, expected.getBounds(), { .5f, .5f, .5f, .5f }, motion, true);
      attack_specimen::drawFan (g, expected.getBounds(), { .5f, .5f, .5f, .5f }, motion); }
    if (specimenDifferences (pairedImage, expected) > 10) return false;
    // Convex Bezier hulls bound every curve point by the control-point displacement.
    // Exercise both signs of every tap at the cache's 0.05 physical-pixel reuse boundary.
    for (const auto dpi : { 1.0f, 1.25f, 2.0f, 4.0f })
    for (int pattern = 0; pattern < 16; ++pattern)
    {
        attack_fan::Motion a, b;
        for (std::size_t tap = 0; tap < a.bend.size(); ++tap)
        {
            const auto phase = static_cast<std::size_t> (pattern) + tap;
            a.bend[tap] = .2f * std::sin (static_cast<float> (phase));
            b.bend[tap] = a.bend[tap] + (phase % 2 == 0 ? 1 : -1) * .1f / (120 * dpi);
        }
        const auto ga = attack_fan::geometry (area, { .8f, .9f, .7f, 1 }, a);
        const auto gb = attack_fan::geometry (area, { .8f, .9f, .7f, 1 }, b);
        for (const auto paths : { std::pair { &ga.ribs, &gb.ribs }, { &ga.branches, &gb.branches } })
        {
            juce::Path::Iterator ia (*paths.first), ib (*paths.second);
            while (ia.next())
            {
                if (! ib.next() || ia.elementType != ib.elementType) return false;
                const auto delta = std::max ({ std::abs (ia.y1 - ib.y1), std::abs (ia.y2 - ib.y2),
                                              std::abs (ia.y3 - ib.y3) });
                if (delta * dpi > .0501f) return false;
            }
            if (ib.next()) return false;
        }
        if (ga.root != gb.root || ga.front != gb.front) return false;
    }
    return true;
}
}
