#pragma once

#include <cmath>
#include "../src/HyphaAttackEnvelopeGeometry.h"
#include <cstdint>

namespace hypha::attack_ui_test
{
inline KirinAttackDetail overviewDetail()
{
    KirinAttackDetail detail {};
    detail.sample_rate = 48'000;
    detail.channels = 2;
    detail.event_sample = 144'000;
    detail.shape_start_sample = detail.event_sample - 4'800;
    detail.shape_end_sample = detail.event_sample + 1'440;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.attack_rms_dbfs = attack_ui::strengthGlowFullDbfs;
    detail.sharpness_available = 1;
    detail.sharpness_acum = attack_ui::brightnessGlowFullAcum;
    detail.contrast_db = attack_ui::transientGlowFullDb;
    detail.sample_edge_ratio_db = 0.0f;
    detail.crest_db = 0.0f;
    detail.peak_plateau_ms = 4.0f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
    {
        const auto distance = std::abs (static_cast<int> (index) - 74);
        detail.shape[index] = index < 74 ? 0.03f
            : 0.88f * std::exp (-static_cast<float> (distance) / 8.0f) + 0.02f;
    }
    return detail;
}

inline bool verifyMeasuredEnvelope()
{
    KirinAttackWaveformBatch batch {}; batch.count=6;
    for (std::uint32_t i=0;i<batch.count;++i) {
        auto& p=batch.points[i]; p.start_sample=48000+i*480;p.end_sample=p.start_sample+480;
        p.sample_rate=48000;p.channels=2;p.generation=7;p.rms_dbfs=i==2?-18.0f:-36.0f;
    }
    const juce::Rectangle<float> area {0,0,600,100};
    const auto base=attack_envelope::geometry (batch,area,0,288000,48000);
    const auto bounds=base.body.getBounds();
    if (std::abs (bounds.getX()-100)>0.001f || std::abs (bounds.getRight()-106)>0.001f
        || std::abs (bounds.getHeight()-73.5f)>0.001f) return false;
    batch.points[3].start_sample+=240;
    const auto gap=attack_envelope::geometry (batch,area,0,288000,48000);
    if (gap.body.contains (103.25f,50) || ! gap.body.contains (104.5f,50)) return false;
    batch.points[3].start_sample-=240;
    batch.points[3].rms_dbfs=std::numeric_limits<float>::quiet_NaN();
    if (attack_envelope::geometry (batch,area,0,288000,48000).body.contains (103.5f,50)) return false;
    for (auto& p:batch.points)p.rms_dbfs=-120;
    if (! attack_envelope::geometry (batch,area,0,288000,48000).body.isEmpty()) return false;
    for (auto& p:batch.points)p.rms_dbfs=-36;
    if (! attack_envelope::geometry (batch,area,0,288000,44100).body.isEmpty()) return false;
    const auto clipped=attack_envelope::geometry (batch,area,48240,49200,48000).body.getBounds();
    if (std::abs (clipped.getX())>.001f || std::abs (clipped.getRight()-600)>.001f) return false;
    const auto original=attack_envelope::geometry (batch,area,0,288000,48000);
    constexpr auto huge=INT64_C(9007199254740993);
    for (auto& p:batch.points){p.start_sample+=huge;p.end_sample+=huge;}
    return original.body == attack_envelope::geometry (batch,area,huge,huge+288000,48000).body;
}
inline bool verifyUpperFeatureIsolation (const KirinAttackEventBatch& events,
    const KirinAttackWaveformBatch& waveform, const KirinAttackDetailBatch& details,
    const KirinAttackPairEventBatch& pairs, const KirinAttackStats& stats)
{
    auto component=std::make_unique<AttackComponent>();
    auto changed=std::make_unique<KirinAttackDetailBatch> (details);
    component->setSize (880,480);
    const auto draw=[&] {
        component->setSnapshot (events,waveform,*changed,waveform,details,pairs,288000,48000,7,stats);
        juce::Image image (juce::Image::ARGB,880,480,true);juce::Graphics g (image);
        component->paintEntireComponent (g,true);
        return image;
    };
    const auto before=draw();
    for (std::uint32_t i=0;i<changed->count;++i) {
        auto& d=changed->details[i]; d.sharpness_acum=0;d.attack_rms_dbfs=-70;
        d.contrast_db=0;d.sample_edge_ratio_db=-24;
    }
    const auto after=draw();
    for (int y=0;y<480-attack_ui::metricsHeight (480);++y)
        for (int x=0;x<880;++x)
            if (before.getPixelAt (x,y)!=after.getPixelAt (x,y)) return false;
    return specimenDifferences (before,after)>100;
}
inline bool verifyEnvelopeSimplificationBound()
{
    KirinAttackWaveformBatch batch {};batch.count=600;
    for (std::uint32_t i=0;i<batch.count;++i) {
        auto& p=batch.points[i];p.start_sample=i*480;p.end_sample=p.start_sample+480;
        p.sample_rate=48000;p.channels=2;p.rms_dbfs=-36+20*std::sin (static_cast<float> (i)*.08f);
    }
    for (const auto dpi:{1.0f,1.25f,2.0f,4.0f}) {
        const auto shape=attack_envelope::geometry (batch,{0,0,900,180},0,288000,48000,.05f/dpi);
        for (std::uint32_t i=0;i<batch.count;++i) {
            const juce::Point<float> measured {(static_cast<float> (i)+.5f)*1.5f,
                90-(batch.points[i].rms_dbfs+72)/72*89};
            juce::Point<float> previous;
            float closest=10000;
            juce::Path::Iterator path (shape.edge);
            while (path.next()) {
                const juce::Point<float> point {path.x1,path.y1};
                if (path.elementType==juce::Path::Iterator::lineTo) {
                    const auto direction=point-previous;
                    const auto length=direction.getDistanceSquaredFromOrigin();
                    const auto v=measured-previous;
                    const auto fraction=length>0?juce::jlimit (0.0f,1.0f,(v.x*direction.x+v.y*direction.y)/length):0;
                    closest=std::min (closest,measured.getDistanceFrom (previous+direction*fraction));
                }
                previous=point;
            }
            if (closest*dpi>.0502f)return false;
        }
    }
    return true;
}
inline bool verifyEnvelopeRaster()
{
    KirinAttackWaveformBatch batch {}; batch.count=10;
    for (std::uint32_t i=0; i<batch.count; ++i) {
        auto& point=batch.points[i];point.start_sample=i*480;point.end_sample=point.start_sample+480;
        point.sample_rate=48000;point.channels=2;point.rms_dbfs=-18;
    }
    batch.points[4].rms_dbfs=std::numeric_limits<float>::quiet_NaN();
    // Odd dimensions exercise fractional-DPI padding; the large width exercises vector fallback.
    for (auto width : {203,4097}) for (auto dpi : {1.0f,1.25f,2.0f,4.0f}) {
        const juce::Rectangle<int> area {4,4,width,width==203?101:12};
        const auto draw=[&] (float inheritedOpacity, bool empty) {
            juce::Image image (juce::Image::ARGB,static_cast<int> (std::ceil ((width+8)*dpi)),
                              static_cast<int> (std::ceil ((area.getHeight()+8)*dpi)),true);
            juce::Graphics g (image);g.addTransform (juce::AffineTransform::scale (dpi));
            g.setOpacity (inheritedOpacity);
            attack_painter::drawEnvelope (g,empty?KirinAttackWaveformBatch{}:batch,area,0,4800,48000,
                attack_painter::WaveformStyle::continuous,1);
            return image;
        };
        const auto full=draw (1,false), inherited=draw (.02f,false), empty=draw (1,true);
        if (specimenDifferences (full,inherited)!=0 || specimenLight (empty)!=0) return false;
        const auto y=static_cast<int> ((area.getY()+area.getHeight()*.35f)*dpi);
        const auto at=[&] (float fraction) {
            return full.getPixelAt (static_cast<int> ((area.getX()+width*fraction)*dpi),y).getAlpha(); };
        if (at (.25f)==0 || at (.45f)!=0 || at (.65f)==0
            || full.getPixelAt (0,y).getAlpha()!=0) return false;
    }
    return true;
}
}
