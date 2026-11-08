#pragma once
#include "HyphaDisplaySettings.h"
#include "HyphaSpectrumPresentation.h"
#include "HyphaSpectrumPeaks.h"
#include "kirin_hypha_ffi.h"
#include "kirin_hypha_display_ffi.h"
#include <limits>

namespace hypha::spectrum_display
{
using Bins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;
using Validity = std::array<uint8_t, KIRIN_SPECTRUM_BAND_COUNT>;

inline float tiltAt (size_t band, float minimumHz, float maximumHz, float dbPerOct) noexcept
{
    return dbPerOct <= 0 ? 0 : dbPerOct * std::log2 (minimumHz / 1000.0f)
        + dbPerOct * std::log2 (maximumHz / minimumHz) * (static_cast<float> (band) + 0.5f) / 256.0f;
}
inline Bins spatial (const Bins& source, float minimumHz, float maximumHz, int choice,
                     const Validity* valid = nullptr)
{
    if (choice == 0)
    {
        float raw[KIRIN_SPECTRUM_BAND_COUNT];
        std::copy (source.begin(), source.end(), raw);
        auto result = spectrum_presentation::calmLowFrequencies (raw,
            spectrum_presentation::lowFrequencyCalmWeights<KIRIN_SPECTRUM_BAND_COUNT> (minimumHz, maximumHz));
        // Never smooth across an unmeasured band, including the established low-band filter.
        if (valid)
            for (size_t i = 0; i < source.size(); ++i)
                for (size_t j = i > 2 ? i - 2 : 0; j <= std::min (i + 2, source.size() - 1); ++j)
                    if ((*valid)[j] == 0) { result[i] = source[i]; break; }
        return result;
    }
    if (choice == 1) return source;
    const auto widthOct = std::array<float, 5> { 0, 0, 1.0f / 24, 1.0f / 12, 1.0f / 6 }[(size_t) choice];
    const auto radius = widthOct * 0.5f * 256 / std::log2 (maximumHz / minimumHz);
    Bins output = source;
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (valid && (*valid)[i] == 0) continue;
        float sum = source[i], weight = 1;
        for (int direction : { -1, 1 })
            for (int step = 1; step < radius + 1; ++step)
            {
                const auto at = static_cast<int> (i) + direction * step;
                if (at < 0 || at >= static_cast<int> (source.size()) || (valid && (*valid)[(size_t) at] == 0)) break;
                const auto part = std::clamp (radius + 1 - step, 0.0f, 1.0f);
                sum += source[(size_t) at] * part; weight += part;
            }
        output[i] = sum / weight;
    }
    return output;
}

// Keep a whole set of factual peak labels for one second. The picture may continue moving;
// the selected notes, order, frequencies and levels remain readable until the next reading.
class PeakReadout
{
public:
    void clear() noexcept { peaks = {}; updated = 0; initialized = false; }
    bool update (const Bins& values, float minimumHz, float maximumHz, double now)
    {
        if (initialized && now - updated < 1000.0) return false;
        Bins hz;
        for (size_t i = 0; i < hz.size(); ++i)
            hz[i] = minimumHz * std::pow (maximumHz / minimumHz, (static_cast<float> (i) + 0.5f) / 256);
        peaks = spectrum_peaks::find (hz.data(), values.data(), values.size());
        updated = now; initialized = true; return true;
    }
    spectrum_peaks::Peaks peaks;
    double updated = 0;
private:
    bool initialized = false;
};

