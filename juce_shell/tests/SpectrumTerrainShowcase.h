#pragma once

#include "../src/HyphaSpectrumComponent.h"
#include "../src/HyphaSpectrumUiContract.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>

// Look review renders for FREQ. Six seconds of a drum-and-vocal mix measured before and after a
// mastering chain (a low shelf, a 350 Hz cut, presence and air, and a compressor that ducks the
// lows on every kick), fed one presentation frame at a time. Written only when
// KIRIN_HYPHA_FREQ_SHOWCASE_DIR names a directory.
namespace hypha::tests
{
namespace freq_showcase
{
constexpr uint32_t rate = 48'000;
constexpr int frameCount = 180;
constexpr int64_t frameSamples = rate / 30;

inline float gaussOct (float hz, float centreHz, float widthOct) noexcept
{
    const auto octaves = std::log2 (hz / centreHz) / widthOct;
    return std::exp (-0.5f * octaves * octaves);
}

inline float bandHz (size_t band) noexcept
{
    const auto position = ((float) band + 0.5f) / (float) KIRIN_SPECTRUM_BAND_COUNT;
    return 10.0f * std::pow (22'000.0f / 10.0f, position);
}

inline float pulse (float t, float period, float offset, float decay) noexcept
{
    auto since = std::fmod (t - offset + 100.0f * period, period);
    return std::exp (-since / decay);
}

inline float preDbfs (float hz, float t) noexcept
{
    auto level = hz > 100.0f ? -30.0f - 4.5f * std::log2 (hz / 100.0f)
                             : -30.0f - 2.0f * std::log2 (100.0f / hz);
    if (hz < 30.0f)
        level -= 12.0f * std::log2 (30.0f / hz);
    const auto kick = pulse (t, 0.5f, 0.0f, 0.12f);
    const auto snare = pulse (t, 1.0f, 0.5f, 0.09f);
    const auto hat = pulse (t, 0.125f, 0.0625f, 0.04f);
    level += 14.0f * kick * gaussOct (hz, 62.0f, 0.55f);
    level += 10.0f * snare * (gaussOct (hz, 240.0f, 1.1f) + 0.6f * gaussOct (hz, 5'000.0f, 0.9f));
    level += 6.0f * hat * gaussOct (hz, 9'500.0f, 0.7f);
    const auto formant = 700.0f + 300.0f * std::sin (0.9f * t);
    level += 8.0f * gaussOct (hz, formant, 0.35f) + 6.0f * gaussOct (hz, 2'600.0f, 0.4f);
    level += 0.6f * std::sin (hz * 0.013f + t * 7.0f) * std::cos (hz * 0.0071f - t * 3.0f);
    return juce::jlimit (-140.0f, -1.0f, level);
}

inline float chainDb (float hz, float t) noexcept
{
    auto gain = 3.0f * (1.0f / (1.0f + std::pow (hz / 80.0f, 2.0f)))
              - 2.5f * gaussOct (hz, 350.0f, 0.7f)
              + 2.0f * gaussOct (hz, 3'000.0f, 0.8f)
              + 3.0f * (1.0f / (1.0f + std::pow (10'000.0f / hz, 2.0f)));
    const auto kick = pulse (t, 0.5f, 0.0f, 0.18f);
    gain -= 3.5f * kick * (1.0f / (1.0f + std::pow (hz / 180.0f, 2.0f)));
    gain -= 1.2f * kick;
    return gain;
}

inline KirinSpectrumView frame (int index)
{
    KirinSpectrumView view {};
    view.sample_rate = rate;
    view.aperture_samples = 4'096u;
    view.fft_size = 8'192u;
    view.approximate_below_hz = 3.0f * (float) rate / 4'096.0f;
    view.min_hz = 10.0f;
    view.max_hz = 22'000.0f;
    view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    view.channels = 2u;
    view.status = KIRIN_SPECTRUM_ACTIVE;
    view.has_data = 1u;
    view.post_has_data = 1u;
    view.presentation_end_samples = frameSamples * (index + 1);
    const auto t = (float) index / 30.0f;
    for (size_t band = 0u; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
    {
        const auto hz = bandHz (band);
        const auto pre = preDbfs (hz, t);
        const auto change = chainDb (hz, t);
        view.pre_dbfs[band] = pre;
        view.post_dbfs[band] = pre + change;
        view.display_db[band] = change;
    }
    return view;
}

inline juce::Image render (int editorWidth, int editorHeight, bool history, float dpi,
                           bool absolute = false)
{
    SpectrumComponent component;
    component.setAbsoluteObservation (absolute);
    component.setPresentationContext (presentation::forEditor (editorWidth, editorHeight));
    component.setSignalActive (true);
    const auto bounds = ui_contract::spectrumPlotBounds (editorWidth, editorHeight);
    component.setSize (bounds.width, bounds.height);
    for (int index = history ? 0 : frameCount - 1; index < frameCount; ++index)
        component.setSnapshot (frame (index));
    juce::Image image (juce::Image::ARGB, (int) std::ceil (bounds.width * dpi),
                       (int) std::ceil (bounds.height * dpi), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (dpi));
    component.paintEntireComponent (g, true);
    return image;
}

inline bool writePng (const juce::File& file, const juce::Image& image)
{
    file.deleteFile();
    juce::FileOutputStream output { file };
    juce::PNGImageFormat png;
    return output.openedOk() && png.writeImageToStream (image, output);
}
}

inline bool writeSpectrumShowcase()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_FREQ_SHOWCASE_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    bool written = true;
    for (const auto& [width, height] : { std::array<int, 2> { 900, 600 }, std::array<int, 2> { 600, 400 } })
        for (const bool absolute : { false, true })
            for (const bool history : { false, true })
                for (const auto dpi : { 1.0f, 2.0f })
                    written = written && freq_showcase::writePng (
                        directory.getChildFile (juce::String (width) + (absolute ? "_post" : "_delta")
                                                + (history ? "_terrain" : "_flat")
                                                + (dpi > 1.0f ? "@2x.png" : ".png")),
                        freq_showcase::render (width, height, history, dpi, absolute));
    return written;
}
}
