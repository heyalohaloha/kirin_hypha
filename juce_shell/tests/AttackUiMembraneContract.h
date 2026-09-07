#pragma once
#include "../src/HyphaAttackMembraneGeometry.h"
#include <limits>
#include <functional>

namespace hypha::attack_ui_test
{
inline bool verifyMembraneContract()
{
    const juce::Rectangle<float> area { 0, 0, 400, 120 };
    const auto base = attack_membrane::geometry (area, { .5f, .5f, .5f, .5f });
    for (int mask = 0; mask < 16; ++mask)
    {
        const auto shape = attack_membrane::geometry (area,
            { float (mask&1), float ((mask>>1)&1), float ((mask>>2)&1), float ((mask>>3)&1) });
        for (const auto& sheet : shape.sheets)
            for (const auto& path : sheet.fills)
                if (path.isEmpty() || ! area.contains (path.getBounds())) return false;
        if (shape.front.isEmpty() || ! area.contains (shape.front.getBounds())) return false;
    }
    for (int i = 0; i <= 20; ++i)
    {
        const auto v = static_cast<float> (i) / 20;
        const auto s = attack_membrane::geometry (area, {v,.5f,.5f,.5f});
        const auto b = attack_membrane::geometry (area, {.5f,v,.5f,.5f});
        const auto t = attack_membrane::geometry (area, {.5f,.5f,v,.5f});
        const auto x = attack_membrane::geometry (area, {.5f,.5f,.5f,v});
        if (! std::equal_to<float> {} (s.opening, base.opening) || ! std::equal_to<float> {} (s.wrinkle, base.wrinkle) || ! std::equal_to<float> {} (s.frontX, base.frontX)
            || ! std::equal_to<float> {} (b.coreThickness, base.coreThickness) || ! std::equal_to<float> {} (b.wrinkle, base.wrinkle) || ! std::equal_to<float> {} (b.frontX, base.frontX)
            || ! std::equal_to<float> {} (t.coreThickness, base.coreThickness) || ! std::equal_to<float> {} (t.opening, base.opening) || ! std::equal_to<float> {} (t.wrinkle, base.wrinkle)
            || ! std::equal_to<float> {} (x.coreThickness, base.coreThickness) || ! std::equal_to<float> {} (x.opening, base.opening) || ! std::equal_to<float> {} (x.frontX, base.frontX)) return false;
        if (v != .5f && (s.sheets[1].fills[0] == base.sheets[1].fills[0]
            || b.sheets[0].fills[0] == base.sheets[0].fills[0]
            || t.front == base.front || x.sheets[3].fills[0] == base.sheets[3].fills[0])) return false;
    }
    KirinAttackWaveformBatch waveform {};
    waveform.count = 20;
    for (std::uint32_t i = 0; i < waveform.count; ++i)
    {
        auto& p = waveform.points[i];
        p.start_sample = i * 480; p.end_sample = (i + 1) * 480;
        p.generation = 7; p.sample_rate = 48'000; p.channels = 2;
        p.rms_dbfs = i < 14 ? -55.0f : -12.0f;
    }
    const auto motion = attack_motion::measuredMotion (waveform, 9'600, 48'000, 7);
    if (motion.bend == attack_motion::Motion {}.bend
        || attack_motion::measuredMotion (waveform, 9'600, 48'000, 8).bend != attack_motion::Motion {}.bend
        || attack_motion::measuredMotion (waveform, 9'600, 0, 7).bend != attack_motion::Motion {}.bend
        || attack_motion::measuredMotion (waveform, 90'000, 48'000, 7).bend != attack_motion::Motion {}.bend)
        return false;

    // Crossing a source bin interpolates the presentation, never a fabricated beat.
    const auto before = attack_motion::measuredMotion (waveform, 14 * 480, 48'000, 7);
    const auto after = attack_motion::measuredMotion (waveform, 14 * 480 + 1, 48'000, 7);
    for (std::size_t i = 0; i < before.bend.size(); ++i)
        if (std::abs (before.bend[i] - after.bend[i]) > .001f) return false;
    for (std::uint32_t i = 0; i < waveform.count; ++i)
        waveform.points[i].rms_dbfs = -60.0f + static_cast<float> (i) * 2;
    if (attack_motion::measuredMotion (waveform, 9'600, 48'000, 7).bend[0] <= 0) return false;
    auto shifted = waveform;
    constexpr auto origin = INT64_C(9007199254740993);
    for (auto& point : shifted.points) { point.start_sample += origin; point.end_sample += origin; }
    if (attack_motion::measuredMotion (waveform, 9'477, 48'000, 7).bend
        != attack_motion::measuredMotion (shifted, origin + 9'477, 48'000, 7).bend) return false;
    waveform.points[18].start_sample += 120; // Gap between two valid taps must break the derivative.
    if (std::abs (attack_motion::measuredMotion (waveform, 9'600, 48'000, 7).bend[0]) > .00001f)
        return false;
    waveform.points[18].start_sample -= 120;
    waveform.points[18].channels = 1;
    if (std::abs (attack_motion::measuredMotion (waveform, 9'600, 48'000, 7).bend[0]) > .00001f)
        return false;
    if (attack_motion::measuredMotion (waveform, 9'600, 44'100, 7).bend != attack_motion::Motion {}.bend
        || attack_motion::measuredMotion (waveform, std::numeric_limits<std::int64_t>::min(),
                                      48'000, 7).bend != attack_motion::Motion {}.bend) return false;
    waveform.points[18].channels = 2;
    for (auto& p : waveform.points) p.rms_dbfs = -120.0f;
    if (attack_motion::measuredMotion (waveform, 9'600, 48'000, 7).bend != attack_motion::Motion {}.bend)
        return false;

    const auto moving = attack_membrane::geometry (area, {.5f,.5f,.5f,.5f}, motion);
    if (moving.sheets[1].fills[0] == base.sheets[1].fills[0]
        || ! std::equal_to<float> {} (moving.coreThickness, base.coreThickness) || ! std::equal_to<float> {} (moving.frontX, base.frontX)) return false;
    for (auto dpi : {1.0f,1.25f,2.0f,4.0f}) for (int pattern = 0; pattern < 16; ++pattern)
    {
        attack_motion::Motion a, b;
        for (std::size_t tap=0; tap<a.bend.size(); ++tap) {
            a.bend[tap]=.2f*std::sin (static_cast<float> (tap+static_cast<std::size_t> (pattern)));
            b.bend[tap]=a.bend[tap]+(tap%2==0?1:-1)*.5f/(120*dpi); }
        const auto ga=attack_membrane::geometry (area, {.8f,.9f,.7f,1}, a);
        const auto gb=attack_membrane::geometry (area, {.8f,.9f,.7f,1}, b);
        for (std::size_t layer=0; layer<4; ++layer)
        {
            if (ga.sheets[layer].lightStart.getDistanceFrom (gb.sheets[layer].lightStart)*dpi > .0501f
                || ga.sheets[layer].lightEnd.getDistanceFrom (gb.sheets[layer].lightEnd)*dpi > .0501f) return false;
            for (std::size_t path=0; path<5; ++path) {
                juce::Path::Iterator ia (ga.sheets[layer].fills[path]), ib (gb.sheets[layer].fills[path]);
                while (ia.next()) {
                    if (! ib.next() || ia.elementType != ib.elementType) return false;
                    if (std::max ({std::abs (ia.y1-ib.y1),std::abs (ia.y2-ib.y2),std::abs (ia.y3-ib.y3)})*dpi > .0501f)
                        return false;
                }
                if (ib.next()) return false;
            }
        }
    }
    const auto invalid=std::numeric_limits<float>::quiet_NaN();
    const auto safe=attack_membrane::geometry (area,{invalid,invalid,invalid,invalid});
    const auto zero=attack_membrane::geometry (area,{});
    if (safe.sheets[0].fills[0] != zero.sheets[0].fills[0]) return false;
    // A missing part of the compound texture observation cannot become a visible valid texture.
    for (int field=0; field<3; ++field) {
        auto detail=comparisonDetail();setComparisonFeature (detail,ComparisonFeature::texture,.8f);
        if (field==0) detail.sample_edge_ratio_db=invalid;
        if (field==1) detail.crest_db=invalid;
        if (field==2) detail.peak_plateau_ms=invalid;
        juce::Image image (juce::Image::ARGB,400,120,true);juce::Graphics g (image);
        attack_painter::drawEventFocus (g,nullptr,&detail,image.getBounds());
        if (specimenLight (image)!=0) return false;
    }
    for (const auto size : {juce::Point<int>{180,65},{400,120},{600,180}})
    {
        juce::Image image (juce::Image::ARGB,size.x,size.y,true);
        { juce::Graphics g (image); attack_specimen::drawMembrane (g,image.getBounds(),{}); }
        if (specimenLight (image) != 0) return false;
        { juce::Graphics g (image); attack_specimen::drawMembrane (g,image.getBounds(),{.7f,.7f,.7f,.7f}); }
        if (specimenLight (image)==0) return false;
    }
    juce::Image faint (juce::Image::ARGB,400,120,true), full (juce::Image::ARGB,400,120,true);
    {juce::Graphics g (faint);attack_specimen::drawMembrane (g,faint.getBounds(),{.001f,0,0,0});}
    {juce::Graphics g (full);attack_specimen::drawMembrane (g,full.getBounds(),{1,0,0,0});}
    return specimenLight (faint) <= specimenLight (full)/100;
}
}
