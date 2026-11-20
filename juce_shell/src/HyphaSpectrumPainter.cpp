#include "HyphaSpectrumPainter.h"
#include "HyphaSpectrumGlowLayers.h"
#include "HyphaSpectrumPhaseProfile.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumPostBody.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaPolylineGeometry.h"
#include "HyphaMonotoneCurve.h"
#include "HyphaTimeFieldImage.h"
#include "HyphaSpectrumTerrain.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace hypha::spectrum_painter
{
namespace
{
    constexpr float kDeltaRangeDb = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;
    constexpr float kIntensityReferenceDb = 18.0f;
    constexpr size_t kIntensityLevelCount = ui_contract::spectrumTipAlpha.size();
    constexpr float kIntensityStepDb = kIntensityReferenceDb
                                     / (float) (kIntensityLevelCount - 1u);
    constexpr float kMagnitudeFloorDbfs = -96.0f;

    float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept
    {
        const float clipped = juce::jlimit (-kDeltaRangeDb, kDeltaRangeDb, db);
        return juce::jmap (clipped, kDeltaRangeDb, -kDeltaRangeDb,
                           plot.getY(), plot.getBottom());
    }

    // Brightness for the six-second field. The band level keeps its full -96..0 dBFS range, but a
    // straight line over 96 dB puts ordinary music (about -60..-10 dBFS) inside a quarter of the
    // available ink, which reads as one flat haze. The exponent spends the range where the music
    // is. -6 dBFS against -60 dBFS now differs by about 4.3x of ink instead of 2x, while -60 dBFS
    // stays legible and nothing above the floor is cut.
    constexpr float kFieldAlphaCeiling = 0.52f;
    constexpr float kFieldGamma = 1.6f;
    // 0.25 dB per entry over the whole -96..0 dBFS range. The exponent is evaluated 385 times at
    // startup instead of once per pixel per frame; a per-pixel pow costs more than the blit.
    constexpr int kFieldStepsPerDb = 4;
    constexpr size_t kFieldTableSize = (size_t) (-kMagnitudeFloorDbfs * kFieldStepsPerDb) + 1u;

    const std::array<uint8_t, kFieldTableSize>& fieldAlphaTable()
    {
        static const auto table = [] {
            std::array<uint8_t, kFieldTableSize> built {};
            for (size_t entry = 0u; entry < built.size(); ++entry)
            {
                const float unit = (float) entry / (float) (built.size() - 1u);
                built[entry] = (uint8_t) juce::jlimit (0, 255, (int) std::lround (
                    kFieldAlphaCeiling * std::pow (unit, kFieldGamma) * 255.0f));
            }
            return built;
        }();
        return table;
    }

    uint8_t fieldAlphaStepFor (float dbfs, const std::array<uint8_t, kFieldTableSize>& table) noexcept
    {
        if (! std::isfinite (dbfs))
            return 0u;
        const int entry = (int) std::lround ((dbfs - kMagnitudeFloorDbfs)
                                             * (float) kFieldStepsPerDb);
        return table[(size_t) juce::jlimit (0, (int) table.size() - 1, entry)];
    }

    float yForMagnitudeDbfs (float dbfs, juce::Rectangle<float> plot, float range) noexcept
    {
        const float clipped = juce::jlimit (-range, 0.0f, dbfs);
        return juce::jmap (clipped, 0.0f, -range,
                           plot.getY(), plot.getBottom());
    }

    void buildCurve (juce::Path& path, const SpectrumBins& x, const SpectrumBins& y,
                     const SpectrumValidity* valid = nullptr)
    {
        const auto known = [&] (size_t i) { return (! valid || (*valid)[i]) && std::isfinite (y[i]); };
        for (size_t first = 0; first < x.size();)
        {
            if (! known (first)) { ++first; continue; }
            auto last = first;
            while (last + 1 < x.size() && known (last + 1)) ++last;
            monotone_curve::append (path, x, y, first, last, true, true);
            first = last + 1;
        }
    }
    void strokePostCurve (juce::Graphics& g, const juce::Path& curve, float scale, float brightness)
    {
        g.setColour (COL_SPECTRUM_POST.withMultipliedBrightness (brightness).withAlpha (ui_contract::spectrumPostGlowAlpha));
        g.strokePath (curve, juce::PathStrokeType (
            ui_contract::spectrumPostGlowStrokeWidth * ui_contract::spectrumGlowScale (scale),
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (COL_SPECTRUM_POST.withMultipliedBrightness (brightness).withAlpha (ui_contract::spectrumPostCurveAlpha));
        g.strokePath (curve, juce::PathStrokeType (
            ui_contract::spectrumPostStrokeWidth * ui_contract::spectrumStrokeScale (scale),
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

float yForMagnitude (float dbfs, juce::Rectangle<float> plot, float range) noexcept
{
    return yForMagnitudeDbfs (dbfs, plot, range);
}

void paintPostCurve (juce::Graphics& g, juce::Rectangle<float> plot,
                     float visualScale, const SpectrumBins& post, float range, float brightness,
                     PathWorkspace* workspace)
{
    std::optional<PathWorkspace> localWorkspace;
    if (workspace == nullptr) localWorkspace.emplace();
    auto& paths = workspace != nullptr ? *workspace : *localWorkspace;
    paths.beginFrame();
    SpectrumBins x {}, y {};
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        x[index] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                              plot.getX(), plot.getRight());
        y[index] = yForMagnitudeDbfs (post[index], plot, range);
    }
    auto& curve = paths.path (0, static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 7u));
    buildCurve (curve, x, y);
    strokePostCurve (g, curve, visualScale, brightness);
}

void paintCurves (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  float visualScale,
                  const SpectrumBins& pre,
                  const SpectrumBins& post,
                  const SpectrumBins& delta,
                  const SpectrumValidity& deltaValid,
                  const SpectrumBins* mark,
                  const SpectrumValidity* markValid, float range, float brightness, bool richPost,
                  PathWorkspace* workspace)
{
    KIRIN_FREQ_SCOPE(profileGeometry, curve_geometry_paths);
    std::optional<PathWorkspace> localWorkspace;
    if (workspace == nullptr) localWorkspace.emplace();
    auto& paths = workspace != nullptr ? *workspace : *localWorkspace;
    paths.beginFrame();
    const float strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    const float glowScale = ui_contract::spectrumGlowScale (visualScale);
    const auto scaledStroke = [strokeScale] (float value) { return value * strokeScale; };
    const auto scaledGlow = [glowScale] (float value) { return value * glowScale; };
    const float zeroY = yForDeltaDb (0.0f, plot);
    SpectrumBins x {}, preY {}, postY {}, deltaY {}, markY {};
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        x[index] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                               plot.getX(), plot.getRight());
        preY[index] = yForMagnitudeDbfs (pre[index], plot, range);
        postY[index] = yForMagnitudeDbfs (post[index], plot, range);
        deltaY[index] = yForDeltaDb (delta[index], plot);
        if (mark != nullptr)
            markY[index] = yForDeltaDb ((*mark)[index], plot);
    }

    constexpr int curveCapacity = static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 7u);
    auto& preCurve = paths.path (0, curveCapacity);
    auto& postCurve = paths.path (1, curveCapacity);
    auto& deltaCurve = paths.path (2, curveCapacity);
    buildCurve (preCurve, x, preY); buildCurve (postCurve, x, postY);
    buildCurve (deltaCurve, x, deltaY, &deltaValid);
    if (mark != nullptr) buildCurve (paths.path (3, curveCapacity), x, markY, markValid);
    const auto& markCurve = paths.existing (3);

    // The wider ±24 dB geometry must not make ordinary 1–6 dB work look dimmer. Brightness keeps
    // the proven ±18 dB response and simply reaches its maximum before the new display edge.
    constexpr std::array<float, 6> tipDepthCoverage {
        1.00f, 0.79f, 0.60f, 0.43f, 0.28f, 0.14f
    };
    constexpr std::array<float, tipDepthCoverage.size()> tipAlphaShare {
        0.055f, 0.080f, 0.130f, 0.200f, 0.310f, 0.480f
    };
    static_assert (PathWorkspace::layers == tipDepthCoverage.size());
    static_assert (PathWorkspace::levels == kIntensityLevelCount);
    // Every segment can be its own run: at most 17 coordinates per band.
    constexpr int ribbonCapacity = static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 18u);
    constexpr int highlightCapacity = static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 10u);
    const auto innerTipY = [&plot] (float db, float coverage) {
        const float magnitudeDb = std::abs (db);
        const float tipDepthDb = std::min (3.0f, magnitudeDb * 0.38f) * coverage;
        const float innerDb = std::copysign (magnitudeDb - tipDepthDb, db);
        return yForDeltaDb (innerDb, plot);
    };
    const auto bucketForSegment = [&delta] (size_t index) {
        const float magnitude = 0.5f * (std::abs (delta[index - 1]) + std::abs (delta[index]));
        return std::min (kIntensityLevelCount - 1u,
                         static_cast<size_t> (magnitude / kIntensityStepDb));
    };
    // Consecutive segments of one colour form one ribbon and one highlight. Retained vertices
    // bound the error to 0.05 logical pixels; shared edges and internal rounded caps disappear.
    // This bounds stroke tessellation by colour runs instead of by individual FFT bands.
    for (size_t first = 1; first < KIRIN_SPECTRUM_BAND_COUNT;)
    {
        if (deltaValid[first - 1] == 0u || deltaValid[first] == 0u)
        {
            ++first;
            continue;
        }
        const size_t bucket = bucketForSegment (first);
        size_t last = first;
        while (last + 1 < KIRIN_SPECTRUM_BAND_COUNT
               && deltaValid[last + 1] != 0u
               && bucketForSegment (last + 1) == bucket)
            ++last;
        if (bucket > 0u)
        {
            for (const auto layer : glowLayers)
            {
                auto& tip = paths.path (PathWorkspace::tipBase + layer * kIntensityLevelCount + bucket,
                                        ribbonCapacity);
                tip.startNewSubPath (x[first - 1], deltaY[first - 1]);
                monotone_curve::append (tip, x, deltaY, first - 1, last, false, true);
                for (size_t index = last + 1; index-- > first - 1;)
                    tip.lineTo (x[index], innerTipY (delta[index], tipDepthCoverage[layer]));
                tip.closeSubPath();
            }
        }
        auto& highlight = paths.path (PathWorkspace::highlightBase + bucket, highlightCapacity);
        highlight.startNewSubPath (x[first - 1], deltaY[first - 1]);
        monotone_curve::append (highlight, x, deltaY, first - 1, last, false, true);
        first = last + 1;
    }

    KIRIN_FREQ_FINISH(profileGeometry);
    KIRIN_FREQ_SCOPE(profilePrePost, pre_post_stroke_and_rich_fill);
    g.setColour (COL_SPECTRUM_PRE.withMultipliedBrightness (brightness).withAlpha (ui_contract::spectrumPreCurveAlpha));
    g.strokePath (preCurve,
                  juce::PathStrokeType (scaledStroke (ui_contract::spectrumPreStrokeWidth),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    if (richPost) post_body::fill (g, plot, x, postY, brightness, &paths.path (5, ribbonCapacity));
    strokePostCurve (g, postCurve, visualScale, brightness);
    if (richPost) post_body::edge (g, postCurve, strokeScale, brightness);

    KIRIN_FREQ_FINISH(profilePrePost);
    KIRIN_FREQ_SCOPE(profileDeltaGeometry, delta_fill_geometry);
    auto& deltaFill = paths.path (4, ribbonCapacity);
    deltaFill.setUsingNonZeroWinding (false);
    for (size_t first = 0; first < KIRIN_SPECTRUM_BAND_COUNT;)
    {
        while (first < KIRIN_SPECTRUM_BAND_COUNT && deltaValid[first] == 0u) ++first;
        if (first >= KIRIN_SPECTRUM_BAND_COUNT) break;
        size_t last = first;
        while (last + 1 < KIRIN_SPECTRUM_BAND_COUNT && deltaValid[last + 1] != 0u)
            ++last;
        if (last > first)
        {
            deltaFill.startNewSubPath (x[first], zeroY);
            deltaFill.lineTo (x[first], deltaY[first]);
            monotone_curve::append (deltaFill, x, deltaY, first, last, false);
            deltaFill.lineTo (x[last], zeroY);
            deltaFill.closeSubPath();
        }
        first = last + 1;
    }
    KIRIN_FREQ_FINISH(profileDeltaGeometry);
    KIRIN_FREQ_SCOPE(profileDeltaPaint, delta_fill_stroke_glow);
    KIRIN_FREQ_SCOPE(profileGradientBody, delta_gradient_body);
    juce::ColourGradient fillGradient (COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.34f),
                                       plot.getX(), plot.getY(),
                                       COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.34f),
                                       plot.getX(), plot.getBottom(), false);
    fillGradient.addColour (0.5, COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.05f));
    g.setGradientFill (fillGradient);
    g.fillPath (deltaFill);

    KIRIN_FREQ_FINISH(profileGradientBody);
    KIRIN_FREQ_SCOPE(profileTipRibbons, delta_tip_ribbons);
    // A measurement-derived tip ribbon adds density beside the live presentation edge, never
    // across the whole body. Numeric readout, MARK and Focus Trail retain their unsmoothed facts.
    for (const auto layer : glowLayers)
    {
        for (size_t bucket = 1; bucket < kIntensityLevelCount; ++bucket)
        {
            const auto& tip = paths.existing (PathWorkspace::tipBase + layer * kIntensityLevelCount + bucket);
            g.setColour (COL_SPECTRUM_DELTA_BR.withMultipliedBrightness (brightness).withAlpha (
                ui_contract::spectrumTipAlpha[bucket] * tipAlphaShare[layer]));
            if (! tip.isEmpty())
                g.fillPath (tip);
        }
    }

    KIRIN_FREQ_FINISH(profileTipRibbons);
    KIRIN_FREQ_SCOPE(profileMarkZero, delta_mark_and_zero_lines);
    // MARK is a presentation-only frozen Δ. Amber separates the chosen moment from the cyan live
    // presentation, while its narrower stroke and lack of fill/glow keep live Δ visually primary.
    if (! markCurve.isEmpty())
    {
        g.setColour (COL_FLORA.withAlpha (
            ui_contract::spectrumMarkCurveAlpha));
        g.strokePath (markCurve,
                      juce::PathStrokeType (
                          scaledStroke (ui_contract::spectrumMarkStrokeWidth),
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
    }

    g.setColour (COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.21f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, scaledGlow (3.2f));
    g.setColour (COL_SPECTRUM_DELTA_BR.withMultipliedBrightness (brightness).withAlpha (0.76f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, scaledStroke (1.0f));

    KIRIN_FREQ_FINISH(profileMarkZero);
    KIRIN_FREQ_SCOPE(profilePrimaryStrokes, delta_primary_strokes);
    g.setColour (COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.17f));
    g.strokePath (deltaCurve, juce::PathStrokeType (scaledGlow (4.6f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_DELTA.withMultipliedBrightness (brightness).withAlpha (0.94f));
    g.strokePath (deltaCurve, juce::PathStrokeType (scaledStroke (2.15f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    KIRIN_FREQ_FINISH(profilePrimaryStrokes);
    KIRIN_FREQ_SCOPE(profileHighlights, delta_bucket_highlights);
    constexpr std::array<float, kIntensityLevelCount> highlightAlpha {
        0.10f, 0.13f, 0.16f, 0.19f, 0.22f, 0.255f, 0.29f,
        0.325f, 0.36f, 0.40f, 0.44f, 0.48f, 0.52f,
        0.56f, 0.60f, 0.64f, 0.68f, 0.715f, 0.75f,
        0.785f, 0.82f, 0.855f, 0.89f, 0.915f, 0.94f
    };
    for (size_t bucket = 0; bucket < kIntensityLevelCount; ++bucket)
    {
        const auto& highlight = paths.existing (PathWorkspace::highlightBase + bucket);
        g.setColour (COL_SPECTRUM_DELTA_BR.withMultipliedBrightness (brightness).withAlpha (highlightAlpha[bucket]));
        if (! highlight.isEmpty())
            g.strokePath (highlight,
                          juce::PathStrokeType (scaledStroke (1.15f),
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}

void paintAbsoluteHistory (juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           const absolute_spectrum::History& history, spectrum_display::Backdrop* cache,
                           display_settings::Values settings, float minimumHz, float maximumHz)
{
    KIRIN_FREQ_SCOPE(profileHistory, history_total);
    if (plot.isEmpty()) return;
    if (cache != nullptr && cache->enabled && history.size() >= 2 && plot.getWidth() >= spectrum_terrain::minimumPlotWidth)
    {
        cache->paintRaster (g, plot, history.revision(), settings, [&] (juce::Image& image, float dpi)
        { KIRIN_FREQ_SCOPE(profileRaster, history_raster_render_callback); spectrum_terrain::renderLevelLandscape (image, dpi, plot, history, settings, minimumHz, maximumHz); });
        return;
    }
    if (cache != nullptr)
    {
        cache->paint (g, plot, history.revision(), settings, [&] (juce::Graphics& target)
        { paintAbsoluteHistory (target, plot, history, nullptr, settings, minimumHz, maximumHz); });
        return;
    }
    const auto brightness = settings.brightness();
    if (! history.empty() && ! spectrum_terrain::paintLevelLandscape (g, plot, history, settings, minimumHz, maximumHz))
    {
        // One blit at the measured resolution: a column per band, a row per 1/180 of the six
        // seconds. The 64 x 40 cell grid this replaced kept one frame per row, so 140 of 180
        // observations never reached the screen and the survivors stepped a whole cell every
        // 150 ms. hypha::time_field owns the row rule, so SPACE draws MONO by the same one.
        const auto& newest = history.at (history.size() - 1u);
        const auto& alphaTable = fieldAlphaTable();
        const auto field = time_field::build (
            { (int) absolute_spectrum::historyCapacity, (int) KIRIN_SPECTRUM_BAND_COUNT,
              absolute_spectrum::historySeconds },
            history.size(),
            [&history, &newest] (size_t index) {
                const auto& frame = history.at (index);
                return frame.sampleRate > 0u
                    ? (double) (newest.endpoint - frame.endpoint) / (double) frame.sampleRate
                    : absolute_spectrum::historySeconds;
            },
            [&history] (size_t index, int column) {
                return history.at (index).postDbfs[(size_t) column];
            },
            COL_SPECTRUM_POST.withMultipliedBrightness (brightness),
            [&alphaTable] (float dbfs) { return fieldAlphaStepFor (dbfs, alphaTable); });
        time_field::draw (g, field, plot);
    }
}

void paintAbsolute (juce::Graphics& g,
                    juce::Rectangle<float> plot,
                    float visualScale,
                    const SpectrumBins& post,
                    const SpectrumBins& peakHold,
                    const absolute_spectrum::History& history,
                    presentation::Context presentation,
                    bool showHistory, display_settings::Values settings, spectrum_display::Backdrop* cache,
                    float minimumHz, float maximumHz, PathWorkspace* workspace)
{
    std::optional<PathWorkspace> localWorkspace;
    if (workspace == nullptr) localWorkspace.emplace();
    auto& paths = workspace != nullptr ? *workspace : *localWorkspace;
    paths.beginFrame();
    const auto range = settings.rangeDb(), brightness = settings.brightness();
    if (showHistory) paintAbsoluteHistory (g, plot, history, cache, settings, minimumHz, maximumHz);

    SpectrumBins x {};
    SpectrumBins currentY {};
    SpectrumBins holdY {};
    for (size_t index = 0u; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        x[index] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                               plot.getX(), plot.getRight());
        currentY[index] = yForMagnitudeDbfs (post[index], plot, range);
        holdY[index] = yForMagnitudeDbfs (std::isfinite (peakHold[index])
                                              ? peakHold[index] : kMagnitudeFloorDbfs,
                                          plot, range);
    }
    constexpr int capacity = static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 8u + 12u);
    auto& current = paths.path (0, capacity);
    auto& hold = paths.path (1, capacity);
    buildCurve (current, x, currentY); buildCurve (hold, x, holdY);
    if (showHistory)
    {
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_NORMAL.withAlpha (0.82f));
        text_style::drawText (g, "-6s", juce::Rectangle<float> { plot.getRight() - 36.0f, plot.getY(),
                                                    36.0f, 14.0f }.toNearestInt(),
                    juce::Justification::centredRight);
        text_style::drawText (g, "NOW", plot.withLeft (plot.getRight() - 36).withTop (plot.getBottom() - 14).toNearestInt(),
                    juce::Justification::centredRight);
    }

    auto& fill = paths.path (2, capacity);
    fill.startNewSubPath (x.front(), plot.getBottom());
    fill.lineTo (x.front(), currentY.front());
    monotone_curve::append (fill, x, currentY, 0, KIRIN_SPECTRUM_BAND_COUNT - 1, false, true);
    fill.lineTo (x.back(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (post_body::gradient (plot, brightness, settings.richFill()));
    g.fillPath (fill);

    const float strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    g.setColour (COL_SPECTRUM_POST.withMultipliedBrightness (brightness).withAlpha (0.18f));
    g.strokePath (current, juce::PathStrokeType (4.2f * strokeScale,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_POST.withMultipliedBrightness (brightness).withAlpha (0.98f));
    g.strokePath (current, juce::PathStrokeType (1.8f * strokeScale,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    if (settings.richFill()) post_body::edge (g, current, strokeScale, brightness);
    if (settings[display_settings::peak] != 1)
    {
    g.setColour (COL_FLORA_BR.withMultipliedBrightness (brightness).withAlpha (ui_contract::spectrumHoldAlpha));
    g.strokePath (hold, juce::PathStrokeType (0.85f * strokeScale,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}
}
