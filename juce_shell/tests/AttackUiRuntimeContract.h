#pragma once
#include "../src/HyphaAttackSnapshotEquality.h"

namespace hypha::attack_ui_test
{
inline bool verifyRedrawContract (const KirinAttackEventBatch& events,
    const KirinAttackWaveformBatch& waveform, const KirinAttackDetailBatch& details,
    const KirinAttackPairEventBatch& pairs, const KirinAttackStats& stats)
{
    auto component = std::make_unique<AttackComponent>();
    auto post = std::make_unique<KirinAttackDetailBatch> (details);
    auto pre = std::make_unique<KirinAttackDetailBatch> (details);
    const auto submit = [&] {
        return component->setSnapshot (events, waveform, *post, waveform, *pre, pairs,
                                       288'000, 48'000, 7, stats); };
    if (! submit() || submit()) return false;
    post->details[1].shape[31] += .001f;
    if (! submit() || submit()) return false;
    pre->details[1].sharpness_acum += .01f;
    if (! submit() || submit()) return false;
    post->details[1].sharpness_acum = std::numeric_limits<float>::quiet_NaN();
    if (! submit() || submit()) return false;
    post->details[1].reserved = 91;
    post->details[1].reserved2 = 551;
    if (submit()) return false;
    component->presentationTick (false);
    if (submit()) return false;
    component->clearSnapshot();
    if (! submit()) return false;
    auto a = details.details[0], b = a;
    a.event_sample = INT64_C(9007199254740992); b.event_sample = a.event_sample + 1;
    return ! attack_equality::same (a, b);
}

inline bool verifyFanCache()
{
    auto cache = std::make_unique<attack_overview_glyph::Cache>();
    const attack_specimen::FeatureAmounts pre { .2f, .4f, .6f, .8f }, post { .8f, .6f, .4f, .2f };
    const auto first = cache->lookup (pre, post, true, 88, 40, 1.0f);
    if (! first.isValid() || cache->builds() != 1 || cache->bytes() != 88 * 40 * 4) return false;
    cache->lookup (pre, post, true, 88, 40, 1.0f);
    if (cache->builds() != 1) return false;
    for (float dpi : { 1.25f, 1.5f, 2.0f })
        if (! cache->lookup (pre, post, true, 88, 40, dpi).isValid()) return false;
    if (cache->builds() != 4) return false;
    // Cached compositing must keep the gradient, including when the caller left a faint fill.
    for (const auto dpi : { 1.0f, 2.0f })
    for (const bool focus : { false, true })
    {
        const auto render = [&] (attack_overview_glyph::Cache* retained) {
            juce::Image image (juce::Image::ARGB, static_cast<int> (88 * dpi),
                                static_cast<int> (40 * dpi), true);
            { juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (dpi));
              g.setOpacity (.05f);
              if (focus) {
                  attack_fan::Motion motion; motion.bend.fill (.18f);
                  attack_overview_glyph::drawFocus (g, { 0, 0, 88, 40 }, pre, post, true, motion, retained);
              } else attack_overview_glyph::drawComparison (g, { 0, 0, 88, 40 }, pre, post, retained); }
            return image;
        };
        const auto direct = render (nullptr), cached = render (cache.get());
        std::uint64_t difference = 0, ink = 0, colourError = 0, colourInk = 0;
        for (int y = 0; y < direct.getHeight(); ++y)
            for (int x = 0; x < direct.getWidth(); ++x)
            {
                const auto a = direct.getPixelAt (x, y), b = cached.getPixelAt (x, y);
                difference += static_cast<unsigned> (std::abs (a.getAlpha() - b.getAlpha()));
                ink += a.getAlpha();
                const std::array<int, 3> ac { a.getRed(), a.getGreen(), a.getBlue() };
                const std::array<int, 3> bc { b.getRed(), b.getGreen(), b.getBlue() };
                for (std::size_t c = 0; c < ac.size(); ++c)
                {
                    colourError += static_cast<unsigned> (std::abs (ac[c] * a.getAlpha() - bc[c] * b.getAlpha()));
                    colourInk += static_cast<unsigned> (ac[c] * a.getAlpha());
                }
            }
        if (ink == 0 || difference > ink / 20 || colourError > colourInk / 20) return false;
    }
    if (cache->lookup (pre, post, true, 88, 40, std::numeric_limits<float>::quiet_NaN()).isValid())
        return false;
    for (int i = 0; i < 540; ++i)
    {
        auto varied = post; varied.strength = static_cast<float> (i) / 540;
        if (! cache->lookup (pre, varied, true, 30, 12, 1.25f).isValid()
            || cache->bytes() > attack_overview_glyph::Cache::byteBudget) return false;
    }
    for (int i = 0; i < 12; ++i)
    {
        auto varied = post; varied.texture = static_cast<float> (i) / 12;
        if (! cache->lookup (pre, varied, true, 200, 100, 4.0f).isValid()
            || cache->bytes() > attack_overview_glyph::Cache::byteBudget) return false;
    }
    if (cache->bytes() == 0) return false;
    auto focusCache = std::make_unique<attack_overview_glyph::Cache>();
    attack_fan::Motion still, tiny, changed;
    tiny.bend.fill (.0001f); changed.bend.fill (.03f);
    if (! focusCache->lookup (pre, post, true, 400, 100, 2, false, &still).isValid()
        || ! focusCache->lookup (pre, post, true, 400, 100, 2, false, &tiny).isValid()
        || focusCache->builds() != 1
        || ! focusCache->lookup (pre, post, true, 400, 100, 2, false, &changed).isValid()
        || focusCache->builds() != 2) return false;
    changed.bend[0] = std::numeric_limits<float>::quiet_NaN();
    if (focusCache->lookup (pre, post, true, 400, 100, 2, false, &changed).isValid()) return false;
    auto twoLanes = std::make_unique<attack_overview_glyph::Cache>();
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < 480; ++i)
        {
            auto varied = post; varied.strength = static_cast<float> (i) / 480;
            if (! twoLanes->lookup ({}, varied, false, 20, 9, 2).isValid()) return false;
        }
    return twoLanes->builds() == 480 && twoLanes->bytes() <= attack_overview_glyph::Cache::byteBudget;
}
}
