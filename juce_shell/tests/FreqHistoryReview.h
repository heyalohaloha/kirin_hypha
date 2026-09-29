#pragma once

#include "SpectrumTerrainShowcase.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Look review of FREQ's six-second landscape (2026-09-29): the POST view fed one frame at a time,
// for a drum track (hits that decay between them) and a busy mix with frame-to-frame detail. Each
// ridge is the highest level of its quarter second and moves back smoothly. Sixty consecutive frames
// of each, for motion, and one still at device scale 2. Written only when
// KIRIN_HYPHA_FREQ_HISTORY_REVIEW_DIR names a directory.
namespace hypha::tests
{
namespace freq_history_review
{
using freq_showcase::bandHz;
using freq_showcase::gaussOct;
using freq_showcase::pulse;

inline float hashNoise (int band, int index) noexcept
{
    auto h = static_cast<std::uint32_t> (band) * 2654435761u ^ static_cast<std::uint32_t> (index) * 2246822519u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return static_cast<float> (h & 0xffffu) / 65535.0f * 2.0f - 1.0f;
}

// Power sum of components in dBFS.
inline float sumDb (std::initializer_list<float> levels) noexcept
{
    double power = 0.0;
    for (const auto level : levels)
        power += std::pow (10.0, level / 10.0);
    return (float) (10.0 * std::log10 (power));
}

inline float decayDb (float t, float period, float offset, float decay) noexcept
{
    return 20.0f * std::log10 (std::max (1.0e-5f, pulse (t, period, offset, decay)));
}

inline float shapeDb (float hz, float centre, float width) noexcept
{
    return 20.0f * std::log10 (std::max (1.0e-5f, gaussOct (hz, centre, width)));
}

// A drum track: kick, snare and hats over a quiet floor; between hits the spectrum falls away.
inline float drumsDbfs (float hz, float t, int band, int index) noexcept
{
    const auto kick = -10.0f + shapeDb (hz, 60.0f, 0.6f) + decayDb (t, 0.5f, 0.0f, 0.12f);
    const auto click = -34.0f + shapeDb (hz, 3'500.0f, 1.0f) + decayDb (t, 0.5f, 0.0f, 0.012f);
    const auto snareBody = -14.0f + shapeDb (hz, 220.0f, 0.8f) + decayDb (t, 1.0f, 0.5f, 0.10f);
    const auto snareWire = -22.0f + shapeDb (hz, 5'000.0f, 1.3f) + decayDb (t, 1.0f, 0.5f, 0.08f);
    const auto hat = -28.0f + shapeDb (hz, 9'500.0f, 0.6f) + decayDb (t, 0.25f, 0.125f, 0.03f);
    const auto floor = -92.0f - 3.0f * std::log2 (hz / 100.0f);
    return juce::jlimit (-140.0f, -1.0f, sumDb ({ kick, click, snareBody, snareWire, hat, floor })
                                          + 2.5f * hashNoise (band, index));
}

// A busy mix: the showcase's drum-and-vocal mix with the band-to-band and frame-to-frame detail a
// real 85 ms spectrum has.
inline float mixDbfs (float hz, float t, int band, int index) noexcept
{
    return juce::jlimit (-140.0f, -1.0f, freq_showcase::preDbfs (hz, t) + 4.0f * hashNoise (band, index)
                                          + 2.0f * hashNoise (band / 3, index));
}

template <typename Level>
KirinSpectrumView frame (int index, Level level)
{
    auto view = freq_showcase::frame (index);
    const auto t = (float) index / 30.0f;
    for (size_t band = 0u; band < KIRIN_SPECTRUM_BAND_COUNT; ++band)
    {
        const auto hz = bandHz (band);
        const auto pre = level (hz, t, (int) band, index);
        const auto change = freq_showcase::chainDb (hz, t);
        view.pre_dbfs[band] = pre;
        view.post_dbfs[band] = pre + change;
        view.display_db[band] = change;
    }
    return view;
}

template <typename Level>
bool writeRun (const juce::File& directory, const juce::String& name, Level level)
{
    constexpr int editorWidth = 900, editorHeight = 600;
    SpectrumComponent component;
    component.setAbsoluteObservation (true);
    component.setPresentationContext (presentation::forEditor (editorWidth, editorHeight));
    component.setSignalActive (true);
    const auto bounds = ui_contract::spectrumPlotBounds (editorWidth, editorHeight);
    component.setSize (bounds.width, bounds.height);
    const auto frames = directory.getChildFile (name);
    if (! frames.createDirectory())
        return false;
    bool written = true;
    constexpr int total = 240, recorded = 60;
    for (int index = 0; index < total; ++index)
    {
        component.setSnapshot (frame (index, level));
        if (index < total - recorded)
            continue;
        for (const auto dpi : { 1.0f, 2.0f })
        {
            if (dpi > 1.0f && index != total - 1)
                continue;
            juce::Image image (juce::Image::ARGB, (int) std::ceil (bounds.width * dpi),
                               (int) std::ceil (bounds.height * dpi), true);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (dpi));
            component.paintEntireComponent (g, true);
            const auto file = dpi > 1.0f ? directory.getChildFile (name + "_still@2x.png")
                                         : frames.getChildFile (juce::String (index - (total - recorded)).paddedLeft ('0', 3) + ".png");
            written = written && freq_showcase::writePng (file, image);
        }
    }
    return written;
}
}

inline bool writeFreqHistoryReview()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_FREQ_HISTORY_REVIEW_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    return freq_history_review::writeRun (directory, "drums", freq_history_review::drumsDbfs)
        && freq_history_review::writeRun (directory, "mix", freq_history_review::mixDbfs);
}
}
