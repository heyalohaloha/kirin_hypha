#include "HyphaSpectrumPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hypha::spectrum_painter
{
namespace
{
    constexpr float kDeltaRangeDb = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;
    constexpr float kIntensityReferenceDb = 18.0f;
    constexpr float kMagnitudeFloorDbfs = -96.0f;

    float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept
    {
        const float clipped = juce::jlimit (-kDeltaRangeDb, kDeltaRangeDb, db);
        return juce::jmap (clipped, kDeltaRangeDb, -kDeltaRangeDb,
                           plot.getY(), plot.getBottom());
    }

    float yForMagnitudeDbfs (float dbfs, juce::Rectangle<float> plot) noexcept
    {
        const float clipped = juce::jlimit (kMagnitudeFloorDbfs, 0.0f, dbfs);
        return juce::jmap (clipped, 0.0f, kMagnitudeFloorDbfs,
                           plot.getY(), plot.getBottom());
    }

    juce::Path makeCurve (const SpectrumBins& x, const SpectrumBins& y)
    {
        juce::Path curve;
        curve.preallocateSpace (static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 3u));
        curve.startNewSubPath (x.front(), y.front());
        for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
            curve.lineTo (x[index], y[index]);
        return curve;
    }
}

void paintCurves (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  float visualScale,
                  const SpectrumBins& pre,
                  const SpectrumBins& post,
                  const SpectrumBins& delta,
                  const SpectrumBins* mark)
{
    const float strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    const float glowScale = ui_contract::spectrumGlowScale (visualScale);
    const auto scaledStroke = [strokeScale] (float value) { return value * strokeScale; };
    const auto scaledGlow = [glowScale] (float value) { return value * glowScale; };
    const float zeroY = yForDeltaDb (0.0f, plot);
    SpectrumBins x {};
    SpectrumBins preY {};
    SpectrumBins postY {};
    SpectrumBins deltaY {};
    SpectrumBins markY {};
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        x[index] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                               plot.getX(), plot.getRight());
        preY[index] = yForMagnitudeDbfs (pre[index], plot);
        postY[index] = yForMagnitudeDbfs (post[index], plot);
        deltaY[index] = yForDeltaDb (delta[index], plot);
        if (mark != nullptr)
            markY[index] = yForDeltaDb ((*mark)[index], plot);
    }

    const juce::Path preCurve = makeCurve (x, preY);
    const juce::Path postCurve = makeCurve (x, postY);
    const juce::Path deltaCurve = makeCurve (x, deltaY);
    const juce::Path markCurve = mark != nullptr ? makeCurve (x, markY) : juce::Path {};

    constexpr size_t intensityLevelCount = ui_contract::spectrumTipAlpha.size();
    // The wider ±24 dB geometry must not make ordinary 1–6 dB work look dimmer. Brightness keeps
    // the proven ±18 dB response and simply reaches its maximum before the new display edge.
    constexpr float intensityStepDb = kIntensityReferenceDb
                                    / (float) (intensityLevelCount - 1u);
    constexpr std::array<float, 6> tipDepthCoverage {
        1.00f, 0.79f, 0.60f, 0.43f, 0.28f, 0.14f
    };
    constexpr std::array<float, tipDepthCoverage.size()> tipAlphaShare {
        0.055f, 0.080f, 0.130f, 0.200f, 0.310f, 0.480f
    };
    std::array<std::array<juce::Path, intensityLevelCount>, tipDepthCoverage.size()>
        intensityTips;
    std::array<juce::Path, intensityLevelCount> highlights;
    const auto innerTipY = [&plot] (float db, float coverage) {
        const float magnitudeDb = std::abs (db);
        const float tipDepthDb = std::min (3.0f, magnitudeDb * 0.38f) * coverage;
        const float innerDb = std::copysign (magnitudeDb - tipDepthDb, db);
        return yForDeltaDb (innerDb, plot);
    };
    for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float magnitude = 0.5f * (std::abs (delta[index - 1])
                                      + std::abs (delta[index]));
        const size_t bucket = std::min (intensityLevelCount - 1u,
                                        static_cast<size_t> (magnitude / intensityStepDb));
        if (bucket > 0u)
        {
            for (size_t layer = 0; layer < tipDepthCoverage.size(); ++layer)
            {
                auto& tip = intensityTips[layer][bucket];
                tip.startNewSubPath (x[index - 1], deltaY[index - 1]);
                tip.lineTo (x[index], deltaY[index]);
                tip.lineTo (x[index], innerTipY (delta[index], tipDepthCoverage[layer]));
                tip.lineTo (x[index - 1], innerTipY (delta[index - 1],
                                                      tipDepthCoverage[layer]));
                tip.closeSubPath();
            }
        }
        highlights[bucket].startNewSubPath (x[index - 1], deltaY[index - 1]);
        highlights[bucket].lineTo (x[index], deltaY[index]);
    }

    g.setColour (COL_SPECTRUM_PRE.withAlpha (ui_contract::spectrumPreCurveAlpha));
    g.strokePath (preCurve,
                  juce::PathStrokeType (scaledStroke (ui_contract::spectrumPreStrokeWidth),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostGlowAlpha));
    g.strokePath (postCurve,
                  juce::PathStrokeType (scaledGlow (ui_contract::spectrumPostGlowStrokeWidth),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostCurveAlpha));
    g.strokePath (postCurve,
                  juce::PathStrokeType (scaledStroke (ui_contract::spectrumPostStrokeWidth),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    juce::Path deltaFill;
    deltaFill.setUsingNonZeroWinding (false);
    deltaFill.startNewSubPath (x.front(), zeroY);
    deltaFill.lineTo (x.front(), deltaY.front());
    for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        deltaFill.lineTo (x[index], deltaY[index]);
    deltaFill.lineTo (x.back(), zeroY);
    deltaFill.closeSubPath();
    juce::ColourGradient fillGradient (COL_SPECTRUM_DELTA.withAlpha (0.34f),
                                       plot.getX(), plot.getY(),
                                       COL_SPECTRUM_DELTA.withAlpha (0.34f),
                                       plot.getX(), plot.getBottom(), false);
    fillGradient.addColour (0.5, COL_SPECTRUM_DELTA.withAlpha (0.05f));
    g.setGradientFill (fillGradient);
    g.fillPath (deltaFill);

    // A fact-derived tip ribbon adds density beside the Δ edge, never across the whole body.
    // It has no hold state or animation: every filled segment belongs to this exact snapshot.
    for (size_t layer = 0; layer < intensityTips.size(); ++layer)
    {
        for (size_t bucket = 1; bucket < intensityTips[layer].size(); ++bucket)
        {
            g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (
                ui_contract::spectrumTipAlpha[bucket] * tipAlphaShare[layer]));
            if (! intensityTips[layer][bucket].isEmpty())
                g.fillPath (intensityTips[layer][bucket]);
        }
    }

    // MARK is a presentation-only frozen Δ. Amber separates the chosen moment from the cyan live
    // fact, while its narrower stroke and lack of fill/glow keep the live Δ visually primary.
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

    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.21f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, scaledGlow (3.2f));
    g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.76f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, scaledStroke (1.0f));

    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.17f));
    g.strokePath (deltaCurve, juce::PathStrokeType (scaledGlow (4.6f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.94f));
    g.strokePath (deltaCurve, juce::PathStrokeType (scaledStroke (2.15f),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    constexpr std::array<float, intensityLevelCount> highlightAlpha {
        0.10f, 0.13f, 0.16f, 0.19f, 0.22f, 0.255f, 0.29f,
        0.325f, 0.36f, 0.40f, 0.44f, 0.48f, 0.52f,
        0.56f, 0.60f, 0.64f, 0.68f, 0.715f, 0.75f,
        0.785f, 0.82f, 0.855f, 0.89f, 0.915f, 0.94f
    };
    for (size_t bucket = 0; bucket < highlights.size(); ++bucket)
    {
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (highlightAlpha[bucket]));
        if (! highlights[bucket].isEmpty())
            g.strokePath (highlights[bucket],
                          juce::PathStrokeType (scaledStroke (1.15f),
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}
}
