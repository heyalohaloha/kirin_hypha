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

inline juce::Point<float> candidatePoint (const hybrid_vu::needle_painter::DialGeometry& dial,
                                          float normalized)
{
    return hybrid_vu::needle_painter::dialPoint (dial, pre_preview::physicalPosition (normalized));
}

inline juce::Point<float> candidateReferenceBandPoint (const hybrid_vu::needle_painter::DialGeometry& dial,
                                                       float normalized)
{
    return dial.pivot + (candidatePoint (dial, normalized) - dial.pivot) * 0.91f;
}

inline juce::Line<float> candidateNeedleLine (const hybrid_vu::needle_painter::DialGeometry& dial,
                                             float normalized, float reach, float capRadius)
{
    const auto ray = (candidatePoint (dial, normalized) - dial.pivot) * reach;
    const auto length = ray.getDistanceFromOrigin();
    const auto inner = length > capRadius ? dial.pivot + ray * (capRadius / length) : dial.pivot + ray;
    return {inner, dial.pivot + ray};
}

inline void drawCandidateText (juce::Graphics& g, const juce::String& text,
    juce::Rectangle<int> box, presentation::Context context, typography::TextRole role,
    juce::Colour colour, text_style::LabelPolicy policy = text_style::LabelPolicy::localized)
{
    constexpr auto instrument = typography::Composition::instrument;
    context.referenceCompact = true;
    g.setFont (labelFont (context, role, instrument));
    g.setColour (colour);
    text_style::draw (g, text, box, context, role, juce::Justification::centred, 1, instrument, policy);
}

inline void paintCandidateDial (juce::Graphics& g, juce::Rectangle<int> chassis,
    const hybrid_vu::needle_painter::DialGeometry& dial, const metric_needle::Frame& frame,
    std::optional<float> needle, double difference, bool isPre, bool ownsReference,
    const metric_needle::Reading& facts, float capRadius, presentation::Context context)
{
    const auto faceWidth = hybrid_vu::needle_painter::faceBounds (chassis).getWidth();
    const auto size = [faceWidth] (float p, float minimum) { return juce::jmax (minimum, faceWidth*p/900.0f); };
    const auto point = [&dial] (float normalized)
    {
        return candidatePoint (dial, normalized);
    };
    if (ownsReference && facts.relativeBand)
    {
        const auto low = *metric_needle::normalized (frame.face, facts.relativeBand->low);
        const auto high = *metric_needle::normalized (frame.face, facts.relativeBand->high);
        juce::Path band;
        band.startNewSubPath (candidateReferenceBandPoint (dial, low));
        for (int step = 1; step <= 32; ++step)
            band.lineTo (candidateReferenceBandPoint (dial, low + (high - low) * static_cast<float> (step) / 32.0f));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.44f));
        g.strokePath (band, juce::PathStrokeType (size (6.0f, 2.8f)));
    }
    const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
    juce::Path arc; arc.startNewSubPath (point (0.0f));
    for (int step = 1; step <= 60; ++step) arc.lineTo (point (step/60.0f));
    g.setColour (ivory.withAlpha (0.12f)); g.strokePath (arc, juce::PathStrokeType (size (4.5f, 0.8f)));
    g.setColour (ivory); g.strokePath (arc, juce::PathStrokeType (size (2.35f, 0.75f)));
    const auto ready = frame.status == metric_needle::Status::ready && std::isfinite (frame.median);
    const auto range = metric_needle::extent (frame.face);
    const int groups = frame.face == metric_needle::Face::sharp ? 6 : 8;
    const int sub = chassis.getWidth() <= 300 ? 2 : 3;
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
            drawCandidateText (g, text, box, context, typography::TextRole::axis, zero ? COL_FLORA_BR : ivory);
        }
    }
    const auto endMark = [&] (double value, juce::Colour colour)
    {
        if (const auto edge = metric_needle::rangeEdge (frame.face, value); edge != 0)
        {
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (point (edge < 0 ? 0.0f : 1.0f)));
        }
    };
    if (ready && needle)
    {
        const auto line = candidateNeedleLine (dial, *needle, 1.11f, capRadius);
        const auto colour = isPre ? COL_SPECTRUM_PRE : COL_FLORA_BR;
        g.setColour (colour.withAlpha (0.23f)); g.drawLine (line, size (5.2f, 1.5f));
        g.setColour (colour.brighter (0.15f)); g.drawLine (line, size (2.7f, 0.95f));
        endMark (difference, COL_FLORA_BR);
    }
    // FOLLOW A, measured position and matching definition are checked upstream. Missing REF
    // remains absent; drawing a needle at zero would invent a matched comparison.
    if (ownsReference && facts.referencePosition)
    {
        g.setColour (COL_SPECTRUM_DELTA);
        g.drawLine (candidateNeedleLine (dial, *facts.referencePosition, 1.0f, capRadius), size (2.1f, 1.0f));
        endMark (facts.reference, COL_SPECTRUM_DELTA);
    }
}

