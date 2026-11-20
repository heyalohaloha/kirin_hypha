#pragma once
#include "CompilerVersionString.h"
// Deterministic paired FULL/LR replay; array/pixel evidence, not a timing/CPU verdict.
#include "SpectrumPacedBenchmark.h"
#include <array>
namespace hypha::tests::freq_pixels
{
using namespace freq_paced;
struct FrameEvidence { juce::String arraysHash; juce::var arrays; };
inline FrameEvidence arraysFor(const SpectrumComponent& view,const KirinSpectrumView& input) {
    check(view.presentedEndpointForTest()==input.presentation_end_samples,"replay did not present expected endpoint");
    juce::MemoryOutputStream bytes; juce::Array<juce::var> pre,post,delta,valid,rawPre,rawPost,rawDelta;
    bytes.writeInt64(view.presentedEndpointForTest());
    int validCount=0;
    for(size_t i=0;i<KIRIN_SPECTRUM_BAND_COUNT;++i) {
        const float a=view.displayedPreForTest(i),b=view.displayedPostForTest(i),d=view.displayedDeltaForTest(i);
        const bool v=view.displayedDeltaValidForTest(i);
        check(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(d),"nonfinite display array cannot prove equivalence");
        bytes.writeFloat(a); bytes.writeFloat(b); bytes.writeFloat(d); bytes.writeByte(v?1:0);
        pre.add(a); post.add(b); delta.add(d); valid.add(v); validCount+=v;
        rawPre.add(input.pre_dbfs[i]); rawPost.add(input.post_dbfs[i]); rawDelta.add(input.display_db[i]);
    }
    check(validCount==KIRIN_SPECTRUM_BAND_COUNT,"paired replay contains missing delta bins");
    auto result=object(); set(result,"endpoint",static_cast<juce::int64>(view.presentedEndpointForTest()));
    set(result,"displayed_pre",pre); set(result,"displayed_post",post); set(result,"displayed_delta",delta); set(result,"delta_valid",valid);
    set(result,"input_pre",rawPre); set(result,"input_post",rawPost); set(result,"input_delta",rawDelta);
    return {juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString(),result};
}
inline juce::String rgbaHash(const juce::Image& image) {
    juce::MemoryOutputStream bytes; bytes.writeInt(image.getWidth()); bytes.writeInt(image.getHeight());
    for(int y=0;y<image.getHeight();++y) for(int x=0;x<image.getWidth();++x) {
        const auto c=image.getPixelAt(x,y);
        bytes.writeByte(static_cast<char>(c.getRed())); bytes.writeByte(static_cast<char>(c.getGreen()));
        bytes.writeByte(static_cast<char>(c.getBlue())); bytes.writeByte(static_cast<char>(c.getAlpha()));
    }
    return juce::SHA256(bytes.getData(),bytes.getDataSize()).toHexString();
}
inline void savePng(const juce::File& file,const juce::Image& image) {
    check(!file.exists(),"replay refuses to replace image"); auto stream=file.createOutputStream();
    check(stream && juce::PNGImageFormat().writeImageToStream(image,*stream),"pixel replay PNG write failed");
}
struct Picture { int index=0; juce::Image image; juce::String rgba; };
struct Pass { std::vector<juce::String> arrays; std::vector<Picture> pictures; };
inline bool checkpoint(int index) {
    if(index==0) return true; // cold initial frame as well as measured checkpoints
    for(int measured:{0,1,29,30,299,599,899}) if(index==warmupFrames+measured) return true;
    return false;
}
inline Pass replay(const Input& input,int width,int pass,const juce::File& directory,const juce::String& sourceId) {
    material_cache::Lifetime material; SpectrumComponent view;
    view.setPresentationContext(presentation::forEditor(width,width*2/3)); view.setSize(width,width*2/3);
    view.setAbsoluteObservation(false); view.setLowScope(false); view.setDisplaySettings(display_settings::Values{});
    view.setAverageChoice(SpectrumComponent::Average::live); view.setSignalActive(true); view.setAnimationVisible(false);
    check(!view.frameDriverActiveForTest() && !view.isAbsoluteObservationForTest() && !view.isPsbObservationForTest()
        && !view.isShapeObservationForTest(),"unexpected replay mode or live clock");
    view.queueSnapshot(input.frames.front(),1);
    const double base=view.curveUpdateTimeForTest();
    check(std::isfinite(base),"invalid initial presentation clock");
    juce::Image image(juce::Image::ARGB,width,width*2/3,true); Pass out; out.arrays.reserve(input.frames.size());
    juce::Array<juce::var> allFrames;
    for(size_t i=0;i<input.frames.size();++i) {
        if(i>0) { view.queueSnapshot(input.frames[i],1); view.presentationTickAt(base+static_cast<double>(i)*1000.0/fps); }
        image.clear(image.getBounds(),BG); juce::Graphics graphics(image); view.paintEntireComponent(graphics,true);
        const auto evidence=arraysFor(view,input.frames[i]); out.arrays.push_back(evidence.arraysHash);
        auto record=object(); set(record,"input_index",static_cast<int>(i)); set(record,"relative_tick_ms",static_cast<double>(i)*1000.0/fps);
        set(record,"endpoint",static_cast<juce::int64>(input.frames[i].presentation_end_samples)); set(record,"arrays_sha256",evidence.arraysHash);
        if(checkpoint(static_cast<int>(i))) {
            const auto prefix=juce::String(width)+"-pass-"+juce::String(pass)+"-input-"+juce::String(static_cast<int>(i));
            const auto hash=rgbaHash(image); set(record,"rgba_sha256",hash); set(record,"arrays",evidence.arrays);
            savePng(directory.getChildFile(prefix+".png"),image);
            out.pictures.push_back({static_cast<int>(i),image.createCopy(),hash});
        }
        allFrames.add(record);
    }
    check(out.arrays.size()==960 && out.pictures.size()==8,"replay must traverse all frames and all checkpoints");
    check(!view.frameDriverActiveForTest(),"unexpected VBlank attachment during replay");
    auto manifest=object(); set(manifest,"source_id_caller_supplied",sourceId); set(manifest,"fixture_sha256",input.sha256);
    set(manifest,"width",width); set(manifest,"height",width*2/3); set(manifest,"scale",1); set(manifest,"pass",pass);
    set(manifest,"mode","paired-full-LR-AVG-LIVE-default-settings-no-hover"); set(manifest,"language","en");
    set(manifest,"os",juce::SystemStats::getOperatingSystemName()); set(manifest,"compiler",hypha::tests::compilerVersionString());
    set(manifest,"juce_version",juce::SystemStats::getJUCEVersion()); set(manifest,"initial_clock_ms",base);
    set(manifest,"frames",allFrames); set(manifest,"pixel_checkpoints",8);
    set(manifest,"scope","960 array hashes, 8 actual RGBA checkpoints; no CPU claim; not absolute/stop/rearm");
    saveJson(directory.getChildFile(juce::String(width)+"-pass-"+juce::String(pass)+".json"),manifest);
    return out;
}
inline juce::var compareImage(const Picture& a,const Picture& b,const juce::File& difference) {
    check(a.index==b.index && a.image.getBounds()==b.image.getBounds(),"pixel evidence mismatched dimensions or frame");
    int changed=0,maxChannel=0; int64_t rgbSum=0,rgbaSum=0;
    juce::Image diff(juce::Image::ARGB,a.image.getWidth(),a.image.getHeight(),true);
    for(int y=0;y<a.image.getHeight();++y) for(int x=0;x<a.image.getWidth();++x) {
        const auto left=a.image.getPixelAt(x,y),right=b.image.getPixelAt(x,y);
        const int r=std::abs(int(left.getRed())-int(right.getRed())),g=std::abs(int(left.getGreen())-int(right.getGreen()));
        const int blue=std::abs(int(left.getBlue())-int(right.getBlue())),alpha=std::abs(int(left.getAlpha())-int(right.getAlpha()));
        changed+=(r+g+blue+alpha)>0; rgbSum+=r+g+blue; rgbaSum+=r+g+blue+alpha;
        maxChannel=std::max(maxChannel,std::max({r,g,blue,alpha}));
        // RGB difference image; alpha-only differences are flagged magenta.
        diff.setPixelAt(x,y,juce::Colour(static_cast<juce::uint8>(std::max(r,alpha)),static_cast<juce::uint8>(g),static_cast<juce::uint8>(std::max(blue,alpha))));
    }
    if(changed) savePng(difference,diff);
    auto result=object(); set(result,"input_index",a.index); set(result,"different_pixels",changed);
    set(result,"rgba_sum",static_cast<juce::int64>(rgbaSum)); set(result,"rgb_sum",static_cast<juce::int64>(rgbSum));
    set(result,"max_channel_difference",maxChannel); set(result,"rgba_sha256_a",a.rgba); set(result,"rgba_sha256_b",b.rgba);
    set(result,"exact",changed==0 && a.rgba==b.rgba); return result;
}
inline int main(const juce::File& directory,const juce::String& sourceId) {
    check(juce::MessageManager::getInstance()->isThisTheMessageThread(),"replay requires GUI thread");
    check(sourceId.isNotEmpty() && !directory.exists(),"source ID and fresh replay directory required");
    check(directory.createDirectory().wasOk(),"cannot create replay directory");
    const i18n::ScopedLanguage language(i18n::Language::english); const auto input=makeInput();
    bool all=true; juce::Array<juce::var> sizes;
    for(int width:{300,600,900}) {
        const auto first=replay(input,width,1,directory,sourceId),second=replay(input,width,2,directory,sourceId);
        check(first.arrays.size()==960 && second.arrays.size()==960 && first.pictures.size()==second.pictures.size(),"incomplete self-comparison");
        juce::Array<juce::var> mismatches,pictures; bool exact=true;
        for(size_t i=0;i<first.arrays.size();++i) if(first.arrays[i]!=second.arrays[i]) { mismatches.add(static_cast<int>(i)); exact=false; }
        for(size_t i=0;i<first.pictures.size();++i) {
            auto result=compareImage(first.pictures[i],second.pictures[i],directory.getChildFile(juce::String(width)+"-diff-input-"+juce::String(first.pictures[i].index)+".png"));
            exact=static_cast<bool>(result.getProperty("exact",false)) && exact; pictures.add(result);
        }
        auto result=object(); set(result,"width",width); set(result,"array_mismatch_indices",mismatches);
        set(result,"pictures",pictures); set(result,"exact",exact); sizes.add(result); all=all&&exact;
        std::cout<<"FREQ_PIXEL_SELF width="<<width<<" arrays=960 pictures=8 exact="<<exact<<std::endl;
    }
    auto result=object(); set(result,"sizes",sizes); set(result,"exact_self_replay",all);
    set(result,"claim","same binary self-replay only, not baseline/candidate equivalence or CPU performance");
    saveJson(directory.getChildFile("self-comparison.json"),result); return all?0:2;
}
}
