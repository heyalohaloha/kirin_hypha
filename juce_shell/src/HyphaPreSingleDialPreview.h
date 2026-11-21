#pragma once

#include "HyphaHybridVuPainter.h"
#include "HyphaMetricNeedle.h"
#include "HyphaTextStyle.h"
#include "HyphaVuNeedlePainter.h"
#include <array>

namespace hypha::metric_face::pre_preview
{
// This is an opt-in offscreen candidate only; the VU and POST product entry points do not use it.
inline juce::Rectangle<int> chassisBounds (juce::Rectangle<int> outer)
{
    const auto unit = juce::jmin (outer.getWidth() / 3, outer.getHeight() / 2);
    return outer.withSizeKeepingCentre (unit * 3, unit * 2);
}

// In canonical asset coordinates, photographed hub discs sit inside the lower curl, above the
// common horizontal gold seam. The lower asymmetric points follow the still-visible curl border.
// No edit may reach the common plated rim at or below y=688.
inline constexpr int chassisAssetWidth = 1536, chassisAssetHeight = 1024;
inline constexpr int protectedGoldRow = 688;
inline juce::Path hubFootprint (juce::Rectangle<int> chassis, int slot)
{
    constexpr std::array<std::array<float, 2>, 13> relative {{
        {{ -73, 0 }}, {{ -68, -30 }}, {{ -52, -55 }}, {{ -27, -69 }},
        {{ 0, -74 }}, {{ 29, -68 }}, {{ 56, -52 }}, {{ 70, -27 }},
        {{ 74, -5 }}, {{ 63, 16 }}, {{ 36, 43 }}, {{ -5, 55 }}, {{ -54, 36 }}
    }};
    const auto cx = slot == 0 ? 420.0f : 1110.0f;
    constexpr auto cy = 625.0f;
    const auto sx = chassis.getWidth() / static_cast<float> (chassisAssetWidth);
    const auto sy = chassis.getHeight() / static_cast<float> (chassisAssetHeight);
    juce::Path path;
    for (std::size_t i = 0; i < relative.size(); ++i)
    {
        const auto [dx, dy] = relative[i];
        const auto x = chassis.getX() + (cx + (slot == 0 ? dx : -dx)) * sx;
        const auto y = chassis.getY() + (cy + dy) * sy;
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    path.closeSubPath();
    return path;
}

inline std::array<juce::Rectangle<float>, 2> hubMasks (juce::Rectangle<int> chassis)
{ return { hubFootprint (chassis, 0).getBounds(), hubFootprint (chassis, 1).getBounds() }; }

inline juce::Image withLocalGrainCovers (const juce::Image& base)
{
    if (! base.isValid()) return {};
    auto covered = base.createCopy();
    const auto local = base.getBounds();
    const float scale = local.getHeight() / static_cast<float> (chassisAssetHeight);
    const int sourceOffsetY = juce::roundToInt (scale * 153.0f);
    const int guardRow = static_cast<int> (std::floor (scale * protectedGoldRow));
    const auto translateTexture = [sourceOffsetY, &base] (int x, int y)
    { return base.getPixelAt (x, juce::jmax (0, y - sourceOffsetY)); };
    // Stage as discrete 1:1 pixel copies from this same uniformly rendered chassis; changing
    // coordinates here is translation only. The AA shape can feather 1px edge but may never
    // paint outside the traced hub footprint or move, rescale or crop the common background.
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto footprint = hubFootprint (local, slot);
        const auto bounds = footprint.getBounds().getSmallestIntegerContainer()
                                    .getIntersection (local.withBottom (guardRow));
        if (bounds.isEmpty()) continue;
        juce::Image mask (juce::Image::SingleChannel, bounds.getWidth(), bounds.getHeight(), true);
        {
            juce::Graphics raster (mask);
            raster.setOrigin (-bounds.getPosition());
            raster.setColour (juce::Colours::white);
            raster.fillPath (footprint);
        }
        for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
            for (int x = bounds.getX(); x < bounds.getRight(); ++x)
            {
                const auto coverage = mask.getPixelAt (x - bounds.getX(), y - bounds.getY()).getFloatAlpha();
                if (coverage > 0.0f)
                    covered.setPixelAt (x, y, base.getPixelAt (x, y).interpolatedWith (translateTexture (x, y), coverage));
            }
    }
    return covered;
}

inline void paintHubMasks (juce::Graphics& g, juce::Rectangle<int> chassis,
                           const hybrid_vu::State& state)
{
    const auto local = chassis.withZeroOrigin();
    const auto displayScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto unit = juce::jmax (1, juce::roundToInt (local.getWidth() *
        juce::jlimit (1.0f, 4.0f, displayScale) / 3.0f));
    const auto renderScale = 3.0f * static_cast<float> (unit) / static_cast<float> (local.getWidth());
    juce::Image base (juce::Image::RGB, unit * 3, unit * 2, true);
    {
        juce::Graphics exact (base);
        exact.addTransform (juce::AffineTransform::scale (renderScale));
        hybrid_vu::paintStatic (exact, local, state);
    }
    g.drawImage (withLocalGrainCovers (base), chassis.toFloat(), juce::RectanglePlacement::centred);
}

inline hybrid_vu::needle_painter::DialGeometry singleDial (juce::Rectangle<int> chassis)
{
    const auto face = hybrid_vu::needle_painter::faceBounds (chassis);
    auto dial = hybrid_vu::needle_painter::dialGeometry (face, 0);
    dial.pivot.x = face.getCentreX();
    dial.radiusX = face.getWidth() * 0.39f; // Vector radius for this dial; not a bitmap affine.
    return dial;
}

// Opt-in inspector input for the exact #728 single-hub artwork. The existing background is
// always the default; a two-hub asset must never be passed as a dedicated single-hub asset.
struct DedicatedSingleChassis
{
    juce::Image image;
    juce::Point<float> hubCentre3072 { 1532.0f, 1252.0f }; // normalized via the uncropped full artwork
    float frontCapRadius3072 = 58.0f;
    bool isValid() const noexcept
    {
        const auto width = image.getWidth(), height = image.getHeight();
        return image.isValid() && ((width == 3072 && height == 2048) || (width == 1536 && height == 1024))
            && std::isfinite (hubCentre3072.x) && std::isfinite (hubCentre3072.y)
            && std::abs (hubCentre3072.x - 1536.0f) < 64.0f && std::abs (hubCentre3072.y - 1252.0f) < 64.0f
            && std::isfinite (frontCapRadius3072) && frontCapRadius3072 >= 48.0f && frontCapRadius3072 < 90.0f;
    }
};

inline juce::Point<float> imageHubPivot (juce::Rectangle<int> chassis, const DedicatedSingleChassis& artwork)
{
    const auto scale = chassis.getWidth() / 3072.0f; // chassisBounds guarantees identical x/y scale
    return chassis.getPosition().toFloat() + artwork.hubCentre3072 * scale;
}

inline float physicalPosition (float normalized)
{
    constexpr auto middle = (2.48f - juce::MathConstants<float>::halfPi) / 1.74f;
    return normalized <= 0.5f ? normalized * 2.0f * middle
                              : middle + (normalized * 2.0f - 1.0f) * (1.0f - middle);
}

inline void paint (juce::Graphics& g, juce::Rectangle<int> outer,
                   const hybrid_vu::State& state, const metric_needle::Frame& frame,
                   const DedicatedSingleChassis* artwork = nullptr)
{
    if (state.role != observatory::Role::pre || ! metric_needle::metric (frame.face)) return;
    const auto chassis = chassisBounds (outer);
    if (chassis.isEmpty()) return;
    const auto useArtwork = artwork != nullptr && artwork->isValid();
    if (useArtwork)
        g.drawImage (artwork->image, chassis.toFloat(), juce::RectanglePlacement::centred);
    else
        paintHubMasks (g, chassis, state);
    hybrid_vu::paintDynamic (g, chassis, state); // Same header, true peak/clip, and bottom readouts.

    auto dial = singleDial (chassis);
    if (useArtwork) dial.pivot = imageHubPivot (chassis, *artwork);
    const auto extent = metric_needle::extent (frame.face);
    const auto facts = metric_needle::reading (frame);
    const auto faceWidth = hybrid_vu::needle_painter::faceBounds (chassis).getWidth();
    const auto size = [faceWidth] (float n, float minimum) { return juce::jmax (minimum, faceWidth*n/900.0f); };
    const auto arcPoint = [&dial] (float n) { return hybrid_vu::needle_painter::dialPoint (dial, physicalPosition (n)); };
    const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
    juce::Path arc;
    arc.startNewSubPath (arcPoint (0.0f));
    for (int step = 1; step <= 80; ++step) arc.lineTo (arcPoint (step / 80.0f));
    g.setColour (ivory.withAlpha (0.10f)); g.strokePath (arc, juce::PathStrokeType (size (4.5f, 0.9f)));
    g.setColour (ivory.withAlpha (0.90f)); g.strokePath (arc, juce::PathStrokeType (size (2.35f, 0.8f)));
    auto context = state.presentation; context.referenceCompact = true;
    const int ticks = frame.face == metric_needle::Face::sharp ? 6 : 8;
    const int minor = chassis.getWidth() <= 300 ? 2 : 3;
    for (int step = 0; step <= ticks * minor; ++step)
    {
        const auto n = static_cast<float> (step) / static_cast<float> (ticks * minor);
        const bool zero = step * 2 == ticks * minor, major = step % minor == 0;
        const auto point = arcPoint (n);
        const auto colour = zero && frame.status == metric_needle::Status::ready && std::isfinite (frame.median)
            ? COL_FLORA_BR : ivory.withAlpha (major ? 0.96f : 0.40f);
        g.setColour (colour);
        g.drawLine ({point, dial.pivot + (point - dial.pivot) * (zero ? 1.16f : major ? 1.13f : 1.06f)},
                    size (zero ? 3.6f : major ? 2.6f : 1.2f, major ? 0.9f : 0.5f));
        if (zero || step == 0 || step == ticks * minor)
        {
            const auto centre = dial.pivot + (point - dial.pivot) * 1.24f;
            const auto text = zero ? juce::String ("0") : (n > 0.5f ? "+" : "-")
                + juce::String (extent, frame.face == metric_needle::Face::sharp ? 1 : 0);
            const auto box = juce::Rectangle<int> (juce::roundToInt (size (68.0f, 36.0f)), 17)
                .withCentre (centre.toInt());
            text_style::draw (g, text, box, context, typography::TextRole::axis,
                juce::Justification::centred, 1, typography::Composition::instrument);
        }
    }

    if (facts.ownPosition)
    {
        const auto tip = dial.pivot + (arcPoint (*facts.ownPosition) - dial.pivot) * 1.11f;
        const auto ray = tip - dial.pivot;
        const auto reach = ray.getDistanceFromOrigin();
        // Geometric origin remains at the photographed centre. Draw the shaft only outside
        // the photograph's front cap, so its specular highlights and shadow stay visible.
        const auto covered = useArtwork ? artwork->frontCapRadius3072 * chassis.getWidth() / 3072.0f
            + size (3.0f, 1.0f) : 0.0f;
        if (reach > covered)
        {
            const auto visibleStart = dial.pivot + ray * (covered / reach);
            g.setColour (COL_SPECTRUM_PRE.withAlpha (0.26f));
            g.drawLine ({visibleStart, tip}, size (5.2f, 1.5f));
            g.setColour (COL_SPECTRUM_PRE.brighter (0.14f));
            g.drawLine ({visibleStart, tip}, size (2.7f, 0.95f));
        }
        if (const auto edge = metric_needle::rangeEdge (frame.face, facts.own); edge != 0)
        {
            const auto p = arcPoint (edge < 0 ? 0.0f : 1.0f);
            g.setColour (COL_FLORA_BR);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
        }
    }

    // The old artwork fallback retains its vector front cap. The dedicated artwork contains
    // its own three-dimensional hub and must never receive either vector cap or two-hub mask.
    if (! useArtwork)
    {
        const auto cap = juce::Rectangle<float> (size (45.0f, 15.0f), size (45.0f, 15.0f)).withCentre (dial.pivot);
        juce::ColourGradient metal (juce::Colour (0xff574733), cap.getTopLeft(),
            juce::Colour (0xff181817), cap.getBottomRight(), false);
        metal.addColour (0.42, juce::Colour (0xff0b0d0d));
        metal.addColour (0.62, juce::Colour (0xff382c1d));
        g.setGradientFill (metal);
        g.fillEllipse (cap);
        g.setColour (COL_FLORA_BR.withAlpha (0.64f));
        g.drawEllipse (cap, size (1.5f, 0.7f));

    }

    // Keep the role and status out of the centre needle's travel, including a true-zero reading.
    const auto caption = frame.status == metric_needle::Status::measuring ? juce::String ("PREPARING")
        : frame.status == metric_needle::Status::unavailable || !std::isfinite (frame.median)
            ? juce::String ("UNAVAILABLE") : juce::String (metric_needle::label (frame.face)) + " / PRE";
    g.setColour (COL_NORMAL);
    text_style::draw (g, caption, juce::Rectangle<int> (chassis.getWidth() / 3, 18)
        .withCentre ({ juce::roundToInt (dial.pivot.x + faceWidth * 0.22f),
                       juce::roundToInt (dial.pivot.y + size (10.0f, 4.0f)) }),
        context, typography::TextRole::legend, juce::Justification::centred, 1, typography::Composition::instrument);
}
}
