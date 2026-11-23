#pragma once

#include "../src/HyphaMetricInstrumentCandidate.h"
#include <cstring>

namespace hypha::tests
{
inline bool sameNeedlePixels (const juce::Image& a, const juce::Image& b)
{
    const juce::Image::BitmapData aa (a, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData bb (b, juce::Image::BitmapData::readOnly);
    const auto bytes = static_cast<std::size_t> (aa.width * aa.pixelStride);
    for (int y = 0; y < aa.height; ++y)
        if (std::memcmp (aa.getLinePointer (y), bb.getLinePointer (y), bytes) != 0) return false;
    return true;
}

// Keep the frozen B-9325 VU operation as the independent oracle, without the new shared helper.
inline void frozenNeedleFinish (juce::Graphics& g, juce::Line<float> line, float width,
                                juce::Colour role, bool useVuFinish)
{
    const auto scale = [width] (float authored, float minimum)
    { return juce::jmax (minimum, authored * width / 900.0f); };
    g.setColour (role.withAlpha (useVuFinish ? 0.20f : 0.23f));
    g.drawLine (line, scale (5.2f, 1.5f));
    g.setColour (useVuFinish ? role.interpolatedWith (COL_NORMAL, 0.34f).brighter (0.09f)
                             : role.brighter (0.15f));
    g.drawLine (line, scale (2.7f, 0.95f));
}

inline void verifyCrestNeedleStyle (const metric_face::MetricChassisPair& mid,
                                    void (*check) (bool, const char*))
{
    namespace mf = metric_face;
    namespace vu = hybrid_vu::needle_painter;
    using metric_needle::Face;
    using observatory::Role;
    for (const int width : {300, 600, 900})
    {
        // 450% is the 900x600 logical editor rendered through a 1.5 transform, not a 1350 logical layout.
        const auto affine = width == 900 ? 1.5f : 1.0f;
        const juce::Rectangle<int> chassis (width, width * 2 / 3);
        const auto face = vu::faceBounds (chassis);
        const auto render = [&] (auto paint) {
            juce::Image image (juce::Image::RGB, juce::roundToInt (affine * width),
                               juce::roundToInt (affine * chassis.getHeight()), true);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (affine));
            paint (g);
            return image;
        };
        for (const auto at : {0.12f, 0.5f, 0.83f})
        {
            const auto actual = render ([&] (auto& g) { vu::paintPair (g, chassis, at, 1.0f - at); });
            const auto frozen = render ([&] (auto& g) {
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto dial = vu::dialGeometry (face, channel);
                    const auto p = vu::dialPoint (dial, channel == 0 ? at : 1.0f - at);
                    frozenNeedleFinish (g, { dial.pivot, dial.pivot + (p - dial.pivot) * 1.11f },
                                        face.getWidth(), COL_FLORA_BR, true);
                }
            });
            check (sameNeedlePixels (actual, frozen), "VU needle pixels remain exactly as before");
        }
        for (const auto role : {Role::pre, Role::post})
            for (int slot = 0; slot < mf::dialCount (role); ++slot)
                for (const auto metric : {Face::crest, Face::psr, Face::sharp})
                {
                    if (role == Role::pre && metric == Face::sharp) continue;
                    const bool one = role == Role::pre, isPre = one || slot == 0, owns = one || slot == 1;
                    const auto dial = mf::candidateDial (chassis, mid, role, slot);
                    const auto cap = (one ? mid.pre.frontCapRadius3072 : 57.0f) * width / 3072.0f + 1.5f;
                    const auto context = presentation::forEditor (width, chassis.getHeight());
                    const auto range = metric_needle::extent (metric);
                    metric_needle::Frame frame;
                    frame.face = metric;
                    frame.status = metric_needle::Status::ready;
                    frame.median = 10.0;
                    metric_needle::Reading facts;
                    facts.relativeBand = metric_needle::Band { -0.4 * range, 0.3 * range };
                    facts.reference = -0.55 * range;
                    facts.referencePosition = metric_needle::normalized (metric, facts.reference);
                    const auto difference = (isPre ? -0.25 : 0.45) * range;
                    const auto at = *metric_needle::normalized (metric, difference);
                    const auto colour = isPre ? COL_SPECTRUM_PRE : COL_FLORA_BR;
                    const auto body = mf::candidateNeedleLine (dial, at, 1.11f, cap);
                    const auto actual = render ([&] (auto& g) {
                        mf::paintCandidateDial (g, chassis, dial, frame, at, difference, isPre, owns, facts, cap, context);
                    });
                    const auto expected = [&] (bool useVuFinish) { return render ([&] (auto& g) {
                        auto empty = facts;
                        empty.referencePosition.reset();
                        mf::paintCandidateDial (g, chassis, dial, frame, std::nullopt, difference,
                                                isPre, owns, empty, cap, context);
                        frozenNeedleFinish (g, body, face.getWidth(), colour, useVuFinish);
                        if (owns)
                        {
                            g.setColour (COL_SPECTRUM_DELTA);
                            g.drawLine (mf::candidateNeedleLine (dial, *facts.referencePosition, 1.0f, cap),
                                        juce::jmax (1.0f, face.getWidth() * 2.1f / 900.0f));
                        }
                    }); };
                    check (sameNeedlePixels (actual, expected (metric == Face::crest)),
                           "only the CREST main needle adopts the VU finish; REF retains its own line");
                    if (metric == Face::crest)
                        check (! sameNeedlePixels (actual, expected (false)),
                               "the CREST finish change must be visible in rendered pixels");
                }
    }
}
}
