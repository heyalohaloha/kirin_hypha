#pragma once
#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaLanguage.h"
#include "UiRefinementContract.h"

namespace hypha::tests::spectrum_lr_surface
{
inline KirinSpectrumView frame (int index, uint32_t mode, bool alternative)
{
    KirinSpectrumView result {};
    result.status = KIRIN_SPECTRUM_NO_PAIR;
    result.post_has_data = 1;
    result.channel_mode = static_cast<uint8_t> (mode);
    result.channels = 2; result.sample_rate = 48'000;
    result.presentation_end_samples = (index + 1) * 1600;
    result.aperture_samples = 4096; result.fft_size = 8192;
    result.min_hz = 10; result.max_hz = 22000; result.approximate_below_hz = 35.15625f;
    for (size_t band = 0; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
        result.post_dbfs[band] = index == 0 ? -12.0f : index == 89 ? -24.0f
            : (alternative ? -36.0f : -66.0f) + 6.0f * std::sin (static_cast<float> (band) * .03f);
    return result;
}
inline int differences (const juce::Image& a, const juce::Image& b)
{
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        count += a.getPixelAt (x, y) != b.getPixelAt (x, y);
    return count;
}
inline void verify()
{
    using ui_refinement::check;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto preset : observatory::sizePresets)
            for (const auto mode : { KIRIN_SPECTRUM_CHANNEL_LR, KIRIN_SPECTRUM_CHANNEL_MID, KIRIN_SPECTRUM_CHANNEL_SIDE })
            {
                SpectrumComponent first, second;
                const auto bounds = ui_contract::spectrumPlotBounds (preset.width, preset.height);
                for (auto* component : { &first, &second })
                {
                    component->setAbsoluteObservation (true); component->setSignalActive (true);
                    component->setPresentationContext (presentation::forEditor (preset.width, preset.height));
                    component->setSize (bounds.width, bounds.height);
                    for (int index = 0; index < 90; ++index)
                        component->setSnapshot (frame (index, mode, component == &second));
                    check (component->absoluteHistorySizeForTest() == 90, "all modes retain the same measured history");
                }
                const auto a = first.createComponentSnapshot (first.getLocalBounds());
                const auto b = second.createComponentSnapshot (second.getLocalBounds());
                check ((differences (a, b) == 0) == (mode == KIRIN_SPECTRUM_CHANNEL_LR),
                       "only LR ignores both landscape and flat-field history at the real component paint entry");
                const auto size = first.absoluteHistorySizeForTest();
                first.setAbsoluteObservation (false); first.setAbsoluteObservation (true);
                check (first.absoluteHistorySizeForTest() == size, "POST/delta switches do not discard retained history");
                first.onChannelModeChange = [] (uint8_t) { return true; };
                const auto outer = spectrum_geometry::plotBoundsFor (first.getLocalBounds().toFloat());
                const auto scale = spectrum_geometry::visualScaleFor (first.getLocalBounds().toFloat());
                const auto point = spectrum_geometry::displayModeBoundsFor ((mode + 1) % 3, outer, scale).getCentre();
                const auto now = juce::Time::getCurrentTime();
                first.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point, {},
                    1, 0, 0, 0, 0, &first, &first, now, point, now, 1, false));
                check (first.absoluteHistorySizeForTest() == size, "channel mode action does not discard history");
                first.setSnapshot (frame (89, mode, false)); first.setSignalActive (false);
                second.clearSnapshot(); second.setSignalActive (false);
                const auto stopped = first.createComponentSnapshot (first.getLocalBounds());
                const auto bare = second.createComponentSnapshot (second.getLocalBounds());
                check ((differences (stopped, bare) == 0) == (mode == KIRIN_SPECTRUM_CHANNEL_LR),
                       "stopped LR has no history while MID and SIDE keep their dimmed history");
                ui_refinement::save (a, juce::String ("freq-") + juce::String (mode)
                    + (language == i18n::Language::japanese ? "-ja-" : "-en-") + juce::String (preset.width));
            }
    }
}
}
