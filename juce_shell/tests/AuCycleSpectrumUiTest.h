#pragma once
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>
#include <stdexcept>

namespace hypha::tests
{
inline void verifyAuCycleSpectrumUi()
{
    const auto require = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    int cases = 0;
    for (int channels : { 1, 2 })
        for (auto language : { i18n::Language::english, i18n::Language::japanese })
            for (int width : { 300, 375, 450 })
            {
                const i18n::ScopedLanguage scoped (language);
                SpectrumComponent component;
                const auto bounds = ui_contract::spectrumPlotBounds (width, width * 2 / 3);
                component.setSize (bounds.width, bounds.height);
                component.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
                component.setSignalActive (true);
                component.setPairingLoopBlock (live_compare::LoopPairingBlock::auCycle);
                KirinSpectrumView frame {};
                frame.status = KIRIN_SPECTRUM_UNAVAILABLE;
                frame.post_has_data = 1; frame.channels = static_cast<uint8_t> (channels);
                frame.sample_rate = 48000; frame.aperture_samples = 4096; frame.fft_size = 8192;
                frame.approximate_below_hz = 35.15625f; frame.min_hz = 10; frame.max_hz = 22000;
                frame.presentation_end_samples = 48000;
                std::fill (std::begin (frame.post_dbfs), std::end (frame.post_dbfs), -24.0f);
                for (bool absolute : { false, true })
                {
                    component.setAbsoluteObservation (absolute);
                    component.setSnapshot (frame);
                    require (std::abs (component.displayedPostForTest (128) + 24.0f) < 0.0001f,
                             "AU Cycle keeps the actual POST bins in both FREQ targets");
                    require (! component.readoutDeltaValidForTest (128), "no unproven delta readout");
                    require (component.absoluteHistorySizeForTest() > 0, "POST history remains available");
                    text_style::ShownTextLog shown;
                    const auto image = component.createComponentSnapshot (component.getLocalBounds());
                    const auto text = shown.texts().joinIntoString (" ");
                    require (! text.contains (text_style::shownText ("ANALYSIS DATA UNAVAILABLE")),
                             "local POST is not analysis unavailable");
                    const auto instruction = text_style::shownText ("Turn Cycle off to compare").removeCharacters (" \n");
                    require (text.removeCharacters (" \n").contains (instruction) == ! absolute,
                             "only the delta target draws the complete Cycle instruction");
                    const auto plot = spectrum_geometry::dataPlotBoundsFor (component.getLocalBounds().toFloat(), false);
                    const int y = juce::roundToInt (plot.getY() + plot.getHeight() * 0.25f);
                    int ink = 0;
                    for (int x = juce::roundToInt (plot.getX() + 4); x < plot.getRight() - 4; ++x)
                    {
                        bool present = false;
                        for (int row = y - 2; row <= y + 2; ++row)
                        {
                            const auto pixel = image.getPixelAt (x, row);
                            present = present || (pixel.getRed() > 80 && pixel.getRed() > pixel.getGreen() + 10
                                && pixel.getGreen() > pixel.getBlue() + 20);
                        }
                        ink += present ? 1 : 0;
                    }
                    require (ink > plot.getWidth() * 0.8f, "POST spectrum has a visible continuous curve");
                    if (! absolute)
                    {
                        auto stale = frame;
                        stale.status = KIRIN_SPECTRUM_ACTIVE; stale.has_data = 1;
                        std::fill (std::begin (stale.display_db), std::end (stale.display_db), 12.0f);
                        component.setSnapshot (stale);
                        const auto withheld = component.createComponentSnapshot (component.getLocalBounds());
                        for (int row = juce::roundToInt (plot.getY()); row < plot.getBottom(); ++row)
                            for (int x = juce::roundToInt (plot.getX()); x < plot.getRight(); ++x)
                                require (image.getPixelAt (x, row) == withheld.getPixelAt (x, row),
                                         "Cycle guidance cannot display a previous paired delta");
                        component.setSnapshot (frame);
                    }
                    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
                    if (directory.isNotEmpty() && channels == 2)
                    {
                        auto out = juce::File (directory).getChildFile ("freq-cycle-" + juce::String (width)
                            + (language == i18n::Language::english ? "-en" : "-ja")
                            + (absolute ? "-post.png" : "-delta.png")).createOutputStream();
                        require (out && out->setPosition (0) && out->truncate().wasOk()
                            && juce::PNGImageFormat().writeImageToStream (image, *out), "save FREQ review");
                    }
                    ++cases;
                }
                component.setAbsoluteObservation (false);
                component.setPairingLoopBlock (live_compare::LoopPairingBlock::none);
                frame.status = KIRIN_SPECTRUM_ACTIVE; frame.has_data = 1;
                std::fill (std::begin (frame.pre_dbfs), std::end (frame.pre_dbfs), -24.0f);
                component.setSnapshot (frame);
                require (component.readoutDeltaValidForTest (128) && std::abs (component.readoutDeltaForTest (128)) < 0.0001f,
                         "verified comparison recovers after Cycle off");
            }
    std::cout << "AU Cycle FREQ UI: " << cases << " layout/language/size/target cases PASS\n";
}
}
