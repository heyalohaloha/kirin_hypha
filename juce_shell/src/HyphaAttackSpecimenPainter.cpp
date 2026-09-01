#include "HyphaAttackSpecimenPainter.h"

#include <cmath>
#include <vector>

#include <BinaryData.h>

#include "HyphaAttackUiContract.h"

namespace hypha::attack_specimen
{
namespace
{
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);

struct EmissionLayers
{
    juce::Image strength;
    juce::Image texture;
    juce::Image brightness;
    juce::Image transient;

    EmissionLayers()
    {
        const auto source = juce::ImageFileFormat::loadFrom (
            BinaryData::attack_specimen_emission_png,
            static_cast<std::size_t> (BinaryData::attack_specimen_emission_pngSize));
        if (! source.isValid())
            return;
        strength = juce::Image (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
        texture = juce::Image (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
        brightness = juce::Image (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
        transient = juce::Image (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
        for (int y = 0; y < source.getHeight(); ++y)
        {
            for (int x = 0; x < source.getWidth(); ++x)
            {
                const auto pixel = source.getPixelAt (x, y);
                const auto level = pixel.getPerceivedBrightness();
                if (level <= 0.018f)
                    continue;
                const auto alpha = juce::jlimit (0.0f, 1.0f, (level - 0.018f) / 0.62f);
                const auto emission = pixel.withAlpha (alpha);
                const bool warm = pixel.getRed() > pixel.getBlue() * 1.12f
                               && pixel.getRed() > pixel.getGreen() * 1.04f;
                const auto horizontal = static_cast<float> (x)
                                      / static_cast<float> (source.getWidth());
                if (warm && level > 0.48f && horizontal > 0.22f && horizontal < 0.48f)
                    strength.setPixelAt (x, y, emission);
                else if (warm)
                    texture.setPixelAt (x, y, emission);
                else if (horizontal > 0.47f)
                    transient.setPixelAt (x, y, emission);
                else
                    brightness.setPixelAt (x, y, emission);
            }
        }
    }
};

const EmissionLayers& emissionLayers()
{
    static const EmissionLayers layers;
    return layers;
}

void drawEmissionLayer (juce::Graphics& g, const juce::Image& image,
                        juce::Rectangle<int> area, float amount)
{
    if (! image.isValid() || amount <= 0.0f)
        return;
    juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (std::sqrt (juce::jlimit (0.0f, 1.0f, amount)));
    const auto width = juce::jmin (area.getWidth(),
                                   static_cast<int> (std::lround (area.getHeight() * 3.15f)));
    const auto target = juce::Rectangle<int> (width, area.getHeight()).withCentre (area.getCentre());
    g.drawImageWithin (image, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                       juce::RectanglePlacement::stretchToFit, false);
}

float sampleShape (const KirinAttackDetail& detail, float phase)
{
    const auto count = juce::jmin (
        detail.shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    if (count < 2)
        return 0.0f;
    const auto position = juce::jlimit (0.0f, 1.0f, phase) * static_cast<float> (count - 1);
    const auto first = static_cast<std::uint32_t> (position);
    const auto second = juce::jmin (first + 1, count - 1);
    const auto blend = position - static_cast<float> (first);
    const auto left = std::isfinite (detail.shape[first]) ? detail.shape[first] : 0.0f;
    const auto right = std::isfinite (detail.shape[second]) ? detail.shape[second] : 0.0f;
    return juce::jmax (0.0f, left + (right - left) * blend);
}

float maximumShape (const KirinAttackDetail& detail)
{
    const auto count = juce::jmin (
        detail.shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    auto maximum = 0.0f;
    for (std::uint32_t index = 0; index < count; ++index)
        if (std::isfinite (detail.shape[index]))
            maximum = juce::jmax (maximum, detail.shape[index]);
    return juce::jmax (maximum, 0.000'001f);
}

juce::Path smoothClosedPath (const std::vector<juce::Point<float>>& points)
{
    juce::Path path;
    if (points.empty())
        return path;
    path.startNewSubPath (points.front());
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        const auto& previous = points[index - 1];
        const auto& point = points[index];
        path.quadraticTo (previous, (previous + point) * 0.5f);
    }
    path.lineTo (points.back());
    path.closeSubPath();
    return path;
}

juce::Path specimenBody (const KirinAttackDetail& detail,
                         juce::Rectangle<int> area,
                         float scale,
                         float tailAmount)
{
    std::vector<juce::Point<float>> points;
    constexpr int arcSteps = 38;
    points.reserve (arcSteps * 2 + 8);
    const auto centreX = static_cast<float> (area.getX()) + area.getWidth() * 0.43f;
    const auto centreY = static_cast<float> (area.getCentreY());
    const auto radiusY = juce::jmax (2.0f, area.getHeight() * 0.43f * scale);
    const auto radiusX = juce::jmin (area.getWidth() * 0.30f,
                                     area.getHeight() * 1.34f) * scale;
    const auto maximum = maximumShape (detail);
    const auto position = [&] (float angle, float phase)
    {
        const auto measured = sampleShape (detail, phase) / maximum;
        const auto modulation = 0.93f + measured * 0.10f;
        return juce::Point<float> {
            centreX + std::cos (angle) * radiusX * modulation,
            centreY - std::sin (angle) * radiusY * modulation
        };
    };
    for (int index = 0; index <= arcSteps; ++index)
    {
        const auto phase = static_cast<float> (index) / static_cast<float> (arcSteps);
        points.push_back (position (juce::MathConstants<float>::pi * (1.0f - phase),
                                    phase * 0.5f));
    }
    const auto bodyRight = position (0.0f, 0.5f);
    const auto tailLength = area.getWidth() * (0.10f + 0.30f * tailAmount) * scale;
    const auto tailTip = juce::jmin (static_cast<float> (area.getRight() - 2),
                                     bodyRight.x + tailLength);
    points.push_back ({ bodyRight.x + tailLength * 0.30f, centreY - radiusY * 0.27f });
    points.push_back ({ tailTip - tailLength * 0.16f, centreY - radiusY * 0.08f });
    points.push_back ({ tailTip, centreY });
    points.push_back ({ tailTip - tailLength * 0.16f, centreY + radiusY * 0.08f });
    points.push_back ({ bodyRight.x + tailLength * 0.30f, centreY + radiusY * 0.27f });
    for (int index = 0; index <= arcSteps; ++index)
    {
        const auto phase = static_cast<float> (index) / static_cast<float> (arcSteps);
        points.push_back (position (-juce::MathConstants<float>::pi * phase,
                                    0.5f + phase * 0.5f));
    }
    return smoothClosedPath (points);
}

void fillFeathered (juce::Graphics& g, const KirinAttackDetail& detail,
                    juce::Rectangle<int> area, float scale, float tail,
                    juce::Colour colour, float amount)
{
    if (amount <= 0.0f)
        return;
    constexpr int layers = 7;
    for (int index = 0; index < layers; ++index)
    {
        const auto progress = static_cast<float> (index) / static_cast<float> (layers - 1);
        g.setColour (colour.withAlpha ((0.018f + progress * 0.050f) * amount));
        g.fillPath (specimenBody (detail, area, scale * (1.12f - progress * 0.20f), tail));
    }
}

void drawMembranes (juce::Graphics& g, const KirinAttackDetail& detail,
                    juce::Rectangle<int> area, float brightness, float tail)
{
    if (brightness <= 0.0f)
        return;
    constexpr int membranes = 7;
    for (int index = 0; index < membranes; ++index)
    {
        const auto progress = static_cast<float> (index) / static_cast<float> (membranes - 1);
        const auto scale = 0.72f + progress * 0.20f;
        g.setColour (brightnessColour.withAlpha ((0.035f + progress * 0.10f) * brightness));
        g.strokePath (specimenBody (detail, area, scale, tail),
                      juce::PathStrokeType (index == membranes - 1 ? 0.95f : 0.48f,
                                            juce::PathStrokeType::curved));
    }
}

void drawAura (juce::Graphics& g, const KirinAttackDetail& detail,
               juce::Rectangle<int> area, float amount, float tail)
{
    if (amount <= 0.0f)
        return;
    constexpr int layers = 7;
    for (int index = 0; index < layers; ++index)
    {
        const auto progress = static_cast<float> (index) / static_cast<float> (layers - 1);
        g.setColour (transientColour.withAlpha ((0.025f + progress * 0.055f) * amount));
        g.strokePath (specimenBody (detail, area, 1.16f - progress * 0.11f, tail),
                      juce::PathStrokeType (3.2f - progress * 2.4f,
                                            juce::PathStrokeType::curved));
    }
}

void drawFibres (juce::Graphics& g, const KirinAttackDetail& detail,
                 juce::Rectangle<int> area, FeatureAmounts amounts)
{
    const auto centre = juce::Point<float> (
        static_cast<float> (area.getX()) + area.getWidth() * 0.43f,
        static_cast<float> (area.getCentreY()));
    constexpr int fibres = 30;
    for (int index = 0; index < fibres; ++index)
    {
        const auto phase = static_cast<float> (index) / static_cast<float> (fibres - 1);
        const auto angle = -2.72f + phase * 5.44f;
        const bool membraneFibre = index % 5 == 0;
        const auto radiusY = area.getHeight() * (membraneFibre ? 0.36f : 0.27f);
        const auto radiusX = juce::jmin (
            area.getWidth() * (membraneFibre ? 0.26f : 0.20f),
            area.getHeight() * (membraneFibre ? 1.12f : 0.84f));
        const auto measured = sampleShape (detail, phase);
        const auto normalized = measured / maximumShape (detail);
        auto endpoint = juce::Point<float> (
            centre.x + std::cos (angle) * radiusX * (0.94f + normalized * 0.08f),
            centre.y + std::sin (angle) * radiusY * (0.94f + normalized * 0.08f));
        if (std::abs (angle) < 0.62f)
            endpoint.x += area.getWidth() * amounts.transient * 0.22f
                        * (1.0f - std::abs (angle) / 0.62f);
        juce::Path fibre;
        const auto startY = centre.y
                          + std::sin ((phase * 4.0f + normalized)
                                      * juce::MathConstants<float>::pi)
                                * radiusY * 0.11f;
        fibre.startNewSubPath (centre.x - radiusX * (0.24f - phase * 0.08f), startY);
        const auto bend = (normalized - 0.5f) * radiusY * 0.28f;
        fibre.cubicTo (centre.x + radiusX * (0.02f + phase * 0.07f), startY + bend,
                       centre.x + (endpoint.x - centre.x) * 0.62f,
                       endpoint.y - bend * 0.45f, endpoint.x, endpoint.y);
        const auto colour = membraneFibre ? brightnessColour
                          : index % 4 == 0 ? strengthColour : textureColour;
        const auto amount = membraneFibre ? amounts.brightness : amounts.texture;
        if (amount <= 0.0f)
            continue;
        g.setColour (colour.withAlpha (amount
            * (membraneFibre ? 0.28f : index % 4 == 0 ? 0.32f : 0.27f)));
        g.strokePath (fibre, juce::PathStrokeType (index % 5 == 0 ? 0.88f : 0.52f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

}

void draw (juce::Graphics& g, const KirinAttackDetail& detail,
           juce::Rectangle<int> area, FeatureAmounts amounts)
{
    if (area.getWidth() < 4 || area.getHeight() < 4 || detail.shape_count < 2)
        return;
    const auto tail = juce::jlimit (0.0f, 1.0f, amounts.transient);
    const auto& layers = emissionLayers();
    drawEmissionLayer (g, layers.texture, area, amounts.texture);
    drawEmissionLayer (g, layers.brightness, area, amounts.brightness);
    drawEmissionLayer (g, layers.transient, area, amounts.transient);
    drawEmissionLayer (g, layers.strength, area, amounts.strength);
    drawAura (g, detail, area, amounts.transient * 1.35f, tail);
    fillFeathered (g, detail, area, 0.69f, tail * 0.55f,
                   textureColour, amounts.texture * 1.25f);
    drawMembranes (g, detail, area, amounts.brightness, tail);
    drawFibres (g, detail, area, amounts);
}
}
