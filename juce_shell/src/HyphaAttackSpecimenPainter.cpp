#include "HyphaAttackSpecimenPainter.h"

#include <cmath>

#include <BinaryData.h>

namespace hypha::attack_specimen
{
namespace
{
float unit (float value) noexcept
{
    return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.0f;
}

float stableAmount (float value) noexcept
{
    constexpr float steps = 48.0f;
    return std::round (unit (value) * steps) / steps;
}

struct EmissionLayers
{
    juce::Image base;
    juce::Image textureCore;
    juce::Image textureDetail;
    juce::Image sharpness;
    float aspect = 1.30f;

    EmissionLayers()
    {
        const auto decoded = juce::ImageFileFormat::loadFrom (
            BinaryData::attack_specimen_body_v2_png,
            static_cast<std::size_t> (BinaryData::attack_specimen_body_v2_pngSize));
        if (! decoded.isValid())
            return;
        const auto source = decoded.getClippedImage (contentBounds (decoded));

        base = transparentLike (source);
        textureCore = transparentLike (source);
        textureDetail = transparentLike (source);
        sharpness = transparentLike (source);
        aspect = static_cast<float> (source.getWidth())
               / static_cast<float> (source.getHeight());

        for (int y = 0; y < source.getHeight(); ++y)
            for (int x = 0; x < source.getWidth(); ++x)
            {
                const auto pixel = source.getPixelAt (x, y);
                const auto level = pixel.getPerceivedBrightness();
                if (level <= 0.012f)
                    continue;

                const auto alpha = juce::jlimit (0.0f, 1.0f, (level - 0.012f) / 0.46f);
                const auto red = static_cast<float> (pixel.getRed());
                const auto green = static_cast<float> (pixel.getGreen());
                const auto blue = static_cast<float> (pixel.getBlue());
                const bool warm = red > blue * 1.10f && red > green * 1.025f;
                const bool cool = blue > red * 1.06f || green > red * 1.08f;

                const auto baseAlpha = alpha * (warm ? 0.20f : cool ? 0.42f : 0.82f);
                base.setPixelAt (x, y, pixel.withAlpha (baseAlpha));
                if (warm)
                {
                    auto& layer = level >= 0.44f ? textureCore : textureDetail;
                    layer.setPixelAt (x, y, pixel.withAlpha (alpha));
                }
                if (cool && isSharpnessArc (source, x, y))
                    sharpness.setPixelAt (x, y, pixel.withAlpha (alpha));
            }
    }

    bool valid() const noexcept
    {
        return base.isValid() && textureCore.isValid()
            && textureDetail.isValid() && sharpness.isValid();
    }

private:
    static juce::Image transparentLike (const juce::Image& source)
    {
        return { juce::Image::ARGB, source.getWidth(), source.getHeight(), true };
    }

    static bool foreground (const juce::Image& source, int x, int y)
    {
        return x >= 0 && y >= 0 && x < source.getWidth() && y < source.getHeight()
            && source.getPixelAt (x, y).getPerceivedBrightness() > 0.012f;
    }

    static juce::Rectangle<int> contentBounds (const juce::Image& source)
    {
        int left = source.getWidth(), right = -1, top = source.getHeight(), bottom = -1;
        for (int y = 0; y < source.getHeight(); ++y)
            for (int x = 0; x < source.getWidth(); ++x)
                if (foreground (source, x, y))
                {
                    left = juce::jmin (left, x); right = juce::jmax (right, x);
                    top = juce::jmin (top, y); bottom = juce::jmax (bottom, y);
                }
        if (right < left || bottom < top)
            return source.getBounds();
        const auto padding = juce::jmax (4, juce::jmin (source.getWidth(), source.getHeight()) / 60);
        return juce::Rectangle<int> (left, top, right - left + 1, bottom - top + 1)
            .expanded (padding).getIntersection (source.getBounds());
    }

    static bool isSharpnessArc (const juce::Image& source, int x, int y)
    {
        const auto nx = 2.0f * static_cast<float> (x) / static_cast<float> (source.getWidth() - 1) - 1.0f;
        const auto ny = 2.0f * static_cast<float> (y) / static_cast<float> (source.getHeight() - 1) - 1.0f;
        const bool selectedArc = (ny < -0.34f && nx > -0.62f && nx < 0.18f)
                              || (nx > 0.53f && ny > -0.22f && ny < 0.34f)
                              || (ny > 0.46f && nx > -0.44f && nx < 0.12f);
        if (! selectedArc)
            return false;
        const auto radius = juce::jmax (2, juce::jmin (source.getWidth(), source.getHeight()) / 48);
        return ! foreground (source, x - radius, y)
            || ! foreground (source, x + radius, y)
            || ! foreground (source, x, y - radius)
            || ! foreground (source, x, y + radius);
    }
};

const EmissionLayers& emissionLayers()
{
    static const EmissionLayers layers;
    return layers;
}

juce::Rectangle<float> specimenBounds (juce::Rectangle<int> area, float strength,
                                       float sourceAspect)
{
    const auto available = area.toFloat().reduced (2.0f);
    const auto maximumHeight = juce::jmin (available.getHeight(),
                                            available.getWidth() / sourceAspect);
    const auto height = maximumHeight * (0.86f + unit (strength) * 0.14f);
    const auto width = maximumHeight * sourceAspect * (0.97f + unit (strength) * 0.03f);
    const auto fixedAttachmentX = available.getCentreX() - maximumHeight * sourceAspect * 0.5f;
    return { fixedAttachmentX,
             available.getCentreY() - height * 0.5f, width, height };
}

void drawLayer (juce::Graphics& g, const juce::Image& image,
                juce::Rectangle<float> target, float opacity)
{
    if (! image.isValid() || opacity <= 0.0f)
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, opacity));
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (image, target);
}
}

void drawSpecimen (juce::Graphics& g, juce::Rectangle<int> area, FeatureAmounts raw)
{
    if (area.getWidth() < 8 || area.getHeight() < 8)
        return;
    const FeatureAmounts amounts { stableAmount (raw.strength), stableAmount (raw.texture),
                                   stableAmount (raw.sharpness) };
    const auto& layers = emissionLayers();
    if (! layers.valid())
        return;

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);
    const auto target = specimenBounds (area, amounts.strength, layers.aspect);
    drawLayer (g, layers.base, target, 0.95f);
    drawLayer (g, layers.textureCore, target,
               0.40f - 0.18f * std::sqrt (amounts.texture));
    drawLayer (g, layers.textureDetail, target,
               0.02f + 0.48f * std::sqrt (amounts.texture));
    drawLayer (g, layers.sharpness, target,
               0.03f + 0.74f * std::sqrt (amounts.sharpness));
}
}
