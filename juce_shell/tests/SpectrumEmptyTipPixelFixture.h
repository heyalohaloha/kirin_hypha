#pragma once
// Exercise actual paintCurves in each binary; no copied baseline implementation.
#include "SpectrumPairedPixelReplay.h"
#include "../src/HyphaSpectrumPainter.h"
#include "../src/HyphaSpectrumGeometry.h"
namespace hypha::tests::freq_empty_tip
{
using namespace freq_paced;
using namespace spectrum_painter;
struct Input { SpectrumBins pre{},post{},delta{},mark{}; SpectrumValidity valid{},markValid{}; };
inline Input inputFor(int stage) {
    const auto source=spectrumPerformanceFixture(); Input in;
    for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) {
        in.pre[i]=source.pre_dbfs[i]; in.delta[i]=source.display_db[i]; in.valid[i]=1;
        if(stage==1) in.valid[i]=(i>0 && i+1<KIRIN_SPECTRUM_BAND_COUNT && i%8<4)?1:0;
        if(stage==2) in.valid[i]=0;
        if(stage==4) in.delta[i]=0.0f;
        if(stage==5) in.delta[i]=0.1f;
        if(stage==6) in.delta[i]=0.749f;
        if(stage==7) in.delta[i]=0.751f;
        if(stage==8) { in.delta[i]=6.0f; in.valid[i]=(i==KIRIN_SPECTRUM_BAND_COUNT/2)?1:0; }
        in.post[i]=in.pre[i]+in.delta[i]; in.mark[i]=2.0f*std::sin(static_cast<float>(i)*.04f); in.markValid[i]=1;
    }
    return in;
}
inline juce::var inputJson(const Input& in) {
    auto out=object(); juce::Array<juce::var> pre,post,delta,valid,mark,markValid; juce::MemoryOutputStream bytes;
    for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) {
        check(std::isfinite(in.pre[i]) && std::isfinite(in.post[i]) && std::isfinite(in.delta[i]) && std::isfinite(in.mark[i]),"finite fixture values required");
        pre.add(in.pre[i]); post.add(in.post[i]); delta.add(in.delta[i]); valid.add(int(in.valid[i])); mark.add(in.mark[i]); markValid.add(int(in.markValid[i]));
        bytes.writeFloat(in.pre[i]); bytes.writeFloat(in.post[i]); bytes.writeFloat(in.delta[i]); bytes.writeByte(in.valid[i]);
        bytes.writeFloat(in.mark[i]); bytes.writeByte(in.markValid[i]);
    }
    set(out,"pre",pre); set(out,"post",post); set(out,"delta",delta); set(out,"valid",valid); set(out,"mark",mark); set(out,"mark_valid",markValid);
    set(out,"input_sha256",juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString()); return out;
}
inline int main(const juce::File& directory,const juce::String& sourceId) {
    check(juce::MessageManager::getInstance()->isThisTheMessageThread(),"fixture requires GUI thread");
    check(!directory.exists() && sourceId.isNotEmpty(),"fresh output and source ID required");
    check(directory.createDirectory().wasOk(),"fixture output directory failed");
    const i18n::ScopedLanguage language(i18n::Language::english);
    const char* names[]{"normal-before","invalid-gaps","all-invalid","normal-after","valid-zero","bucket-zero","below-boundary","above-boundary","isolated-valid"};
    juce::Array<juce::var> results; int images=0,cases=0;
    for(int width:{300,600,900}) for(int light=0;light<display_settings::choiceLimit;++light) {
        if(!display_settings::valid(display_settings::light,light)) continue;
        display_settings::Values settings; settings.selected[display_settings::light]=light;
        for(bool marked:{false,true}) {
            PathWorkspace paths; // Retained across the full normal -> gaps -> empty -> normal sequence.
            for(int stage=0;stage<9;++stage) {
                const auto input=inputFor(stage); auto evidence=inputJson(input);
                const int height=width*2/3;
                juce::Image image(juce::Image::ARGB,width,height,true,juce::NativeImageType());
                image.clear(image.getBounds(),BG); juce::Graphics graphics(image);
                const auto bounds=image.getBounds().toFloat();
                const auto plot=spectrum_geometry::dataPlotBoundsFor(bounds,false);
                paintCurves(graphics,plot,spectrum_geometry::visualScaleFor(bounds),input.pre,input.post,input.delta,input.valid,
                    marked?&input.mark:nullptr,marked?&input.markValid:nullptr,settings.rangeDb(),settings.brightness(),false,&paths);
                int nonempty=0,bucketZeroHighlights=0;
                for(size_t layer=0;layer<PathWorkspace::layers;++layer) for(size_t bucket=1;bucket<PathWorkspace::levels;++bucket)
                    nonempty+=!paths.existing(PathWorkspace::tipBase+layer*PathWorkspace::levels+bucket).isEmpty();
                bucketZeroHighlights=!paths.existing(PathWorkspace::highlightBase).isEmpty();
                if(stage==2 || stage==4 || stage==5 || stage==6 || stage==8)
                    check(nonempty==0,"boundary input did not produce genuinely empty tip paths");
                else check(nonempty>0,"nonempty reference stage did not exercise actual tip fill");
                if(stage==4 || stage==5 || stage==6) check(bucketZeroHighlights==1,"bucket-zero highlight path must exist");
                const auto prefix=juce::String(width)+"-light-"+juce::String(light)+"-mark-"+juce::String(marked?1:0)+"-"+names[stage];
                set(evidence,"case",prefix); set(evidence,"width",width); set(evidence,"height",height); set(evidence,"stage",stage);
                set(evidence,"light_choice",light); set(evidence,"brightness",settings.brightness()); set(evidence,"mark_enabled",marked);
                set(evidence,"tip_nonempty_paths",nonempty); set(evidence,"bucket0_highlight",bucketZeroHighlights);
                set(evidence,"range_db",settings.rangeDb()); set(evidence,"rich_post",false);
                set(evidence,"primary_rgba_sha256",freq_pixels::rgbaHash(image));
                freq_pixels::savePng(directory.getChildFile(prefix+".png"),image); ++images;
                // Keep precisely the outgoing Graphics state; no setColour, save/restore or new Graphics.
                // This separate diagnostic is not the primary picture.
                graphics.fillRect(1,height-5,4,4);
                set(evidence,"outgoing_probe_rgba_sha256",freq_pixels::rgbaHash(image));
                freq_pixels::savePng(directory.getChildFile(prefix+"-outgoing-probe.png"),image); ++images;
                results.add(evidence); ++cases;
            }
        }
    }
    check(cases==162 && images==324,"all sizes, brightness choices, MARK states and stages must be present");
    auto manifest=object(); set(manifest,"schema","hypha-empty-tip-pixels-v1"); set(manifest,"source_id_caller_supplied",sourceId);
    set(manifest,"backend","ARGB explicit NativeImageType, same fixed transform/clip and scale1");
    set(manifest,"os",juce::SystemStats::getOperatingSystemName()); set(manifest,"juce_version",juce::SystemStats::getJUCEVersion());
    set(manifest,"compiler",__VERSION__); set(manifest,"cases",results); set(manifest,"image_count",images);
    set(manifest,"scope","real paintCurves direct boundary inputs; no component smoothing; no baseline/candidate comparison or CPU verdict");
    saveJson(directory.getChildFile("manifest.json"),manifest);
    std::cout<<"FREQ_EMPTY_TIP_FIXTURE cases="<<cases<<" images="<<images<<" generated=1 comparison=NOT_RUN"<<std::endl;
    return 0;
}
}
