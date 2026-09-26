#include "HyphaAttackPainter.h"
#include "HyphaAttackDepth.h"
#include "HyphaAttackEnvelopeGeometry.h"
#include "HyphaTheme.h"

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
    const auto& depth = attack_depth::look();
    const auto shape = attack_envelope::geometry (batch, area.toFloat(), first, latest, rate,
                                                  .05f / juce::jmax (1.0f, dpi));
    const bool reference = style == WaveformStyle::trace;
    const auto colour = reference ? juce::Colour (attack_ui::preTraceColour) : juce::Colour (attack_ui::waveformColour);
    const auto plot = area.toFloat();
    // Measured light fades with age toward the left end of the plot, the oldest time.
    const auto stroke = [&] (juce::Graphics& target, const juce::Path& path, juce::Colour ink, float a,
                             const juce::PathStrokeType& type, juce::AffineTransform shift = {}) {
        if (path.isEmpty() || a <= 0.0f) return;
        target.setGradientFill (attack_depth::ageBrush (ink, a, plot));
        target.strokePath (path, type, shift);
    };
    const auto paint = [&] (juce::Graphics& target) {
    juce::Graphics::ScopedSaveState saved (target); target.reduceClipRegion (area);
    if (! reference)
    {
        // Light falls from the measured edge toward the floor of the plot, and older light is
        // fainter: the body darkens by what age has taken from it.
        juce::ColourGradient body (colour.withAlpha (alpha*.42f), plot.getTopLeft(),
            colour.withAlpha (alpha*.03f), plot.getBottomLeft(), false);
        body.addColour (.35, colour.withAlpha (alpha*.16f));
        target.setGradientFill (body); target.fillPath (shape.body);
        target.setGradientFill (attack_depth::ageShade (BG.darker (.82f), plot));
        target.fillPath (shape.body);
        // A glass-tube wall straddles the edge (inside: the wall, outside: its glow), and the key
        // light catches just inside it.
        stroke (target, shape.edge, colour, alpha * (depth.fresnel + .03f * depth.bloom),
                juce::PathStrokeType (5.5f, juce::PathStrokeType::beveled));
        // The highlight stays cyan so the only pale neutral line is the PRE reference.
        stroke (target, shape.edge, colour.interpolatedWith (COL_NORMAL, .25f), alpha * depth.specular,
                juce::PathStrokeType (1.1f), juce::AffineTransform::translation (0.0f, 1.6f));
    }
    // PRE stays a plain reference trace; the measured POST edge is the brightest line.
    stroke (target, shape.edge, colour, alpha * (reference ? .80f : .85f),
            juce::PathStrokeType (reference ? 1.0f : .8f, juce::PathStrokeType::beveled));
    if (! reference)
        attack_depth::paintGlints (target, shape.edge, plot.getY() + plot.getHeight() * .30f,
                                   colour, alpha * depth.glint, plot);
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
}
