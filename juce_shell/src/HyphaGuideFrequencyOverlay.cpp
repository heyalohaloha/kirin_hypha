#include "HyphaGuideFrequencyOverlay.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hypha::guide_frequency
{
namespace
{
    bool validFrequencyRange (double lowHz, double highHz) noexcept
    {
        return std::isfinite (lowHz) && std::isfinite (highHz)
            && lowHz > 0.0 && highHz > lowHz && highHz <= 1'000'000.0;
    }

    bool sameDoubleBits (double left, double right) noexcept
    {
        return std::memcmp (&left, &right, sizeof (double)) == 0;
    }
}

Overlay fromGuidePresentation (
    const pre_display::GuidePresentationSnapshot& presentation)
{
    Overlay out;
    if (! presentation.guideAvailable
        || presentation.targetRole != pre_display::GuideTargetRole::post
        || ! presentation.hasPrimary || ! presentation.primary.hasBand
        || ! validFrequencyRange (
               presentation.primary.lowHz, presentation.primary.highHz))
    {
        return out;
    }

    if (presentation.primary.phase == pre_display::GuideFactPhase::active)
        out.emphasis = Emphasis::active;
    else if (presentation.primary.phase == pre_display::GuideFactPhase::cue)
        out.emphasis = Emphasis::cue;
    else
        return out;

    out.kind = presentation.payloadKind == "masking"
             ? FactKind::masking : FactKind::inspect;
    out.guideId = presentation.guideId;
    out.itemId = presentation.primary.itemId;
    out.label = presentation.primary.label;
    out.frequencyBasis = presentation.primary.frequencyBasis;
    out.lowHz = presentation.primary.lowHz;
    out.highHz = presentation.primary.highHz;
    return out;
}

bool equivalent (const Overlay& left, const Overlay& right) noexcept
{
    return left.emphasis == right.emphasis && left.kind == right.kind
        && left.guideId == right.guideId && left.itemId == right.itemId
        && left.label == right.label
        && left.frequencyBasis == right.frequencyBasis
        && sameDoubleBits (left.lowHz, right.lowHz)
        && sameDoubleBits (left.highHz, right.highHz);
}

juce::Rectangle<float> bandBoundsFor (
    const Overlay& overlay, float minimumHz, float maximumHz,
    juce::Rectangle<float> plot) noexcept
{
    if (! overlay.visible() || ! validFrequencyRange (overlay.lowHz, overlay.highHz)
        || ! std::isfinite (minimumHz) || ! std::isfinite (maximumHz)
        || minimumHz <= 0.0f || maximumHz <= minimumHz
        || overlay.highHz <= minimumHz || overlay.lowHz >= maximumHz
        || plot.isEmpty())
    {
        return {};
    }
    const auto clippedLow = juce::jmax ((double) minimumHz, overlay.lowHz);
    const auto clippedHigh = juce::jmin ((double) maximumHz, overlay.highHz);
    const float left = spectrum_geometry::xForFrequency (
        (float) clippedLow, minimumHz, maximumHz, plot);
    const float right = spectrum_geometry::xForFrequency (
        (float) clippedHigh, minimumHz, maximumHz, plot);
    return { left, plot.getY(), juce::jmax (0.0f, right - left), plot.getHeight() };
}

void paint (juce::Graphics& g, juce::Rectangle<float> plot,
            float visualScale, const Overlay& overlay,
            float minimumHz, float maximumHz)
{
    auto band = bandBoundsFor (overlay, minimumHz, maximumHz, plot);
    if (band.isEmpty())
        return;

    const auto strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    const auto glowScale = ui_contract::spectrumGlowScale (visualScale);
    const float minimumWidth = juce::jmax (1.0f, 1.5f * strokeScale);
    if (band.getWidth() < minimumWidth)
        band = band.withSizeKeepingCentre (minimumWidth, band.getHeight())
                   .getIntersection (plot);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.toNearestInt());
    if (overlay.emphasis == Emphasis::active)
    {
        juce::ColourGradient wash (
            COL_GUIDE.withAlpha (ui_contract::guideBandActiveTopAlpha),
            band.getCentreX(), band.getY(),
            COL_GUIDE.withAlpha (ui_contract::guideBandActiveBottomAlpha),
            band.getCentreX(), band.getBottom(), false);
        g.setGradientFill (wash);
        g.fillRect (band);

        for (int layer = 3; layer >= 1; --layer)
        {
            const float spread = (float) layer * 2.0f * glowScale;
            const float alpha = ui_contract::guideBandGlowAlpha / (float) layer;
            g.setColour (COL_GUIDE.withAlpha (alpha));
            g.drawVerticalLine (juce::roundToInt (band.getX() - spread),
                                band.getY(), band.getBottom());
            g.drawVerticalLine (juce::roundToInt (band.getRight() + spread),
                                band.getY(), band.getBottom());
        }
    }

    const bool masking = overlay.kind == FactKind::masking;
    const float bracketDepth = (masking ? 8.0f : 5.0f) * visualScale;
    const float alpha = overlay.emphasis == Emphasis::active
                      ? ui_contract::guideBandActiveStrokeAlpha
                      : ui_contract::guideBandCueStrokeAlpha;
    const float stroke = (masking ? 1.25f : 0.9f) * strokeScale;
    g.setColour ((overlay.emphasis == Emphasis::active ? COL_GUIDE_BR : COL_GUIDE)
                     .withAlpha (alpha));
    g.drawLine (band.getX(), band.getY(), band.getRight(), band.getY(), stroke);
    g.drawLine (band.getX(), band.getY(), band.getX(),
                band.getY() + bracketDepth, stroke);
    g.drawLine (band.getRight(), band.getY(), band.getRight(),
                band.getY() + bracketDepth, stroke);
    if (masking)
    {
        const float lowerY = band.getY() + 3.0f * visualScale;
        g.setColour (COL_GUIDE.withAlpha (alpha * 0.52f));
        g.drawLine (band.getX(), lowerY, band.getRight(), lowerY,
                    0.75f * strokeScale);
    }
}
}
