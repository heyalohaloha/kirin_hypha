#pragma once

#include "HyphaPreSingleDialPreview.h"

namespace hypha::metric_face
{
// An in-memory candidate only. The factory loads both images in an isolated QA runner;
// no env/config/filesystem probing or bundled asset is added to the product.
struct MetricChassisPair
{
    pre_preview::DedicatedSingleChassis pre;
    juce::Image post;
    bool isValid() const noexcept
    {
        return pre.isValid() && post.isValid() && post.getWidth() == pre.image.getWidth()
            && post.getHeight() == pre.image.getHeight();
    }
};

class MeterBackdrop
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<int> chassis, const juce::Image& source)
    {
        const auto scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto unit = juce::jmax (1, juce::roundToInt (chassis.getWidth() * scale / 3.0f));
        if (! raster.isValid() || image != source || raster.getWidth() != unit * 3)
        {
            image = source;
            raster = juce::Image (juce::Image::RGB, unit*3, unit*2, true);
            juce::Graphics layer (raster);
            layer.drawImage (source, raster.getBounds().toFloat(), juce::RectanglePlacement::centred);
            ++builds;
        }
        g.drawImage (raster, chassis.toFloat(), juce::RectanglePlacement::centred);
    }
    unsigned buildsForTest() const noexcept { return builds; }
private:
    juce::Image image, raster;
    unsigned builds = 0;
};

inline int dialCount (observatory::Role role) noexcept { return role == observatory::Role::pre ? 1 : 2; }
inline hybrid_vu::needle_painter::DialGeometry candidateDial (
    juce::Rectangle<int> chassis, const MetricChassisPair& bank, observatory::Role role, int channel)
{
    using namespace hybrid_vu::needle_painter;
    if (role == observatory::Role::pre)
    {
        auto dial = pre_preview::singleDial (chassis);
        dial.pivot = pre_preview::imageHubPivot (chassis, bank.pre);
        return dial;
    }
    auto dial = dialGeometry (faceBounds (chassis), channel);
    // Published two-dial geometry matches the photographed mid hubs in the offscreen reference.
    return dial;
}

inline void paintCandidateDial (juce::Graphics& g, juce::Rectangle<int> chassis,
    const hybrid_vu::needle_painter::DialGeometry& dial, const metric_needle::Frame& frame,
    std::optional<float> needle, double difference, bool isPre,
    float capRadius, presentation::Context context)
{
    const auto faceWidth = hybrid_vu::needle_painter::faceBounds (chassis).getWidth();
    const auto size = [faceWidth] (float p, float minimum) { return juce::jmax (minimum, faceWidth*p/900.0f); };
    const auto point = [&dial] (float normalized)
    {
        return hybrid_vu::needle_painter::dialPoint (dial, pre_preview::physicalPosition (normalized));
    };
    const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
    juce::Path arc; arc.startNewSubPath (point (0.0f));
    for (int step = 1; step <= 60; ++step) arc.lineTo (point (step/60.0f));
    g.setColour (ivory.withAlpha (0.12f)); g.strokePath (arc, juce::PathStrokeType (size (4.5f, 0.8f)));
    g.setColour (ivory); g.strokePath (arc, juce::PathStrokeType (size (2.35f, 0.75f)));
    const auto ready = frame.status == metric_needle::Status::ready && std::isfinite (frame.median);
    const auto range = metric_needle::extent (frame.face);
    const int groups = frame.face == metric_needle::Face::sharp ? 6 : 8;
    const int sub = chassis.getWidth() <= 300 ? 2 : 3;
    context.referenceCompact = true;
    for (int step = 0; step <= groups*sub; ++step)
    {
        const auto t = static_cast<float> (step) / static_cast<float> (groups*sub);
        const auto zero = step*2 == groups*sub, major = step%sub == 0;
        const auto p = point (t);
        g.setColour (zero && ready ? COL_FLORA_BR : ivory.withAlpha (major ? 0.96f : 0.40f));
        g.drawLine ({ p, dial.pivot + (p-dial.pivot)*(zero ? 1.16f : major ? 1.13f : 1.06f) },
                    size (zero ? 3.6f : major ? 2.6f : 1.2f, major ? 0.9f : 0.5f));
        if (step == 0 || zero || step == groups*sub)
        {
            const auto anchor = dial.pivot + (p-dial.pivot)*1.22f;
            const auto text = zero ? juce::String ("0") : (t > 0.5f ? "+" : "-")
                                + juce::String (range, frame.face == metric_needle::Face::sharp ? 1 : 0);
            const auto box = juce::Rectangle<int> (juce::roundToInt (size (52.0f, 30.0f)), 16).withCentre (anchor.toInt());
            text_style::draw (g, text, box, context, typography::TextRole::axis,
                juce::Justification::centred, 1, typography::Composition::instrument);
        }
    }
    if (! ready || ! needle) return;
    const auto ray = (point (*needle)-dial.pivot)*1.11f;
    const auto reach = ray.getDistanceFromOrigin();
    if (reach > capRadius)
    {
        const auto colour = isPre ? COL_SPECTRUM_PRE : COL_FLORA_BR;
        const auto start = dial.pivot + ray * (capRadius/reach);
        g.setColour (colour.withAlpha (0.23f));
        g.drawLine ({start, dial.pivot+ray}, size (5.2f, 1.5f));
        g.setColour (colour.brighter (0.15f));
        g.drawLine ({start, dial.pivot+ray}, size (2.7f, 0.95f));
    }
    if (const auto edge = metric_needle::rangeEdge (frame.face, difference); edge != 0)
    {
        g.setColour (COL_FLORA_BR);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (point (edge < 0 ? 0.0f : 1.0f)));
    }
}

