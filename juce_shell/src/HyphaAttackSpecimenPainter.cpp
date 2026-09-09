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
    juce::Image strength;
    juce::Image texture;
    juce::Image sharpness;

    EmissionLayers()
    {
        const auto decoded = juce::ImageFileFormat::loadFrom (
            BinaryData::attack_specimen_emission_png,
            static_cast<std::size_t> (BinaryData::attack_specimen_emission_pngSize));
        if (! decoded.isValid())
            return;
        // The approved source includes a diagnostic tail. ATTACK's central specimen uses the
        // membrane body and the first part of that tail so it reads at 600x400 and below.
        const auto source = decoded.getClippedImage ({ 0, 0,
            juce::jmin (600, decoded.getWidth()), decoded.getHeight() });

        base = transparentLike (source);
        strength = transparentLike (source);
        texture = transparentLike (source);
        sharpness = transparentLike (source);

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
                const auto horizontal = static_cast<float> (x)
                                      / static_cast<float> (source.getWidth());

                base.setPixelAt (x, y, pixel.withAlpha (alpha));
                if (warm && horizontal > 0.16f && horizontal < 0.52f && level > 0.28f)
                    strength.setPixelAt (x, y, pixel.withAlpha (alpha));
                if (warm)
                    texture.setPixelAt (x, y, pixel.withAlpha (alpha));
                if (cool)
                    sharpness.setPixelAt (x, y, pixel.withAlpha (alpha));
            }
    }

    bool valid() const noexcept
    {
        return base.isValid() && strength.isValid()
            && texture.isValid() && sharpness.isValid();
    }

private:
    static juce::Image transparentLike (const juce::Image& source)
    {
        return { juce::Image::ARGB, source.getWidth(), source.getHeight(), true };
    }
};

const EmissionLayers& emissionLayers()
{
    static const EmissionLayers layers;
    return layers;
}

juce::Rectangle<float> specimenBounds (juce::Rectangle<int> area, float strength)
{
    const auto available = area.toFloat().reduced (2.0f);
    // Compact DAW panes are much wider than their remaining ATTACK detail height. The approved
    // emission is deliberately presented as a broad specimen rather than collapsing to a glyph.
    auto height = available.getHeight();
    auto width = juce::jmin (available.getWidth() * 0.72f, height * 3.20f);
    const auto growth = 0.82f + unit (strength) * 0.18f;
    width *= growth;
    height *= growth;
    return { available.getCentreX() - width * 0.5f,
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
    const auto target = specimenBounds (area, amounts.strength);
    drawLayer (g, layers.base, target, 0.88f);
    drawLayer (g, layers.strength, target, 0.08f + 0.52f * std::sqrt (amounts.strength));
    drawLayer (g, layers.texture, target, 0.05f + 0.55f * std::sqrt (amounts.texture));
    drawLayer (g, layers.sharpness, target, 0.06f + 0.54f * std::sqrt (amounts.sharpness));
}
}
