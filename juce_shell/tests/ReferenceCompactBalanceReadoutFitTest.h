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
        const auto abSharedUnitText = text_style::shownText ("A -40.0 / B -50.0");
        const auto targetText = text_style::shownText ("TARGET -48.0 TO -32.0 dB");
        const auto font = labelFont (context, typography::TextRole::unit, typography::Composition::visualization);
        const auto blauertFont = requiresJapaneseGlyphs (blauertWords) ? nativeTextFontLike (font) : font;
        const auto draw = [&] (int width, float height, float hz, const char* frequencyText, bool audit)
        {
            const auto frequency = text_style::shownText (frequencyText);
            const juce::Rectangle<float> chart (10.0f, 30.0f, static_cast<float> (width), height);
            state.chartPointer = { chart.getX() + chart.getWidth() * static_cast<float> (std::log (hz / 20.0f) / std::log (1000.0)),
                                   chart.getCentreY() };
            juce::Image image (juce::Image::ARGB, 300, 120, true);
            juce::Graphics g (image);
            const auto clip = g.getClipBounds().getIntersection (
                reference_ui::chartReadingClip (chart, height < 40.0f ? 14.0f : 0.0f)).toFloat();
            const text_style::ShownTextLog log;
            reference_ui::paintCompactBalanceChart (g, chart, state, context);
            const auto& texts = log.texts();
            const auto& areas = log.areas();
            const bool fits = blauertFont.getStringWidthFloat (blauertWords) <= static_cast<float> (width - 8);
            const auto partial = [&] (const juce::String& shown)
            {
                return shown.endsWithChar (0x2026) && blauertWords.startsWith (shown.dropLastCharacters (1).trimEnd())
                    && shown.length() > 1;
            };
            for (const auto& shown : texts)
                require (!partial (shown), "compact BALANCE never draws a shortened Blauert row");
            const auto blauert = texts.indexOf (blauertWords);
            if (height < 40.0f || !fits)
                require (blauert < 0, "narrow BALANCE hides Blauert entirely");
            else
                require (blauert >= 0 && areas[static_cast<size_t> (blauert)].getBottom() <= chart.getY(),
                         "a full-width Blauert caption does not cover a reading");
            std::set<float> rows;
            std::vector<juce::Rectangle<float>> cells;
            for (int index = 0; index < texts.size(); ++index)
            {
                const auto& t = texts[index];
                if (t != aText && t != bText && t != abText && t != abSharedUnitText
                    && t != targetText && t != frequency) continue;
                const auto& area = areas[static_cast<size_t> (index)];
                rows.insert (area.getY());
                if (audit)
                {
                    require (!area.isEmpty() && clip.contains (area)
                                 && area.getWidth() >= text_style::shownWidth (font, t),
                             "every BALANCE readout cell fits its complete rendered text and the production clip");
                    for (const auto& cell : cells)
                        require (!cell.intersects (area), "BALANCE readout cells do not overlap");
                    cells.push_back (area);
                }
            }
            if (!audit)
                return rows.size() == 3 && texts.contains (aText) && texts.contains (bText)
                    && texts.contains (targetText) && texts.contains (frequency);
            const auto target = texts.indexOf (targetText);
            require (target >= 0 && chart.contains (areas[static_cast<size_t> (target)]),
                     "complete TARGET endpoints remain on the plot, not clipped or abbreviated");
            require (texts.contains (frequency), "complete frequency and, below 1 kHz, note are always present");
            const bool combined = texts.contains (abText) || texts.contains (abSharedUnitText);
            if (height == 14.0f)
                require (rows.size() == 2 && cells.size() == 3 && combined && !texts.contains (aText) && !texts.contains (bText),
                         "minimum height keeps frequency and note, complete A and B and TARGET in only two rows");
            else
                require (rows.size() == 3 && cells.size() == 4 && texts.contains (aText) && texts.contains (bText) && !combined,
                         "the 24px and 40px controls keep all four separate readings in three rows");
            return true;
        };
        draw (160, 40.0f, 1250.0f, "1250 Hz", false);  // No shortened caption despite enough height.
        draw (160, 14.0f, 155.0f, "155 Hz D#3", true);  // The exact fifth audit counterexample.
        draw (160, 14.0f, 1250.0f, "1250 Hz", true);     // No note above 1 kHz, but never drop Hz.
        bool found = false;
        for (int width = 160; width <= 225; ++width)
        {
            if (!draw (width, 24.0f, 1250.0f, "1250 Hz", false)) continue;
            for (const auto height : { 14.0f, 24.0f, 40.0f }) draw (width, height, 1250.0f, "1250 Hz", true);
            found = true; break;
        }
        require (found, "same narrow width exercises the real two/three-row renderer at all three heights");
    }
}
}
