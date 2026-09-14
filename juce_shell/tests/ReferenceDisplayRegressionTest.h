#pragma once

#include "../src/HyphaReferenceDisplayText.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaReferenceVisuals.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyReferenceDisplayRegression()
{
    const auto check = [] (bool valid, const char* message)
    {
        if (!valid) { std::cerr << "Reference display: " << message << '\n'; std::exit (1); }
    };
    reference_ui::State state;
    state.separateComparisons = state.libraryReceived = state.osOnline = true;
    state.osAccess = os_access::State::ready;
    state.readiness = reference_ui::Readiness::ready;
    state.title = "Reference song / session metadata and a deliberately long source name";
    state.presetId = "saved"; state.checkId = "dynamics"; state.cueId = "whole";
    state.presets = { { "saved", juce::String::fromUTF8 ("全工程｜基本3項目") },
                      { "custom", juce::String::fromUTF8 ("夜明けの音") } };
    state.checks = { { "dynamics", juce::String::fromUTF8 ("ダイナミクス") },
                     { "low", juce::String::fromUTF8 ("低域") } };
    state.cues = { { "whole", juce::String::fromUTF8 ("曲全体") } };
    state.checkLabel = state.checks.front().label;
    state.viewBindings = { "dynamics", "loudness" };
    state.status = "PLAY TO AUDITION";
    reference_ui::Component component;
    for (const auto width : { 300, 375, 450, 600, 900 })
    {
        component.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        component.setSize (width - 12, width == 900 ? 470 : width * 2 / 3 - 64);
        component.setState (state);
        const auto& shown = component.state();
        check (shown.presets.front().label == "Quick Reference" && shown.checkLabel == "Dynamics"
            && shown.cues.front().label == "Full track", "standard names must display in English");
        check (shown.presets[1].label == state.presets[1].label && shown.title == state.title,
               "custom names and source metadata must be preserved");
        auto* preset = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-preset"));
        check (preset && preset->getText() == "Quick Reference", "selector must use display labels");
        const auto font = preset->getLookAndFeel().getComboBoxFont (*preset);
        check (font.getHeight() <= preset->getHeight(), "selector font must fit its row");
        auto* b = component.findChildWithID ("reference-b");
        auto* c = component.findChildWithID ("reference-c");
        check (b && c && !b->getBounds().intersects (c->getBounds()), "A/B/C controls must stay separate");
        juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image); component.paintEntireComponent (graphics, true);
        const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_DISPLAY_OUTPUT", {});
        if (output.isNotEmpty())
        {
            const auto root = juce::File (output); check (root.createDirectory(), "render directory");
            juce::FileOutputStream stream (root.getChildFile (juce::String (width) + ".png"));
            check (juce::PNGImageFormat().writeImageToStream (image, stream), "render evidence");
        }
    }
    const auto font = nativeTextFont (presentation::forEditor (900, 600), typography::TextRole::body);
    const auto title = juce::String::repeatedString ("Long source title ", 64);
    for (const auto width : { -1.0f, 0.0f, 5.0f, 30.0f, 100.0f, 400.0f })
    {
        const auto fitted = text_style::ellipsizedText (title, font, width);
        check (font.getStringWidthFloat (fitted) <= juce::jmax (0.0f, width), "long title must fit");
    }
    check (text_style::ellipsizedText ("Mix", font, 400.0f) == "Mix", "short title must stay intact");
    check (text_style::ellipsizedText ({}, font, 400.0f).isEmpty(), "empty title must stay empty");

    // A short real measurement can contain a timeline with only null samples.
    // It must render the same unavailable state as a missing timeline.
    const auto plot = [&state] {
        juce::Image image (juce::Image::ARGB, 450, 240, true);
        juce::Graphics graphics (image);
        reference_ui::paintConfiguredReferenceViews (graphics, image.getBounds().toFloat(),
            state, presentation::forEditor (900, 600));
        return image;
    };
    const auto missing = plot();
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->dynamics.emplace(); measurement->loudness.emplace();
    measurement->dynamics->series["psr_millidb"] = { std::nullopt, std::nullopt };
    measurement->loudness->series["lufs_s_millilu"] = { std::nullopt, std::nullopt };
    state.detailedMeasurement = measurement;
    const auto unavailable = plot();
    for (int y = 0; y < missing.getHeight(); ++y)
        for (int x = 0; x < missing.getWidth(); ++x)
            check (missing.getPixelAt (x, y) == unavailable.getPixelAt (x, y),
                   "null-only timeline must display NO DATA");
}
}
