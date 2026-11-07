#pragma once
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaHelpLineBar.h"
#include "../src/HyphaSurfacePresentation.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLanguage.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests::ui_refinement
{
inline void check (bool ok, const char* message)
{
    if (! ok) { std::cerr << "UI refinement: " << message << '\n'; std::exit (1); }
}
inline double luminance (juce::Colour colour)
{
    const auto linear = [] (double value) { return value <= .04045 ? value / 12.92 : std::pow ((value + .055) / 1.055, 2.4); };
    return .2126 * linear (colour.getFloatRed()) + .7152 * linear (colour.getFloatGreen())
         + .0722 * linear (colour.getFloatBlue());
}
inline double contrast (juce::Colour ink, juce::Colour background)
{
    const auto a = luminance (ink), b = luminance (background);
    return (std::max (a, b) + .05) / (std::min (a, b) + .05);
}
inline void save (const juce::Image& image, const juce::String& name)
{
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_UI_REFINEMENT_DIR", {});
    if (directory.isEmpty()) return;
    auto stream = juce::File (directory).getChildFile (name + ".png").createOutputStream();
    check (stream && stream->setPosition (0) && stream->truncate().wasOk()
           && juce::PNGImageFormat().writeImageToStream (image, *stream), "write preview");
}
inline void verify()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
        {
            for (int width = 300; width <= 900; ++width)
            {
                observatory::View view (role);
                auto* reference = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-reference"));
                check (reference != nullptr, "shared REF action exists");
                auto& context = view.contextMenuAnchor();
                view.setSize (width, juce::roundToInt (width * 2.0 / 3.0));
                for (const auto meterContext : { meter_context::MeterContext::twoMix, meter_context::MeterContext::trackStem,
                                                meter_context::MeterContext::twoMix })
                {
                    view.setMeterContext (meterContext);
                    check (reference->isVisible() && reference->isEnabled() == (role == observatory::Role::post), "REF remains visible with PRE disabled");
                    check (reference->getY() == context.getY() && reference->getHeight() == context.getHeight()
                           && reference->getX() > context.getRight(), "REF follows 2MIX in the same header row");
                    check (view.getLocalBounds().contains (reference->getBounds()) && ! reference->getBounds().intersects (context.getBounds()), "header controls fit");
                    check (view.informationAnchor().getExplicitFocusOrder() < context.getExplicitFocusOrder()
                           && context.getExplicitFocusOrder() < reference->getExplicitFocusOrder(), "accessibility follows header order");
                    const auto font = labelFont (presentation::forEditor (width, view.getHeight()), typography::TextRole::action);
                    for (auto* button : { reference, dynamic_cast<juce::Button*> (&context) })
                    {
                        const auto required = text_style::shownWidth (font, button->getButtonText());
                        if (required > button->getWidth() - 6)
                            std::cerr << width << " px " << button->getButtonText() << " needs " << required
                                      << ", has " << button->getWidth() - 6 << '\n';
                        check (required <= button->getWidth() - 6, "header label fits without shrinking or ellipsis");
                    }
                    const auto titleFont = labelFont (presentation::forEditor (width, view.getHeight()), typography::TextRole::shellTitle);
                    const auto titleStyle = typography::resolve (presentation::forEditor (width, view.getHeight()), typography::TextRole::shellTitle);
                    const auto titleMinimum = 12 + text_style::requiredWidth (titleFont, role == observatory::Role::post ? "POST" : "PRE", titleStyle)
                        + (observatory::densityForWidth (width) == observatory::Density::compact ? 3 : 5) + std::ceil (text_style::shownWidth (titleFont, "HYPHA"));
                    check (view.informationAnchor().getWidth() >= titleMinimum, "title keeps all its glyphs when lending unused space");
                    check (! view.informationAnchor().getBounds().intersects (context.getBounds()), "header hit areas remain disjoint");
                    juce::Button* post = nullptr; juce::Button* delta = nullptr;
                    for (auto* child : view.getChildren())
                        if (auto* button = dynamic_cast<juce::Button*> (child); button && button->isVisible())
                        {
                            if (button->getButtonText() == "POST") post = button;
                            if (button->getButtonText() == hypha::delta()) delta = button;
                        }
                    if (post && delta) check (post->getWidth() == delta->getWidth(), "POST and delta have equal widths");
                }
            }
            for (const auto preset : observatory::sizePresets)
            {
                observatory::View view (role);
                auto* reference = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-reference"));
                view.setSize (preset.width, preset.height);
                view.setReferenceOwned (false);
                check (reference->getTitle().contains ("About Kirin OS"), "license help is preserved");
                view.setReferenceOwned (true);
                check (reference->getTitle() == "Reference", "owner title is preserved");
                for (auto domain : { observatory::Domain::level, observatory::Domain::reference })
                {
                    view.setDomain (domain);
                    check (reference->getToggleState() == (role == observatory::Role::post && domain == observatory::Domain::reference), "REF selected state follows the open page");
                    const auto tag = juce::String (role == observatory::Role::post ? "post" : "pre")
                        + (language == i18n::Language::japanese ? "-ja-" : "-en-") + juce::String (preset.width)
                        + (domain == observatory::Domain::reference ? "-ref" : "-level");
                    save (view.createComponentSnapshot (view.getLocalBounds()), tag);
                }
            }
        }
    }
    HelpLineBar line;
    line.show (" ", { 0, 0, 900, 28 }, { 0, 0, 900, 28 }, presentation::forEditor (900, 600));
    const auto background = line.createComponentSnapshot (line.getLocalBounds());
    double oldMinimum = 100, newMinimum = 100;
    for (int y = 5; y < background.getHeight() - 5; ++y)
        for (int x = 8; x < background.getWidth() - 8; ++x)
        {
            oldMinimum = std::min (oldMinimum, contrast (COL_TEXT_SECONDARY, background.getPixelAt (x, y)));
            newMinimum = std::min (newMinimum, contrast (HelpLineBar::textColour(), background.getPixelAt (x, y)));
        }
    check (HelpLineBar::textColour() == COL_NORMAL && newMinimum >= 12 && newMinimum > oldMinimum * 2,
           "ivory help has strong contrast without changing other tiers");
    std::cout << "Help line contrast: BG " << contrast (COL_TEXT_SECONDARY, BG) << " -> " << contrast (HelpLineBar::textColour(), BG)
              << "; rendered panel minimum " << oldMinimum << " -> " << newMinimum << ":1\n";
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        line.show ("POST Spectrum: current magnitude and rolling peak hold.", { 0, 0, 900, 28 }, { 0, 0, 900, 28 }, presentation::forEditor (900, 600));
        save (line.createComponentSnapshot (line.getLocalBounds()), language == i18n::Language::japanese ? "help-ja" : "help-en");
    }
}
}
