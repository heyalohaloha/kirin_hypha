#include "HyphaAttackPainter.h"
#include "HyphaAttackOrganismPainter.h"
#include "HyphaAttackEnvelopeGeometry.h"

namespace hypha::attack_painter
{
void drawEnvelope (juce::Graphics& g, const KirinAttackWaveformBatch& batch,
                   juce::Rectangle<int> area,
                   std::int64_t first, std::int64_t latest, std::uint32_t rate,
                   WaveformStyle style, float alpha)
{
    // The upper lane contains only the measured 10 ms RMS envelope on its actual sample axis.
    // Detail values cannot alter its shape, colour or apparent duration.
    const auto dpi = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto shape = attack_envelope::geometry (batch, area.toFloat(), first, latest, rate,
                                                  .05f / juce::jmax (1.0f, dpi));
    const bool reference = style == WaveformStyle::trace;
    const auto colour = reference ? juce::Colour (0xffa3b3b9) : juce::Colour (attack_ui::waveformColour);
    const auto paint = [&] (juce::Graphics& target) {
    juce::Graphics::ScopedSaveState saved (target); target.reduceClipRegion (area);
    if (! reference)
    {
        juce::ColourGradient gradient (colour.withAlpha (alpha*.55f), area.toFloat().getTopLeft(),
            colour.withAlpha (alpha*.55f), area.toFloat().getBottomLeft(), false);
        gradient.addColour (.5, colour.withAlpha (alpha*.10f));
        target.setGradientFill (gradient); target.fillPath (shape.body);
    }
    target.setColour (colour.withAlpha (alpha * (reference ? .45f : .70f)));
    target.strokePath (shape.edge, juce::PathStrokeType (.65f, juce::PathStrokeType::beveled));
    };
   #if JUCE_MAC
    // CoreGraphics uses an intermediate software raster for this many-segment envelope.
    // Other backends already rasterize spans directly and must not pay for another full image.
    // Check scale and dimensions before converting to integer pixel extents.
    if (std::isfinite (dpi) && dpi > 0 && dpi <= 4
        && area.getWidth() > 0 && area.getWidth() <= 4096
        && area.getHeight() > 0 && area.getHeight() <= 2048)
    {
        const auto pw = static_cast<int> (std::ceil (area.getWidth() * dpi));
        const auto ph = static_cast<int> (std::ceil (area.getHeight() * dpi));
        if (static_cast<std::uint64_t> (pw) * static_cast<std::uint64_t> (ph) > 2*1024*1024)
        { paint (g); return; }
        juce::Image raster (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType {});
        if (raster.isValid())
        {
            { juce::Graphics pixels (raster);
              pixels.addTransform (juce::AffineTransform::translation (
                  -static_cast<float> (area.getX()), -static_cast<float> (area.getY())).scaled (dpi));
              paint (pixels); }
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (area);
            g.setOpacity (1);
            // Already rasterized at device resolution, including path antialiasing and gradients.
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (raster, {static_cast<float> (area.getX()), static_cast<float> (area.getY()),
                                  static_cast<float> (pw)/dpi, static_cast<float> (ph)/dpi});
            return;
        }
    }
   #endif
    paint (g);
}
void drawEventFocus (juce::Graphics& g, const KirinAttackDetail* pre,
                     const KirinAttackDetail* post, juce::Rectangle<int> area,
                     const attack_motion::Motion& motion, attack_focus::Cache* cache)
{
    attack_organism::drawFocus (g, pre, post, area, motion, cache);
}
}
