#pragma once
#include <cstdlib>
#include <iostream>
#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"

// Called with a live delta spectrum and an adopted MARK at each tested size.
inline void verifySpectrumComparisonStatusContract (hypha::SpectrumComponent& spectrum)
{
    auto require = [] (bool condition)
    {
        if (! condition)
        {
            std::cerr << "FREQ pair-only comparison status contract failed\n";
            std::exit (EXIT_FAILURE);
        }
    };
    for (const auto language : { hypha::i18n::Language::english,
                                hypha::i18n::Language::japanese })
    {
        hypha::i18n::ScopedLanguage scoped (language);
        auto paint = [&] (bool legend)
        {
            juce::Image image (juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true);
            hypha::text_style::ShownTextLog log;
            juce::Graphics graphics (image);
            spectrum.paintEntireComponent (graphics, true);
            for (const auto& text : { juce::String::fromUTF8 ("Δ"), juce::String ("PRE"),
                                      juce::String ("POST"), juce::String ("MARK") })
                require (log.texts().contains (text) == legend);
            require (spectrum.hasMark());
            return image;
        };
        spectrum.setComparisonStatus ({});
        const auto baseline = paint (true);
        for (const auto reason : { KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT,
                                   KIRIN_COMPARISON_REASON_STALE,
                                   KIRIN_COMPARISON_REASON_LOCAL_INACTIVE })
        {
            spectrum.setComparisonStatus (hypha::comparison_presentation::spectrumStatusText (
                KIRIN_COMPARISON_STATE_HOLDING, static_cast<uint8_t> (reason)));
            const auto unchanged = paint (true);
            for (int y = 0; y < baseline.getHeight(); ++y)
                for (int x = 0; x < baseline.getWidth(); ++x)
                    require (baseline.getPixelAt (x, y) == unchanged.getPixelAt (x, y));
        }
        for (const auto reason : { KIRIN_COMPARISON_REASON_NO_PAIR,
                                   KIRIN_COMPARISON_REASON_PRE_BYPASSED,
                                   KIRIN_COMPARISON_REASON_PRE_INACTIVE,
                                   KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH,
                                   KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN,
                                   KIRIN_COMPARISON_REASON_AUDITION_ACTIVE,
                                   KIRIN_COMPARISON_REASON_UNSUPPORTED_VIEW,
                                   KIRIN_COMPARISON_REASON_UNSUPPORTED_METRIC })
        {
            const auto status = hypha::comparison_presentation::spectrumStatusText (
                KIRIN_COMPARISON_STATE_REJECTED, static_cast<uint8_t> (reason));
            require (status.isNotEmpty());
            spectrum.setComparisonStatus (status);
            paint (false);
        }
        spectrum.setComparisonStatus ({});
        paint (true);
    }
}
