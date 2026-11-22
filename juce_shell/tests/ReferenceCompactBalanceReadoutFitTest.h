#pragma once
#include "ReferenceOverlayTest.h"
#include "../src/HyphaReferenceBlauertZones.h"
#include "../src/HyphaReferenceChartReading.h"
#include <set>

namespace hypha::tests::reference_overlay
{
inline void verifyCompactBalanceReadoutFits()
{
    auto context = presentation::forEditor (300, 200); context.referenceCompact = true;
    auto state = page (2, 0);
    state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
    auto a = std::make_shared<ref::KirinSpectrumWindow> (*state.aKirin);
    std::fill (a->medianDb.begin(), a->medianDb.end(), -40.0f); state.aKirin = a;
    for (size_t index = 0; index < state.songFacts.size(); ++index)
    {
        auto& fact = state.songFacts[index];
        fact.lufsI = -20.0; fact.loudnessOffsetLu = 0.0;
        std::fill (fact.medianDb.begin(), fact.medianDb.end(), index == 0 ? -50.0f : -30.0f);
    }
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const auto blauertWords = text_style::shownText (reference_ui::blauertReadout ('B', { 0.0, 0.0 }));
        const auto aText = text_style::shownText ("A -40.0 dB");
        const auto bText = text_style::shownText ("B -50.0 dB");
        const auto abText = text_style::shownText ("A -40.0 / B -50.0 dB");
        const auto targetText = text_style::shownText ("TARGET -48.0 TO -32.0 dB");
        const auto frequency = text_style::shownText ("1250 Hz");
        const auto draw = [&] (int width, float height, bool audit)
        {
            const juce::Rectangle<float> chart (10.0f, 30.0f, static_cast<float> (width), height);
            state.chartPointer = { chart.getX() + chart.getWidth() * static_cast<float> (std::log (62.5) / std::log (1000.0)),
                                   chart.getCentreY() };
            juce::Image image (juce::Image::ARGB, 300, 120, true);
            juce::Graphics g (image);
            const auto clip = g.getClipBounds().getIntersection (
                reference_ui::chartReadingClip (chart, height < 40.0f ? 14.0f : 0.0f)).toFloat();
            const text_style::ShownTextLog log;
            reference_ui::paintCompactBalanceChart (g, chart, state, context);
            const auto& texts = log.texts();
            const auto& areas = log.areas();
            std::set<float> rows;
            for (int index = 0; index < texts.size(); ++index)
            {
                const auto& t = texts[index];
                if (t != aText && t != bText && t != abText && t != targetText && t != frequency) continue;
                const auto& area = areas[static_cast<size_t> (index)];
                rows.insert (area.getY());
                if (audit) require (!area.isEmpty() && clip.contains (area),
                                    "every BALANCE readout label fits fully inside the production clip");
            }
            if (!audit)
                return rows.size() == 3 && texts.contains (aText) && texts.contains (bText)
                    && texts.contains (targetText) && texts.contains (frequency);
            const auto target = texts.indexOf (targetText);
            require (target >= 0 && chart.contains (areas[static_cast<size_t> (target)]),
                     "complete TARGET endpoints remain on the plot, not clipped or abbreviated");
            if (height == 14.0f)
                require (rows.size() == 2 && texts.contains (abText) && !texts.contains (aText) && !texts.contains (bText),
                         "only the minimum-height case combines complete A and B values in two rows");
            else
                require (rows.size() == 3 && texts.contains (aText) && texts.contains (bText)
                             && texts.contains (frequency) && !texts.contains (abText),
                         "the 24px and 40px controls keep all four separate readings in three rows");
            const auto blauert = texts.indexOf (blauertWords);
            require (height < 40.0f ? blauert < 0 : blauert >= 0 && areas[static_cast<size_t> (blauert)].getBottom() <= chart.getY(),
                     "Blauert keeps the separate 40px caption instead of covering a reading");
            return true;
        };
        bool found = false;
        for (int width = 160; width <= 225; ++width)
        {
            if (!draw (width, 24.0f, false)) continue;
            for (const auto height : { 14.0f, 24.0f, 40.0f }) draw (width, height, true);
            found = true; break;
        }
        require (found, "same narrow width exercises the real two/three-row renderer at all three heights");
    }
}
}
