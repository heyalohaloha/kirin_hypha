#pragma once

#include "ReferenceComparisonLayoutContract.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests::reference_compact_capacity
{
inline void expect (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Reference compact capacity: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG); component.paintEntireComponent (g, true);
    return image;
}

inline juce::Image comparisonGlass (reference_ui::ComparisonView& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics g (image); g.fillAll (BG);
    const key_light::Scope light (view);
    surface_material::paintObservationWell (g, view.getLocalBounds().toFloat(), false);
    reference_ui::window_material::paintInterior (g, view.getLocalBounds().toFloat(), view.getLocalBounds().toFloat());
    return image;
}

inline bool requireTextInk (const juce::Image& actual, const juce::Image& background,
                           juce::Rectangle<float> bounds, presentation::Context context,
                           typography::TextRole role, const juce::String& text,
                           int justification, juce::Colour ink, bool required = true)
{
    const auto line = std::ceil (typography::resolve (context, role,
        typography::Composition::visualization).lineHeight);
    expect (bounds.getHeight() >= line, "numerical output keeps its actual unchanged text line");
    juce::Image mask (juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true);
    juce::Graphics g (mask); g.setColour (juce::Colours::white);
    const auto shown = text_style::shownText (text);
    const auto contracted = labelFont (context, role, typography::Composition::visualization);
    g.setFont (requiresJapaneseGlyphs (shown) ? nativeTextFontLike (contracted)
        : labelFont (context, role, typography::Composition::visualization));
    g.drawText (shown, bounds.toNearestInt(), justification, false);
    // Native font antialiasing responds to ink colour. A white-alpha mask is only a
    // footprint; compare the actual text to the unchanged product ink on its real glass.
    auto expected = background.createCopy();
    juce::Graphics reference (expected); reference.setColour (ink);
    reference.setFont (requiresJapaneseGlyphs (shown) ? nativeTextFontLike (contracted)
        : labelFont (context, role, typography::Composition::visualization));
    reference.drawText (shown, bounds.toNearestInt(), justification, false);
    int glyphs = 0, visible = 0;
    const auto region = bounds.getSmallestIntegerContainer().getIntersection (actual.getBounds());
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (mask.getPixelAt (x, y).getAlpha() >= 160)
            {
                ++glyphs;
                const auto pixel = actual.getPixelAt (x, y);
                const auto wanted = expected.getPixelAt (x, y);
                const auto glass = background.getPixelAt (x, y);
                const auto contrast = int (wanted.getRed()) + wanted.getGreen() + wanted.getBlue()
                    - glass.getRed() - glass.getGreen() - glass.getBlue();
                visible += contrast > 30 && std::abs (int (pixel.getRed()) - wanted.getRed()) <= 3
                    && std::abs (int (pixel.getGreen()) - wanted.getGreen()) <= 3
                    && std::abs (int (pixel.getBlue()) - wanted.getBlue()) <= 3;
            }
    const bool valid = glyphs > 5 && visible * 5 >= glyphs * 4;
    if (! valid && required)
    {
        std::cerr << "Reference ink diagnostic: language=" << int (i18n::current())
                  << " editor=" << context.logicalWidth << "x" << context.logicalHeight
                  << " role=" << int (role) << " bounds=" << bounds.toString()
                  << " expected=" << text << " shown=" << shown
                  << " glyphs=" << glyphs << " visible=" << visible << '\n';
        const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REF_CAPACITY_REVIEW_DIR", {});
        if (path.isNotEmpty())
        {
            const juce::File directory (path);
            for (const auto& item : { std::make_pair ("failed-actual.png", actual),
                                      std::make_pair ("failed-mask.png", mask),
                                      std::make_pair ("failed-reference.png", expected),
                                      std::make_pair ("failed-background.png", background) })
            {
                juce::FileOutputStream stream (directory.getChildFile (item.first));
                if (stream.setPosition (0) && stream.truncate().wasOk())
                    juce::PNGImageFormat().writeImageToStream (item.second, stream);
            }
        }
        expect (false, "expected real numerical glyphs survive the glass, frame and curves");
    }
    return valid;
}

inline void writeIfRequested (juce::Component& root, const juce::String& name)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_REF_CAPACITY_REVIEW_DIR", {});
    if (path.isEmpty()) return;
    const juce::File directory (path);
    expect (directory.createDirectory(), "capacity review directory");
    juce::FileOutputStream stream (directory.getChildFile (name + ".png"));
    expect (stream.setPosition (0) && stream.truncate().wasOk(), "fresh PNG stream");
    expect (juce::PNGImageFormat().writeImageToStream (render (root), stream), "capacity review PNG");
}

