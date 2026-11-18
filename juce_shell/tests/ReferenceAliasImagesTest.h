#pragma once
#include "../src/HyphaReferenceDisplayText.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void exportReferenceAliasImages()
{
    const auto destination = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_ALIAS_IMAGES", {});
    if (destination.isEmpty()) return;
    const auto require = [] (bool ok, const char* why)
    { if (! ok) { std::cerr << "Reference alias images: " << why << '\n'; std::exit (EXIT_FAILURE); } };
    const juce::File output (destination);
    require (! output.exists(), "fresh output directory required");
    require (output.createDirectory().wasOk(), "output directory");
    const char* const names[][2] {
        { u8"Recording · room, noise, and bleed", u8"録音｜部屋・ノイズ・他の音の混入" },
        { "Hook placement", u8"聴かせたい箇所の位置" },
        { "Hook", u8"聴かせたい箇所" },
        { "Separation and bleed", u8"分離と他の音の混入" },
        { "Bleed", u8"他の音の混入" },
    };
    const i18n::ScopedLanguage japanese (i18n::Language::japanese);
    juce::String report ("width\tcase\tcontrol\tvisible\tfont_px\ttext\tellipsized\ttooltip\tbounds\n");
    int images = 0, visibleNames = 0, failures = 0;
    for (const int width : { 300, 600, 900 }) for (int item = 0; item < 5; ++item)
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, width * 2 / 3); shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        reference_ui::Component panel;
        shell.addAndMakeVisible (panel);
        panel.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        reference_ui::State state;
        state.separateComparisons = state.libraryReceived = state.osOnline = true;
        state.osAccess = os_access::State::ready; state.readiness = reference_ui::Readiness::ready;
        state.comparisonSlot = 2; state.title = "Synthetic source";
        state.presetId = "00000000-0000-4000-8000-000000000101";
        state.checkId = "00000000-0000-4000-8000-000000002001/1";
        state.cueId = "whole"; state.cueLabel = "Full track";
        state.presetName = juce::String::fromUTF8 (names[0][0]);
        state.checkLabel = juce::String::fromUTF8 (names[item == 0 ? 4 : item][0]);
        state.presets = { { state.presetId, state.presetName } };
        state.checks = { { state.checkId, state.checkLabel + "  /  Synthetic source", state.checkLabel, "Synthetic source" } };
        state.cues = { { state.cueId, state.cueLabel } };
        panel.setState (state); panel.setBounds (shell.analysisBodyBounds());
        // Single selected options are real read-only Labels, not hidden failed Combos.
        // Compact C uses checkBox; inspection C uses the grouped checkChooser.
        juce::Component* selected = nullptr;
        const auto chooseVisible = [&] (const char* id)
        {
            if (selected == nullptr)
                if (auto* control = panel.findChildWithID (id); control && control->isVisible()
                    && ! control->getBounds().isEmpty()) selected = control;
        };
        if (item == 0)
        {
            chooseVisible ("reference-preset");
            chooseVisible ("reference-selection-value-0");
        }
        else
        {
            chooseVisible ("reference-check-choice");
            chooseVisible ("reference-check");
            chooseVisible ("reference-selection-value-2");
        }
        const auto name = juce::String::fromUTF8 (names[item][1]);
        const bool grouped = selected && selected->getComponentID() == "reference-check-choice";
        const auto expected = name + (item != 0 && ! grouped ? "  /  Synthetic source" : "");
        if (selected)
        {
            ++visibleNames;
            auto* combo = dynamic_cast<juce::ComboBox*> (selected);
            auto* label = dynamic_cast<juce::Label*> (selected);
            require (label || combo, "visible name has a supported native control type");
            const auto font = label ? label->getFont() : combo->getLookAndFeel().getComboBoxFont (*combo);
            const auto shown = label ? label->getText() : combo ? combo->getText() : juce::String {};
            const auto room = label ? label->getBorderSize().subtractedFrom (label->getLocalBounds()).getWidth()
                : juce::jmax (0, selected->getWidth() - selected->getHeight() - 6);
            const auto ellipsized = text_style::ellipsizedText (expected, font, static_cast<float> (room));
            // An ellipsis is allowed, squeezing below the declared font is not. Full text
            // remains in the Label tooltip; this does not claim a rendered hover preview.
            if ((! label && ! combo) || shown != expected || font.getHeight() < 12.0f
                || font.getHeight() > selected->getHeight() || room <= 0
                || ! panel.getLocalBounds().contains (selected->getBounds())
                || font.getStringWidthFloat (ellipsized) > room + 0.5f
                || (label && (label->getTooltip() != expected || label->getMinimumHorizontalScale() != 1.0f)))
                ++failures;
            for (const auto* id : { "reference-preset", "reference-check-choice", "reference-check",
                    "reference-selection-value-0", "reference-selection-value-2",
                    "reference-check-details", "reference-check-song", "reference-match" })
                if (const auto* other = panel.findChildWithID (id); other && other != selected && other->isVisible()
                    && other->getParentComponent() == selected->getParentComponent()
                    && other->getBounds().intersects (selected->getBounds())) ++failures;
            report += juce::String (width) + "\t" + juce::String (item) + "\t" + selected->getComponentID()
                + (label ? ":Label" : ":ComboBox") + "\t1\t"
                + juce::String (font.getHeight()) + "\t" + shown + "\t" + ellipsized
                + "\t" + (label ? label->getTooltip() : combo ? combo->getTooltip() : juce::String {})
                + "\t" + selected->getBounds().toString() + "\n";
        }
        else { ++failures; report += juce::String (width) + "\t" + juce::String (item) + "\tno-visible-name\t0\tNA\tNA\tNA\tNA\tNA\n"; }
        const auto file = output.getChildFile ("ja-" + juce::String (width) + "-alias-" + juce::String (item) + ".png");
        auto stream = file.createOutputStream();
        require (stream && juce::PNGImageFormat().writeImageToStream
            (shell.createComponentSnapshot (shell.getLocalBounds()), *stream), "write full shell image");
        ++images;
    }
    require (output.getChildFile ("controls.tsv").replaceWithText (report), "write inspection inventory");
    std::cout << "REFERENCE_ALIAS_IMAGES=" << images << " VISIBLE_NAMES=" << visibleNames << " FAILURES=" << failures << '\n';
    require (images == 15 && visibleNames == 15 && failures == 0, "name visibility/font/layout contract");
}
}
