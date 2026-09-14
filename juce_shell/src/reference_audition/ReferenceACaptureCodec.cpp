#include "ReferenceACaptureEvidenceCodec.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cmath>
#include <cstring>
namespace hypha::reference_audition
{
std::uint64_t captureHash(std::uint64_t hash, const float* pcm, size_t count) noexcept
{
    for(size_t i=0;i<count;++i) { std::uint32_t bits=0; std::memcpy(&bits,pcm+i,sizeof(bits)); hash=hash*1315423911ULL+bits; }
    return hash;
}
static std::uint64_t hashPower(std::uint64_t n)
{ std::uint64_t p=1315423911ULL,result=1; for(;n;n>>=1,p*=p) if(n&1) result*=p; return result; }
void mergeACaptureBins(std::vector<ACaptureBin>& bins, int channels)
{
    size_t out=0;
    for(size_t i=0;i<bins.size();i+=2)
    {
        auto a=bins[i];
        if(i+1<bins.size())
        {
            const auto& b=bins[i+1]; const auto frames=a.value.frames+b.value.frames;
            for(int c=0;c<2;++c)
            {
                a.value.peak[c]=std::max(a.value.peak[c],b.value.peak[c]);
                a.value.rms[c]=std::sqrt((a.value.rms[c]*a.value.rms[c]*a.value.frames+b.value.rms[c]*b.value.rms[c]*b.value.frames)/frames);
            }
            a.fingerprint=a.fingerprint*hashPower(b.value.frames*std::uint64_t(channels))+b.fingerprint;
            a.value.frames=frames; a.value.short_lufs=b.value.short_lufs;
            a.truePeak=std::max(a.truePeak,b.truePeak);
            const auto rms=std::sqrt((a.value.rms[0]*a.value.rms[0]+a.value.rms[1]*a.value.rms[1])/channels);
            a.value.crest_db=rms>1e-15 && a.truePeak>0 ? 20*std::log10(a.truePeak/rms) : std::numeric_limits<double>::quiet_NaN();
        }
        bins[out++]=a;
    }
    bins.resize(out);
}
juce::String encodeACapture(const ACaptureData& data)
{
    if(data.bins.empty() || data.bins.size()>2048 || data.receiver.length()>160 || data.units.size()>7200 || data.bindings.size()>16) return {};
    juce::MemoryOutputStream out;
    out.writeInt(0x41435032); out.writeString(data.id); out.writeString(data.receiver); out.writeString(data.verifiedWork);
    out.writeInt64(data.created); out.writeInt64(data.hostStart); out.writeInt64(juce::int64(data.frames));
    out.writeInt64(juce::int64(data.hop)); out.writeInt(data.rate); out.writeInt(data.channels);
    out.writeInt(data.clockSource); out.writeBool(data.complete); out.writeDouble(data.integrated); out.writeDouble(data.maximumTruePeak);
    out.writeInt(int(data.bins.size()));
    for(const auto& bin:data.bins)
    {
        out.writeInt64(juce::int64(bin.offset)); out.writeInt64(juce::int64(bin.value.frames));
        for(auto v:bin.value.peak) out.writeDouble(v);
        for(auto v:bin.value.rms) out.writeDouble(v);
        out.writeDouble(bin.value.short_lufs); out.writeDouble(bin.value.crest_db); out.writeDouble(bin.truePeak); out.writeInt64(juce::int64(bin.fingerprint));
    }
    writeCaptureEvidence(out,data);
    const auto checksum=juce::SHA256(out.getData(),out.getDataSize()).toHexString();
    const auto encoded=checksum+":"+out.getMemoryBlock().toBase64Encoding();
    return encoded.length()<=1024*1024-4096 ? encoded : juce::String();
}
std::shared_ptr<const ACaptureData> decodeACapture(const juce::String& text)
{
    if(text.isEmpty() || text.length()>1024*1024-4096) return {};
    if(text.indexOfChar(':')!=64) return {};
    juce::MemoryBlock bytes; if(!bytes.fromBase64Encoding(text.substring(65)) || bytes.getSize()>768*1024) return {};
    if(bytes.toBase64Encoding()!=text.substring(65)) return {};
    if(juce::SHA256(bytes.getData(),bytes.getSize()).toHexString()!=text.substring(0,64)) return {};
    juce::MemoryInputStream in(bytes,false);
    const auto schema=in.readInt();
    if(schema!=0x41435031 && schema!=0x41435032) return {};
    if(schema==0x41435031 && (text.length()>256*1024 || bytes.getSize()>192*1024)) return {};
    auto d=std::make_shared<ACaptureData>(); d->id=in.readString(); d->receiver=in.readString(); d->verifiedWork=in.readString();
    d->created=in.readInt64(); d->hostStart=in.readInt64(); d->frames=std::uint64_t(in.readInt64());
    d->hop=std::uint64_t(in.readInt64()); d->rate=in.readInt(); d->channels=in.readInt(); d->clockSource=in.readInt();
    d->complete=in.readBool(); d->integrated=in.readDouble(); d->maximumTruePeak=in.readDouble();
    const int count=in.readInt();
    constexpr auto limit=std::numeric_limits<std::int64_t>::max()/4;
    if(d->id.length()!=36 || juce::Uuid(d->id).isNull() || d->verifiedWork.length()>160 || d->clockSource<0 || d->clockSource>8 || d->receiver.length()>160 || d->rate<8000 || d->rate>768000 || d->channels<1 || d->channels>2
        || d->frames<1 || d->frames>std::uint64_t(d->rate)*7200 || d->hop<1 || d->hop>d->frames+std::uint64_t(d->rate)*128
        || d->hostStart < -limit || d->hostStart>limit || count<1 || count>2048
        || in.getNumBytesRemaining()<juce::int64(count)*80 || !std::isfinite(d->maximumTruePeak) || d->maximumTruePeak<0) return {};
    std::uint64_t end=0;
    for(int i=0;i<count;++i)
    {
        ACaptureBin bin; bin.offset=std::uint64_t(in.readInt64()); bin.value.frames=std::uint64_t(in.readInt64());
        for(auto& v:bin.value.peak) v=in.readDouble();
        for(auto& v:bin.value.rms) v=in.readDouble();
        bin.value.short_lufs=in.readDouble(); bin.value.crest_db=in.readDouble(); bin.truePeak=in.readDouble(); bin.fingerprint=std::uint64_t(in.readInt64());
        if(std::isinf(bin.value.crest_db) || (std::isinf(bin.value.short_lufs) && bin.value.short_lufs>0)) return {};
        if(bin.offset!=end || bin.value.frames<1 || bin.value.frames>d->frames-end || !std::isfinite(bin.truePeak) || bin.truePeak<0) return {};
        for(int c=0;c<2;++c) if(!std::isfinite(bin.value.peak[c]) || !std::isfinite(bin.value.rms[c])
            || bin.value.rms[c]<0 || bin.value.peak[c]<bin.value.rms[c]-1e-7) return {};
        end+=bin.value.frames; d->bins.push_back(bin);
    }
    if(end!=d->frames || (schema==0x41435032 ? !readCaptureEvidence(in,*d) : !in.isExhausted())) return {};
    if(!d->complete) d->integrated=std::numeric_limits<double>::quiet_NaN();
    d->restored=true; d->revision=1; return d;
}
}
