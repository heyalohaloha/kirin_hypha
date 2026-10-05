#pragma once

#include "../src/HyphaSpectrumChromePainter.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSurfaceMaterial.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests::spectrum_control_light
{
inline void checkLight (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Spectrum control light contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

struct Fixture
{
    KirinSpectrumView snapshot {};
    spectrum_painter::SpectrumBins pre {}, post {}, delta {};
    spectrum_painter::SpectrumValidity validity {};
    guide_frequency::Overlay guide;
    juce::String empty;

    Fixture()
    {
        snapshot.status = KIRIN_SPECTRUM_ACTIVE;
        snapshot.has_data = snapshot.post_has_data = 1;
        snapshot.channels = 2; snapshot.sample_rate = 48'000;
        snapshot.aperture_samples = 4'096; snapshot.fft_size = 8'192;
        snapshot.min_hz = 10; snapshot.max_hz = 22'000;
        snapshot.presentation_end_samples = 288'000;
        pre.fill (-48); post.fill (-42); delta.fill (6); validity.fill (1);
    }

    spectrum_chrome::PaintState state (presentation::Context context, uint8_t mode,
        bool absolute = true, bool mark = false, bool valid = true, bool shape = false,
        uint8_t channels = 2) const
    {
        return { snapshot, pre, post, delta, validity, pre, post, delta, validity,
            delta, validity, nullptr, empty, empty, empty, guide, nullptr, post,
            absolute, mode == KIRIN_SPECTRUM_SELECTION_MID_SIDE, shape,
            true, valid, mark, -1, -1, mode, channels, true, context };
    }
};

inline juce::Image render (juce::Rectangle<float> bounds, const spectrum_chrome::PaintState& state,
                           int originX)
{
    juce::Component root, pane;
    root.getProperties().set (key_light::rootProperty, true);
    root.setSize (state.presentation.logicalWidth, state.presentation.logicalHeight);
    root.addAndMakeVisible (pane);
    pane.setBounds (originX, 67, juce::roundToInt (bounds.getWidth()), juce::roundToInt (bounds.getHeight()));
    const key_light::Scope light (pane);
    juce::Image image (juce::Image::ARGB, pane.getWidth(), pane.getHeight(), true);
    juce::Graphics graphics (image);
    graphics.fillAll (BG);
    spectrum_chrome::paint (graphics, bounds, state);
    return image;
}

inline int differences (const juce::Image& first, const juce::Image& second,
                        juce::Rectangle<float> area)
{
    int pixels = 0;
    const auto clip = area.toNearestInt().getIntersection (first.getBounds());
    for (int y = clip.getY(); y < clip.getBottom(); ++y)
        for (int x = clip.getX(); x < clip.getRight(); ++x)
            pixels += first.getPixelAt (x, y) != second.getPixelAt (x, y);
    return pixels;
}

inline int bevelBrightness (const juce::Image& image, juce::Rectangle<float> plate, bool right)
{
    int sum = 0;
    const auto at = right ? 0.70f : 0.30f;
    const auto strip = juce::Rectangle<float> (plate.getX() + plate.getWidth() * (at - 0.06f),
        plate.getY() + 1.0f, plate.getWidth() * 0.12f, 3.0f).toNearestInt();
    for (int y = strip.getY(); y < strip.getBottom(); ++y)
        for (int x = strip.getX(); x < strip.getRight(); ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            sum += pixel.getRed() + pixel.getGreen() + pixel.getBlue();
        }
    return sum;
}

inline void checkDirection (const juce::Image& rightLit, const juce::Image& leftLit,
                            juce::Rectangle<float> plate, const juce::String& name)
{
    checkLight (bevelBrightness (rightLit, plate, true) > bevelBrightness (leftLit, plate, true)
        && bevelBrightness (leftLit, plate, false) > bevelBrightness (rightLit, plate, false),
        name + ": the raised bevel follows the editor light, not a uniform reflection");
}
}

namespace hypha::tests
{
inline void verifySpectrumControlLightContract()
{
    spectrum_control_light::Fixture fixture;
    for (const auto& preset : ui_contract::spectrumSizePresets)
    {
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto body = ui_contract::spectrumPlotBounds (preset.width, preset.height);
        const juce::Rectangle<float> bounds (0, 0, (float) body.width, (float) body.height);
        const auto outer = spectrum_geometry::plotBoundsFor (bounds);
        const auto scale = spectrum_geometry::visualScaleFor (bounds);
        const auto where = " at " + juce::String (preset.width);
        if (spectrum_geometry::viewOnly (scale))
        {
            for (size_t mode = 0; mode < ui_contract::spectrumDisplayModeWidths.size(); ++mode)
                spectrum_control_light::checkLight (
                    spectrum_geometry::displayModeBoundsFor (mode, outer, scale).isEmpty(),
                    "100% retains quiet, view-only modes");
            spectrum_control_light::checkLight (spectrum_geometry::markBoundsFor (outer, scale).isEmpty()
                && spectrum_geometry::deltaModeBoundsFor (outer, scale).isEmpty(),
                "100% adds no MARK or delta-mode plates");
            continue;
        }
        // The same pane on either side of the logical editor light; only its raised material
        // changes. Unselected mode labels must remain quiet and identical at either origin.
        for (uint8_t mode = 0; mode < ui_contract::spectrumDisplayModeWidths.size(); ++mode)
        {
            const auto state = fixture.state (context, mode);
            const auto rightLit = spectrum_control_light::render (bounds, state, -preset.width);
            const auto leftLit = spectrum_control_light::render (bounds, state, preset.width);
            spectrum_control_light::checkDirection (rightLit, leftLit,
                spectrum_geometry::displayModeBoundsFor (mode, outer, scale),
                "selected channel mode " + juce::String (mode) + where);
            for (size_t other = 0; other < ui_contract::spectrumDisplayModeWidths.size(); ++other)
                if (other != mode)
                    spectrum_control_light::checkLight (spectrum_control_light::differences (rightLit, leftLit,
                        spectrum_geometry::displayModeBoundsFor (other, outer, scale)) == 0,
                        "unselected modes stay quiet" + where);
        }
        const auto side = spectrum_geometry::displayModeBoundsFor (KIRIN_SPECTRUM_CHANNEL_SIDE, outer, scale);
        const auto availableSide = spectrum_control_light::render (bounds,
            fixture.state (context, KIRIN_SPECTRUM_CHANNEL_SIDE), 0);
        const auto unavailableSide = spectrum_control_light::render (bounds,
            fixture.state (context, KIRIN_SPECTRUM_CHANNEL_SIDE, true, false, true, false, 1), 0);
        spectrum_control_light::checkLight (spectrum_control_light::differences (
            availableSide, unavailableSide, side) > 0, "mono SIDE keeps its unavailable text state" + where);

        const auto mark = spectrum_geometry::markBoundsFor (outer, scale);
        for (const bool held : { false, true })
        {
            const auto state = fixture.state (context, KIRIN_SPECTRUM_CHANNEL_LR, false, held);
            spectrum_control_light::checkDirection (
                spectrum_control_light::render (bounds, state, -preset.width),
                spectrum_control_light::render (bounds, state, preset.width), mark,
                (held ? "held MARK" : "empty MARK") + where);
        }
        const auto emptyMark = spectrum_control_light::render (bounds,
            fixture.state (context, KIRIN_SPECTRUM_CHANNEL_LR, false), 0);
        const auto heldMark = spectrum_control_light::render (bounds,
            fixture.state (context, KIRIN_SPECTRUM_CHANNEL_LR, false, true), 0);
        const auto invalidMark = spectrum_control_light::render (bounds,
            fixture.state (context, KIRIN_SPECTRUM_CHANNEL_LR, false, false, false), 0);
        spectrum_control_light::checkLight (spectrum_control_light::differences (emptyMark, heldMark, mark) > 0
            && spectrum_control_light::differences (emptyMark, invalidMark, mark) > 0,
            "held/clear and unavailable MARK states remain distinct" + where);

        const auto selector = spectrum_geometry::deltaModeBoundsFor (outer, scale);
        for (const bool shape : { false, true })
        {
            const auto state = fixture.state (context, KIRIN_SPECTRUM_CHANNEL_LR, false, false, true, shape);
            const auto rightLit = spectrum_control_light::render (bounds, state, -preset.width);
            const auto leftLit = spectrum_control_light::render (bounds, state, preset.width);
            const auto half = selector.getWidth() * 0.5f;
            const auto selected = shape ? selector.withTrimmedLeft (half) : selector.withWidth (half);
            const auto quiet = shape ? selector.withWidth (half) : selector.withTrimmedLeft (half);
            spectrum_control_light::checkDirection (rightLit, leftLit, selected,
                (shape ? "SHAPE" : "RAW") + where);
            spectrum_control_light::checkLight (spectrum_control_light::differences (rightLit, leftLit, quiet) == 0,
                "the unselected delta mode stays quiet" + where);
        }
    }
}
}