inline juce::String candidateSignedNumber (double value)
{
    if (! std::isfinite (value)) return juce::String::charToString (0x2014);
    return (value > 0 ? "+" : "") + juce::String (value, 2);
}

inline void paintCandidateDetails (juce::Graphics& g, juce::Rectangle<int> chassis,
    const metric_needle::Frame& frame, const metric_needle::Reading& facts,
    bool one, presentation::Context context)
{
    const auto lineHeight = juce::jlimit (12, 24, juce::roundToInt (chassis.getHeight() * 0.047f));
    const auto fitted = [&] (float x, float y, float width) {
        return juce::Rectangle<int> (juce::roundToInt (chassis.getWidth() * width), lineHeight)
            .withCentre ({chassis.getX() + juce::roundToInt (chassis.getWidth() * x),
                          chassis.getY() + juce::roundToInt (chassis.getHeight() * y)});
    };
    const auto drawDetail = [&] (const juce::String& text, juce::Rectangle<int> box, juce::Colour colour) {
        drawCandidateText (g, text, box, context, typography::TextRole::legend, colour, text_style::LabelPolicy::fixed);
    };
    drawDetail ("PRE " + candidateSignedNumber (one ? facts.own : facts.pre),
                fitted (0.145f, 0.615f, 0.24f), COL_SPECTRUM_PRE);
    drawDetail ("REF " + candidateSignedNumber (facts.reference),
                fitted (one ? 0.855f : 0.50f, 0.615f, 0.24f), COL_SPECTRUM_DELTA);
    if (! one)
        drawDetail ("POST " + candidateSignedNumber (facts.own), fitted (0.855f, 0.615f, 0.24f), COL_FLORA_BR);
    const auto summary = fitted (0.5f, 0.685f, 0.90f);
    const bool ready = frame.status == metric_needle::Status::ready && std::isfinite (frame.median);
    const auto song = juce::String::fromUTF8 (frame.song.c_str());
    if (ready)
    {
        auto songBox = summary;
        const auto zeroBox = song.isEmpty() ? summary : songBox.removeFromLeft (summary.getWidth() * 2 / 5);
        drawDetail ("0 = " + juce::String (frame.median, 2) + " " + metric_needle::unit (frame.face), zeroBox, COL_NORMAL);
        if (song.isNotEmpty()) drawDetail (song, songBox, COL_TEXT_SECONDARY);
    }
    else if (frame.status != metric_needle::Status::measuring && ! frame.reason.empty())
        drawDetail (text_style::shownText (metric_needle::reasonText (frame.reason)), summary, COL_NORMAL);
    else if (song.isNotEmpty())
        drawDetail (song, summary, COL_TEXT_SECONDARY);
}

inline juce::Rectangle<int> candidateHeadlineBounds (juce::Rectangle<int> chassis, bool singleReady)
{
    if (singleReady)
        return {chassis.getX() + juce::roundToInt (chassis.getWidth() * 0.045f),
                chassis.getY() + juce::roundToInt (chassis.getHeight() * 0.495f),
                juce::roundToInt (chassis.getWidth() * 0.185f), 18};
    return chassis.withTrimmedTop (juce::roundToInt (chassis.getHeight() * 0.31f))
        .removeFromTop (18).withSizeKeepingCentre (chassis.getWidth() / 4, 18);
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
                            own ? facts.own : facts.pre, isPre, own, facts, cap, context);
    }
    paintCandidateDetails (g, chassis, frame, facts, one, context);
    const auto headline = frame.status == metric_needle::Status::measuring ? juce::String ("PREPARING")
        : frame.status != metric_needle::Status::ready || ! std::isfinite (frame.median) ? juce::String ("UNAVAILABLE")
        : juce::String (metric_needle::label (frame.face));
    const bool ready = frame.status == metric_needle::Status::ready && std::isfinite (frame.median);
    const auto line = candidateHeadlineBounds (chassis, one && ready);
    drawCandidateText (g, headline, line, context, typography::TextRole::legend, COL_NORMAL);
    return true;
}
}
