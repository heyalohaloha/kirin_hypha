#pragma once
#include "../src/HyphaAnalysisUiText.h"
#include "../src/HyphaObservationPageContract.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaTooltipLookAndFeel.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>
#include <stdexcept>

namespace hypha::tests
{
inline void verifyAbsoluteAnalysisHelp()
{
    const auto require = [] (bool value, const char* reason) {
        if (! value) throw std::runtime_error (reason);
    };
    const auto live = analysis_ui::liveOverviewTooltip();
    const juce::String page = observatory::pageCapabilities (observatory::Role::post,
        observatory::Domain::time, analysis_navigation::Page::absolute,
        observatory::ObservationTarget::delta).help;
    require (! live.containsIgnoreCase ("latency") && ! page.containsIgnoreCase ("latency"),
             "absolute LIVE help must not require output latency");
    require (analysis_ui::sharpnessDeltaTooltip().contains ("output latency"),
             "Sharpness Delta must keep its timing requirement");
    int cases = 0;
    for (const auto size : { observatory::sizePresets[0], observatory::sizePresets[1],
                             observatory::sizePresets[2] })
    {
        SpectrumComponent spectrum;
        // Choose PSB where its control exists, then retain that choice at the 100% view-only size.
        spectrum.setSize (juce::jmax (375, size.width), size.height);
        spectrum.setAbsoluteObservation (true);
        const auto bounds = spectrum.getLocalBounds().toFloat();
        const auto toggle = spectrum_geometry::subviewBoundsFor (
            spectrum_geometry::plotBoundsFor (bounds), spectrum_geometry::visualScaleFor (bounds));
        const auto point = toggle.getCentre();
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), point,
            {}, 0, 0, 0, 0, 0, &spectrum, &spectrum, now, point, now, 0, false);
        spectrum.mouseDown (event);
        require (spectrum.isPsbObservation(), "the real PSB control must be selected");
        const auto absolutePsb = spectrum.getTooltip();
        require (! absolutePsb.containsIgnoreCase ("latency"), "absolute PSB help needs no latency");
        spectrum.setAbsoluteObservation (false);
        require (spectrum.getTooltip().contains ("output latency"), "Delta PSB must retain its timing requirement on target change");
        const auto deltaPsb = spectrum.getTooltip();
        spectrum.setAbsoluteObservation (true);
        require (spectrum.getTooltip() == absolutePsb, "PSB help must follow the current target");
        spectrum.setSize (size.width, size.height);
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            juce::Image review (juce::Image::ARGB, size.width, size.height * 5, true);
            juce::Graphics reviewGraphics (review);
            int row = 0;
            for (const auto& text : { live, page, absolutePsb, deltaPsb, analysis_ui::sharpnessDeltaTooltip() })
            {
                require (i18n::hasTranslation (text), "analysis help needs a Japanese counterpart");
                const auto shown = text_style::shownText (text);
                require (language == i18n::Language::english || shown != text, "Japanese help must be translated");
                TooltipLookAndFeel look;
                const auto tooltip = look.getTooltipBounds (shown, { size.width - 1, size.height - 1 },
                    { 0, 0, size.width, size.height });
                require (spectrum.getLocalBounds().contains (tooltip), "help must fit inside the editor");
                const auto context = presentation::forOutput (450, 300, presentation::OutputTarget::tooltip);
                juce::AttributedString attributed;
                attributed.append (shown, requiresJapaneseGlyphs (shown)
                    ? nativeTextFont (context, typography::TextRole::tooltip)
                    : labelFont (context, typography::TextRole::tooltip), COL_NORMAL);
                juce::TextLayout complete;
                complete.createLayoutWithBalancedLineLengths (attributed, static_cast<float> (tooltip.getWidth() - 14));
                require (complete.getHeight() <= tooltip.getHeight() - 8, "the whole help text must fit without clipping");
                juce::Image image (juce::Image::ARGB, tooltip.getWidth(), tooltip.getHeight(), true);
                juce::Graphics graphics (image);
                look.drawTooltip (graphics, shown, image.getWidth(), image.getHeight());
                reviewGraphics.drawImageAt (image, 0, row * size.height);
                ++row;
                ++cases;
            }
            const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_ANALYSIS_HELP_REVIEW_DIR", {});
            if (directory.isNotEmpty())
            {
                auto output = juce::File (directory).getChildFile (juce::String (size.width)
                    + (language == i18n::Language::english ? "-en.png" : "-ja.png")).createOutputStream();
                require (output != nullptr && juce::PNGImageFormat().writeImageToStream (review, *output), "write help review image");
            }
        }
    }
    std::cout << "Absolute analysis help: PASS " << cases << " language/size/text cases\n";
}
}
