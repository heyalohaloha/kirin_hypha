#include "HyphaGuideFrequencyOverlay.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

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

    Emphasis emphasisFor (pre_display::GuideFactPhase phase) noexcept
    {
        if (phase == pre_display::GuideFactPhase::active)
            return Emphasis::active;
        if (phase == pre_display::GuideFactPhase::cue)
            return Emphasis::cue;
        return Emphasis::hidden;
    }

    void appendBand (Overlay& overlay,
                     const pre_display::GuidePresentationFact& fact,
                     BandRole role)
    {
        const auto emphasis = emphasisFor (fact.phase);
        if (overlay.count >= overlay.bands.size()
            || emphasis == Emphasis::hidden || ! fact.hasBand
            || ! validFrequencyRange (fact.lowHz, fact.highHz))
            return;
        auto& band = overlay.bands[overlay.count++];
        band.emphasis = emphasis;
        band.role = role;
        band.itemId = fact.itemId;
        band.label = fact.label;
        band.frequencyBasis = fact.frequencyBasis;
        band.lowHz = fact.lowHz;
        band.highHz = fact.highHz;
    }

    bool equivalent (const Band& left, const Band& right) noexcept
    {
        return left.emphasis == right.emphasis && left.role == right.role
            && left.itemId == right.itemId && left.label == right.label
            && left.frequencyBasis == right.frequencyBasis
            && sameDoubleBits (left.lowHz, right.lowHz)
            && sameDoubleBits (left.highHz, right.highHz);
    }

    void paintFocusOutline (juce::Graphics& g, juce::Rectangle<float> band,
                            float visualScale, float strokeScale, float alpha)
    {
        const float stroke = 0.8f * strokeScale;
        const float inset = juce::jmax (1.0f, visualScale);
        g.setColour (COL_GUIDE.withAlpha (alpha));
        g.drawRect (band, stroke);
        if (band.getWidth() > 2.0f * inset && band.getHeight() > 2.0f * inset)
            g.drawRect (band.reduced (inset, inset), 0.55f * strokeScale);
    }

    void paintMeasuredBand (juce::Graphics& g, juce::Rectangle<float> band,
                            float visualScale, float strokeScale, float glowScale,
                            Emphasis emphasis)
    {
        if (emphasis == Emphasis::active)
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
        const float alpha = emphasis == Emphasis::active
                          ? ui_contract::guideBandActiveStrokeAlpha
                          : ui_contract::guideBandCueStrokeAlpha;
        const float depth = 7.0f * visualScale;
        const float stroke = 1.2f * strokeScale;
        g.setColour ((emphasis == Emphasis::active ? COL_GUIDE_BR : COL_GUIDE)
                         .withAlpha (alpha));
        g.drawLine (band.getX(), band.getY(), band.getRight(), band.getY(), stroke);
        g.drawLine (band.getX(), band.getY(), band.getX(), band.getY() + depth, stroke);
        g.drawLine (band.getRight(), band.getY(), band.getRight(),
                    band.getY() + depth, stroke);
    }
}

Overlay fromGuidePresentation (
    const pre_display::GuidePresentationSnapshot& presentation)
{
    Overlay out;
    if (! presentation.guideAvailable
        || presentation.targetRole != pre_display::GuideTargetRole::post)
        return out;

    out.guideId = presentation.guideId;
    if (presentation.payloadKind == "masking")
    {
        if (presentation.hasMaskingFocus)
            appendBand (out, presentation.maskingFocus, BandRole::maskingFocus);
        if (presentation.hasPrimary
            && presentation.primary.kind
                == pre_display::GuidePresentationFactKind::maskingMeasuredInterval)
            appendBand (out, presentation.primary, BandRole::maskingMeasured);
    }
    else if (presentation.hasPrimary
             && presentation.primary.kind
                 == pre_display::GuidePresentationFactKind::inspectEvent)
    {
        appendBand (out, presentation.primary, BandRole::inspect);
    }
    return out;
}

bool equivalent (const Overlay& left, const Overlay& right) noexcept
{
    if (left.guideId != right.guideId || left.count != right.count)
        return false;
    for (std::size_t index = 0; index < left.count; ++index)
        if (! equivalent (left.bands[index], right.bands[index]))
            return false;
    return true;
}

juce::Rectangle<float> bandBoundsFor (
    const Band& band, float minimumHz, float maximumHz,
    juce::Rectangle<float> plot) noexcept
{
    if (! band.visible() || ! validFrequencyRange (band.lowHz, band.highHz)
        || ! std::isfinite (minimumHz) || ! std::isfinite (maximumHz)
        || minimumHz <= 0.0f || maximumHz <= minimumHz
        || band.highHz <= minimumHz || band.lowHz >= maximumHz || plot.isEmpty())
        return {};
    const auto clippedLow = juce::jmax ((double) minimumHz, band.lowHz);
    const auto clippedHigh = juce::jmin ((double) maximumHz, band.highHz);
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
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.toNearestInt());
    const auto strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    const auto glowScale = ui_contract::spectrumGlowScale (visualScale);
    for (std::size_t index = 0; index < overlay.count; ++index)
    {
        const auto& source = overlay.bands[index];
        auto bounds = bandBoundsFor (source, minimumHz, maximumHz, plot);
        if (bounds.isEmpty())
            continue;
        const float minimumWidth = juce::jmax (1.0f, 1.5f * strokeScale);
        if (bounds.getWidth() < minimumWidth)
            bounds = bounds.withSizeKeepingCentre (minimumWidth, bounds.getHeight())
                           .getIntersection (plot);
        if (source.role == BandRole::maskingFocus)
        {
            const float alpha = source.emphasis == Emphasis::active
                              ? ui_contract::guideBandActiveStrokeAlpha * 0.62f
                              : ui_contract::guideBandCueStrokeAlpha * 0.62f;
            paintFocusOutline (g, bounds, visualScale, strokeScale, alpha);
        }
        else
            paintMeasuredBand (g, bounds, visualScale, strokeScale,
                               glowScale, source.emphasis);
    }
}
}
