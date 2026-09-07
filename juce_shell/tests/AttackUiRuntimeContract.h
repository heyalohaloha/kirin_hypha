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

inline bool verifyFocusCache()
{
    auto cache = std::make_unique<attack_focus::Cache>();
    const attack_specimen::FeatureAmounts pre {.2f,.4f,.6f,.8f}, post {.8f,.6f,.4f,.2f};
    attack_motion::Motion still, tiny, changed;
    tiny.bend.fill (.0001f); changed.bend.fill (.03f);
    if (! cache->lookup (pre,post,true,400,100,2,still).isValid()
        || ! cache->lookup (pre,post,true,400,100,2,tiny).isValid() || cache->builds()!=1
        || ! cache->lookup (pre,post,true,400,100,2,changed).isValid() || cache->builds()!=2) return false;
    for (auto dpi : {1.0f,1.25f,2.0f,4.0f}) {
        const auto result=cache->lookup (pre,post,true,200,100,dpi,still);
        if (! result.isValid() || cache->bytes()!=static_cast<std::size_t> (result.getWidth()*result.getHeight()*4))
            return false;
    }
    for (int i=0;i<120;++i) {
        auto varied=post;varied.strength=static_cast<float> (i)/120;
        if (! cache->lookup (pre,varied,true,400,120,2,still).isValid()
            || cache->bytes()!=400*120*4*4) return false;
    }
    for (auto dpi : {1.0f,1.25f,2.0f,4.0f}) {
        juce::Image direct (juce::Image::ARGB,static_cast<int> (std::ceil (203*dpi)),static_cast<int> (std::ceil (101*dpi)),true);
        juce::Image cached=direct.createCopy();
        {juce::Graphics g (direct);g.addTransform (juce::AffineTransform::scale (dpi));g.setOpacity (.05f);
         attack_focus::drawFocus (g,{0,0,203,101},pre,post,true,changed);}
        {juce::Graphics g (cached);g.addTransform (juce::AffineTransform::scale (dpi));g.setOpacity (.05f);
         attack_focus::drawFocus (g,{0,0,203,101},pre,post,true,changed,cache.get());}
        std::uint64_t error=0,ink=0;
        for (int y=0;y<direct.getHeight();++y) for (int x=0;x<direct.getWidth();++x) {
            const auto a=direct.getPixelAt (x,y),b=cached.getPixelAt (x,y);
            for (auto pair : {std::pair{a.getRed(),b.getRed()},std::pair{a.getGreen(),b.getGreen()},
                              std::pair{a.getBlue(),b.getBlue()},std::pair{a.getAlpha(),b.getAlpha()}}) {
                error+=static_cast<unsigned> (std::abs (pair.first*a.getAlpha()-pair.second*b.getAlpha()));
                ink+=static_cast<unsigned> (pair.first*a.getAlpha());
            }
        }
        if (ink==0 || error>ink/20) return false;
    }
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    changed.bend[0]=nan;
    return ! cache->lookup (pre,post,true,400,100,2,changed).isValid()
        && ! cache->lookup (pre,post,true,400,100,nan,still).isValid()
        && ! cache->lookup (pre,post,true,1024,512,4,still).isValid()
        && ! cache->lookup (pre,post,true,0,100,1,still).isValid()
        && cache->bytes()<=attack_focus::Cache::byteBudget;
}
}
