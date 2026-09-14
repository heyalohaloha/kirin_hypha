#include "ReferenceCaptureEvidence.h"
#include "ReferenceRuntimeACapture.h"
#include <cmath>
#include <cstring>
namespace hypha::reference_audition
{
bool CaptureBindingReceipt::valid() const
{
    const auto hash=[](const juce::String& v){return v.length()==64 && v.containsOnly("0123456789abcdef");};
    constexpr auto bound=std::numeric_limits<std::int64_t>::max()/4;
    for(auto v:{hostAnchor,sourceAnchor,probeStart,probeEnd}) if(v < -bound || v>bound) return false;
    if(captureId.length()!=36 || juce::Uuid(captureId).isNull() || !hash(sourceHash) || (!sourcePcmHash.isEmpty() && !hash(sourcePcmHash))
        || !hash(calibrationHash) || work.length()>160 || rate<8000 || rate>768000 || channels<1 || channels>2
        || !revision || policy!=1 || probeEnd-probeStart!=std::int64_t(rate)*4) return false;
    if(gainKnown && (pairedBlocks<27 || !std::isfinite(fullGainDb) || !std::isfinite(displayGainDb)
        || !std::isfinite(aPeakDbtp) || !std::isfinite(bPeakDbtp) || !std::isfinite(ceilingDbtp)
        || std::abs(fullGainDb)>120 || (originalFallback ? std::abs(displayGainDb)>0 : (displayGainDb>fullGainDb || displayGainDb<fullGainDb)))) return false;
    return true;
}
bool captureDigestEqual(const KirinReferenceCaptureUnit& a,const KirinReferenceCaptureUnit& b) noexcept
{ return std::memcmp(a.digest,b.digest,sizeof(a.digest))==0; }
bool captureUniqueSequence(const std::vector<KirinReferenceCaptureUnit>& units,size_t first)
{
    if(first+4>units.size()) return false;
    int audible=0; for(size_t i=first;i<first+4;++i) if(std::max(units[i].rms[0],units[i].rms[1])>=0.00031622777f) ++audible;
    if(audible<3) return false;
    for(size_t at=0;at+4<=units.size();++at)
    {
        if(at==first) continue;
        bool same=true; for(size_t i=0;i<4;++i) if(!captureDigestEqual(units[at+i],units[first+i])) { same=false; break; }
        if(same) return false;
    }
    return true;
}
bool captureProbeMatches(const std::vector<KirinReferenceCaptureUnit>& units,std::int64_t hostStart,int rate,int channels,const RuntimeACaptureAudio& audio)
{
    if(rate<8000 || rate>768000 || channels<1 || channels>2 || audio.sampleRateHz!=rate || audio.channels!=channels
        || audio.frameCount!=std::int64_t(rate)*4 || audio.startSample<hostStart) return false;
    const auto offset=audio.startSample-hostStart;
    if(offset%rate || audio.interleaved.size()!=size_t(rate)*4*size_t(channels)) return false;
    const auto first=size_t(offset/rate); if(first+4>units.size()) return false;
    auto* index=kirin_reference_index_create(uint32_t(rate),uint32_t(channels),uint32_t(rate));
    if(!index) return false;
    bool same=true;
    for(size_t unit=0;unit<4 && same;++unit)
    {
        for(int at=0;at<rate;)
        {
            const int frames=std::min(8192,rate-at);
            if(!kirin_reference_index_push(index,audio.interleaved.data()+(unit*size_t(rate)+size_t(at))*size_t(channels),size_t(frames*channels))) { same=false; break; }
            at+=frames;
        }
        KirinReferenceCaptureUnit value{};
        same=same && kirin_reference_index_finish(index,&value)==uint32_t(rate) && captureDigestEqual(value,units[first+unit]);
    }
    kirin_reference_index_drop(index); return same;
}
}