inline bool paintMetricCandidate (juce::Graphics& g, juce::Rectangle<int> outer,
    const hybrid_vu::State& state, const metric_needle::Frame& frame,
    const MetricChassisPair* mid, MeterBackdrop& backdrop)
{
    if (mid == nullptr || ! mid->isValid() || ! metric_needle::metric (frame.face))
        return false; // not chosen yet: do not draw or change the existing VU/old PRE or POST.
    const auto chassis = pre_preview::chassisBounds (outer);
    if (chassis.isEmpty()) return false;
    const bool one = dialCount (state.role) == 1;
    backdrop.paint (g, chassis, one ? mid->pre.image : mid->post);
    hybrid_vu::paintDynamic (g, chassis, state);
    const auto facts = metric_needle::reading (frame);
    const auto context = state.presentation;
    for (int slot = 0; slot < dialCount (state.role); ++slot)
    {
        const auto dial = candidateDial (chassis, *mid, state.role, slot);
        const bool isPre = one || slot == 0;
        const bool own = one || slot == 1;
        const auto cap = (one ? mid->pre.frontCapRadius3072 : 57.0f) * chassis.getWidth()/3072.0f + 1.5f;
        paintCandidateDial (g, chassis, dial, frame, own ? facts.ownPosition : facts.prePosition,
                            own ? facts.own : facts.pre, isPre, cap, context);
        const auto caption = juce::String (isPre ? "PRE" : "POST") + " / " + metric_needle::unit (frame.face);
        const auto x = dial.pivot.x + (one ? chassis.getWidth()*0.21f : chassis.getWidth()*0.082f);
        const auto y = dial.pivot.y + (one ? 5.0f : 3.0f);
        g.setColour (COL_TEXT_SECONDARY);
        text_style::draw (g, caption, juce::Rectangle<int> (92, 16).withCentre ({juce::roundToInt (x),juce::roundToInt (y)}),
            context, typography::TextRole::legend, juce::Justification::centred, 1, typography::Composition::instrument);
    }
    g.setColour (COL_NORMAL);
    const auto headline = frame.status == metric_needle::Status::measuring ? juce::String ("PREPARING")
        : frame.status != metric_needle::Status::ready || ! std::isfinite (frame.median) ? juce::String ("UNAVAILABLE")
        : juce::String (metric_needle::label (frame.face));
    const auto line = chassis.withTrimmedTop (juce::roundToInt (chassis.getHeight()*0.31f))
        .removeFromTop (18).withSizeKeepingCentre (chassis.getWidth()/4, 18);
    text_style::draw (g, headline, line, context, typography::TextRole::legend,
        juce::Justification::centred, 1, typography::Composition::instrument);
    return true;
}
}