// V の比較の窓（REF の主役の窓）の小さい大きさ：数値の行は波形と重ならず、ガラスと枠と線の上でも字が読める。
// 値が無いときは範囲を保って準備中と言い、Blind のあいだは隠れて、戻れば同じ範囲に戻る。
inline void verify()
{
    const auto pair = reference_comparison_layout::timeline();
    int layouts = 0;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        // REF is a POST-only shipping entry; PRE sanitizes this domain to LEVEL.
        for (const auto& preset : observatory::sizePresets)
        {
            const auto context = presentation::forEditor (preset.width, preset.height);
            juce::Component root; root.getProperties().set (key_light::rootProperty, true);
            root.setSize (preset.width, preset.height);
            observatory::View shell (observatory::Role::post); root.addAndMakeVisible (shell);
            shell.setBounds (root.getLocalBounds()); shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            reference_ui::Component panel; root.addAndMakeVisible (panel);
            panel.setPresentationContext (context); panel.setBounds (shell.analysisBodyBounds());
            auto state = reference_review::playing (reference_review::library());
            state.versionId = "v4"; state.versionReady = state.checkReady = true;
            state.versionStep = state.checkStep = reference_ui::SourceStep::ready;
            state.status = "READY / A REMAINS LIVE";
            state.comparisonSlot = 1; state.visualTimeline = pair; state.visualPositionSeconds = 8;
            panel.setState (state);
            auto* comparison = dynamic_cast<reference_ui::ComparisonView*> (panel.findChildWithID ("reference-comparison-view"));
            expect (comparison && comparison->isVisible(), "shipping route displays V comparison");
            const auto& layout = comparison->visualLayout();
            expect (! layout.readout.intersects (layout.waveform), "compact numerical row is clear of both waveforms");
            const auto name = juce::String (language == i18n::Language::english ? "en" : "ja")
                + "-post-" + juce::String (preset.width);
            const juce::String readout ("A 0.5 LU QUIETER / 3s");
            if (preset.width >= 450 && ! layout.detailed())
            {
                const auto background = comparisonGlass (*comparison);
                const auto actual = render (*comparison);
                requireTextInk (actual, background, layout.readout, context,
                    typography::TextRole::captureMetadata, readout,
                    juce::Justification::centredLeft, COL_TEXT_SECONDARY);
                expect (! requireTextInk (background, background, layout.readout, context,
                    typography::TextRole::captureMetadata, readout,
                    juce::Justification::centredLeft, COL_TEXT_SECONDARY, false),
                    "a glass fill alone cannot satisfy numerical ink coverage");
                auto obscured = actual.createCopy();
                juce::Graphics overlay (obscured); overlay.setColour (COL_NORMAL.withAlpha (0.65f));
                overlay.fillRect (layout.readout);
                expect (! requireTextInk (obscured, background, layout.readout, context,
                    typography::TextRole::captureMetadata, readout,
                    juce::Justification::centredLeft, COL_TEXT_SECONDARY, false),
                    "a bright overlay covering the glyphs cannot satisfy ink coverage");
            }
            writeIfRequested (root, name + "-version");
            const auto range = comparison->selectedRange();
            state.visualTimeline.reset(); panel.setState (state);
            expect (comparison->isVisible() && comparison->selectedRange() == range,
                    "missing data keeps its view range and empty state without fabricated measurements");
            if (preset.width >= 450)
                requireTextInk (render (*comparison), comparisonGlass (*comparison),
                    comparison->getLocalBounds().toFloat().reduced (6, 1), context,
                    typography::TextRole::captureMetadata, "Preparing V overview",
                    juce::Justification::centred, COL_TEXT_SECONDARY);
            writeIfRequested (root, name + "-missing");
            state.visualTimeline = pair; panel.setState (state);
            expect (comparison->selectedRange() == range, "verified observations return to the same view");
            state.blindPhase = reference_ui::BlindPhase::active; panel.setState (state);
            expect (! comparison->isVisible() && comparison->getTitle().isEmpty(),
                    "compact readouts remain concealed throughout Blind");
            state.blindPhase = reference_ui::BlindPhase::available; panel.setState (state);
            expect (comparison->isVisible() && comparison->selectedRange() == range,
                    "return from Blind keeps the selected interval");
            ++layouts;
        }
    }
    expect (layouts == 10, "POST shipping entry, both languages and all sizes were covered");
    std::cout << "Reference compact capacity: " << layouts << " shipping layouts passed\n";
}
}