class PsbMotion
{
public:
    static double ceilingFor (const std::array<double, 20>& values, bool delta) noexcept
    {
        double peak = 0;
        for (const auto value : values) peak = std::max (peak, delta ? std::abs (value) : value);
        const auto step = delta ? 0.01 : 0.05;
        return std::min (1.0, std::max (delta ? 0.02 : 0.10, std::ceil (peak / step) * step));
    }
    void clear() noexcept { ready = false; shown.fill (0); ceiling = 0; }
    void accept (const KirinPsbView& frame, bool valid, double now)
    {
        const bool continuous = ready && valid && frame.is_delta == previous.is_delta
            && frame.sample_rate == previous.sample_rate && frame.channels == previous.channels
            && frame.state_epoch_samples == previous.state_epoch_samples
            && frame.presentation_end_samples >= previous.presentation_end_samples
            && frame.presentation_end_samples - previous.presentation_end_samples <= frame.aperture_samples;
        std::copy (std::begin (frame.shares), std::end (frame.shares), target.begin());
        targetCeiling = ceilingFor (target, frame.is_delta != 0);
        if (! continuous) { shown = target; ceiling = targetCeiling; lastTick = now; }
        previous = frame; ready = valid;
    }
    bool advance (double now)
    {
        if (! ready || now <= lastTick) return false;
        const auto response = -std::expm1 (-(now - lastTick) / 80.0);
        lastTick = now;
        const auto beforeCeiling = ceiling;
        ceiling += (targetCeiling - ceiling) * response;
        if (std::abs (targetCeiling - ceiling) < 1.0e-8) ceiling = targetCeiling;
        bool changed = std::abs (beforeCeiling - ceiling) > 0;
        for (size_t i = 0; i < shown.size(); ++i)
        {
            const auto before = shown[i];
            shown[i] += (target[i] - shown[i]) * response;
            if (std::abs (target[i] - shown[i]) < 1.0e-8) shown[i] = target[i];
            changed = changed || std::abs (before - shown[i]) > 0.0;
        }
        return changed;
    }
    std::array<double, 20> shown {};
    double ceiling = 0;
private:
    std::array<double, 20> target {};
    KirinPsbView previous {};
    double lastTick = 0, targetCeiling = 0;
    bool ready = false;
};

// Only the measured background is cached. Live curves, probes and labels are painted every
// frame. The instance owns the image, so one editor cannot invalidate another editor's field.
class Backdrop
{
public:
    void invalidate() { image = {}; }
    template <typename Draw>
    void paint (juce::Graphics& g, juce::Rectangle<float> plot, uint64_t revision,
                display_settings::Values settings, Draw&& draw)
    {
        if (! enabled) { draw (g); ++builds; return; }
        const auto dpi = pixelScale (g);
        if (needs (plot, revision, settings, dpi))
        {
            prepare (plot, dpi);
            juce::Graphics target (image);
            target.addTransform (juce::AffineTransform::translation (-plot.getX(), -plot.getY()).scaled (dpi));
            draw (target);
            remember (plot, revision, settings, dpi);
        }
        g.drawImage (image, plot);
    }
    // The terrain already rasterizes its pixels. Keep that raster itself, avoiding a second
    // full-size image and compositing pass at every 30 Hz observation.
    template <typename Draw>
    void paintRaster (juce::Graphics& g, juce::Rectangle<float> plot, uint64_t revision,
                      display_settings::Values settings, Draw&& draw)
    {
        const auto dpi = pixelScale (g);
        if (needs (plot, revision, settings, dpi))
        {
            prepare (plot, dpi);
            draw (image, dpi);
            remember (plot, revision, settings, dpi);
        }
        const juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setOpacity (1);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (image, { plot.getX(), plot.getY(), static_cast<float> (image.getWidth()) / dpi, static_cast<float> (image.getHeight()) / dpi });
    }
    unsigned builds = 0;
    bool enabled = true;
private:
    static float pixelScale (juce::Graphics& g)
    { return std::clamp (g.getInternalContext().getPhysicalPixelScaleFactor(), 1.0f, 4.0f); }
    bool needs (juce::Rectangle<float> plot, uint64_t revision, display_settings::Values settings, float dpi) const
    { return ! image.isValid() || plot != lastPlot || std::abs (dpi - lastDpi) > 0 || revision != lastRevision || settings != lastSettings; }
    void prepare (juce::Rectangle<float> plot, float dpi)
    {
        const auto width = static_cast<int> (std::ceil (plot.getWidth() * dpi));
        const auto height = static_cast<int> (std::ceil (plot.getHeight() * dpi));
        if (! image.isValid() || image.getWidth() != width || image.getHeight() != height)
            image = juce::Image (juce::Image::ARGB, width, height, true);
        else
            image.clear (image.getBounds());
    }
    void remember (juce::Rectangle<float> plot, uint64_t revision, display_settings::Values settings, float dpi)
    { lastPlot = plot; lastDpi = dpi; lastRevision = revision; lastSettings = settings; ++builds; }
    juce::Image image;
    juce::Rectangle<float> lastPlot;
    float lastDpi = 0;
    uint64_t lastRevision = 0;
    display_settings::Values lastSettings;
};
}
