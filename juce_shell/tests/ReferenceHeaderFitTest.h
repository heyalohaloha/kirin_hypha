#pragma once
#include "ReferenceADirectionTest.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceLegend.h"
#include "../src/HyphaReferenceVisualLayout.h"
#include <algorithm>
#include <limits>

namespace hypha::tests::reference_text_repairs
{
// Inspect each header in isolation: a complete legend in the wider chart must
// not conceal an ellipsis in its narrower neighbour.
inline void verifyChartHeader (juce::Rectangle<float> cell, presentation::Context context,
                               const juce::String& detail)
{
    using reference_guide_contract::require;
    text_style::ShownTextLog log;
    juce::Image image (juce::Image::ARGB, juce::roundToInt (cell.getRight()) + 1,
                       juce::roundToInt (cell.getBottom()) + 1, true);
    juce::Graphics g (image);
    const auto full = cell;
    reference_ui::paintReferenceHeader (g, cell, "LOW FREQUENCY", detail, context);
    for (const auto& label : reference_ui::referenceLegendParts (detail))
    {
        const auto translated = text_style::shownText (label);
        const auto compact = [] (const juce::String& text) { return text.removeCharacters (" \t\r\n"); };
        int first = -1, last = -1;
        for (int start = 0; start < log.texts().size() && first < 0; ++start)
        {
            juce::String combined;
            for (int end = start; end < log.texts().size(); ++end)
            {
                combined += compact (log.texts()[end]);
                if (combined == compact (translated)) { first = start; last = end; break; }
                if (!compact (translated).startsWith (combined)) break;
            }
        }
        require (first >= 0, "each chart keeps its whole source label: " + translated
            + " in " + juce::String (full.getWidth()) + " px; drew " + log.texts().joinIntoString (" | "));
        auto area = log.areas()[static_cast<size_t> (first)];
        for (int line = first + 1; line <= last; ++line) area = area.getUnion (log.areas()[static_cast<size_t> (line)]);
        const auto font = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
        const auto actualFont = requiresJapaneseGlyphs (translated) ? nativeTextFontLike (font) : font;
        const auto requiredHeight = text_style::shownWidth (font, label) <= area.getWidth()
            ? actualFont.getHeight() : text_style::shownWrappedHeight (label, font, int (area.getWidth()));
        require (requiredHeight <= area.getHeight() + 0.1f,
                 "the actual legend box holds all lines: " + translated + " at " + area.toString()
                    + ", needs " + juce::String (requiredHeight));
        require (area.getY() >= full.getY() && area.getBottom() <= cell.getY(), "legend ink stays above the plot");
    }
    require (log.texts().contains (text_style::shownText ("LOW FREQUENCY")), "the complete chart title is visible");
    require (cell.getHeight() >= 24, "the complete header retains a usable plot");
}

inline void verifyHeaderLayouts (reference_ui::Component& panel, reference_ui::State state,
                                presentation::Context context = presentation::forEditor (900, 600))
{
    for (const auto* layout : {"auto", "equal", "main"})
    {
        state.presentationLayout = layout; panel.setState (state);
        const auto area = panel.observationWindowBounds().toFloat();
        reference_guide_contract::require (!area.isEmpty(), "the real C page exposes its configured charts");
        for (int count : {1, 2, 3})
        {
            const auto cells = reference_ui::referenceVisualCells (area, count, layout);
            for (int index = 0; index < count; ++index)
                verifyChartHeader (cells[static_cast<size_t> (index)], context, reference_ui::cueSpectrumLegend (state));
        }
    }
    // A source label can exceed one row at a narrow capacity boundary. Exercise
    // wrapping directly; the expected phrase comes from the observation, not a fit formula.
    for (const int width : {100, 150})
        verifyChartHeader ({0, 0, float (width), 200}, context, "A LAST 180 S / C CHORUS / LEVEL NOT MATCHED");
}

inline void verifyRegularHeaderSizes()
{
    for (const auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto size : observatory::sizePresets)
        {
            observatory::View shell (observatory::Role::post);
            shell.setSize (size.width, size.height); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            auto state = a_direction::checkState(); state.blindLargeScreen = true;
            state.viewBindings = {"spectrum_full", "spectrum_low"}; state.cuePart = reference_audition::CuePart::chorus;
            state.cueLoudness = state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
            reference_ui::Component panel;
            const auto context = presentation::forEditor (size.width, size.height);
            panel.setPresentationContext (context); panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
            panel.setState (state);
            if (!panel.observationWindowBounds().isEmpty()) verifyHeaderLayouts (panel, state, context);
            else reference_guide_contract::require (!panel.detailedLayout(), "only compact pages omit the chart and open C at Inspection size");
        }
    }
}

inline void exportHeaderSnapshots (const juce::File& directory,
                                  std::vector<observatory::EditorSize> sizes)
{
    using reference_guide_contract::require;
    require (directory.createDirectory().wasOk(), "create chart evidence directory");
    for (const auto size : observatory::sizePresets)
        if (std::none_of (sizes.begin(), sizes.end(), [&] (const auto& item) { return item.width == size.width; }))
            sizes.push_back ({size.width, size.height});
    for (const auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto size : sizes)
            for (const auto* layout : {"equal", "main"})
            {
                const auto viewport = observatory::displayViewport (size.width, size.height);
                observatory::View shell (observatory::Role::post);
                shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
                shell.setExternalAnalysisBodyActive (true);
                auto state = a_direction::checkState(); state.blindLargeScreen = true;
                state.viewBindings = {"spectrum_full", "spectrum_low"}; state.presentationLayout = layout;
                state.cuePart = reference_audition::CuePart::chorus;
                state.cueLoudness = state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
                reference_ui::Component panel;
                panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
                shell.addAndMakeVisible (panel); panel.setBounds (shell.analysisBodyBounds()); panel.setState (state);
                juce::Image image (juce::Image::ARGB, size.width, size.height, true);
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (float (size.width) / viewport.width));
                shell.paintEntireComponent (g, true);
                const auto file = directory.getChildFile ((language == i18n::Language::japanese ? "ja-" : "en-")
                    + juce::String (size.width) + "x" + juce::String (size.height) + "-" + layout + ".png");
                auto output = file.createOutputStream();
                require (output && juce::PNGImageFormat().writeImageToStream (image, *output), "write the complete C page at its actual output size");
            }
    }
}
}
