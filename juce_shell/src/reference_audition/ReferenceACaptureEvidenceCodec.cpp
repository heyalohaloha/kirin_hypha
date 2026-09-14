#include "ReferenceACaptureEvidenceCodec.h"
#include <cmath>
namespace hypha::reference_audition
{
void writeCaptureEvidence(juce::MemoryOutputStream& out,const ACaptureData& d)
{
    out.writeInt(d.terminationReason);
    out.writeInt(int(d.clockSignature.input)); out.writeInt(int(d.clockSignature.output)); out.writeByte(char(d.clockSignature.presentation));
    out.writeBool(d.clockSignature.inputValid); out.writeBool(d.clockSignature.outputValid);
    out.writeInt(int(d.units.size()));
    for(const auto& u:d.units)
    {
        out.write(u.digest,sizeof(u.digest)); for(auto v:u.bands) out.writeShort(v);
        for(auto v:u.rms) out.writeFloat(v); for(auto v:u.peak) out.writeFloat(v);
    }
    out.writeInt(int(d.bindings.size()));
    for(const auto& b:d.bindings)
    {
        out.writeString(b.captureId); out.writeString(b.sourceHash); out.writeString(b.sourcePcmHash); out.writeString(b.work); out.writeString(b.calibrationHash);
        for(auto v:{b.hostAnchor,b.sourceAnchor,b.probeStart,b.probeEnd}) out.writeInt64(v);
        out.writeInt64(juce::int64(b.revision)); out.writeInt64(juce::int64(b.pairedBlocks));
        out.writeInt(b.rate); out.writeInt(b.channels); out.writeInt(b.policy);
        for(auto v:{b.fullGainDb,b.displayGainDb,b.aPeakDbtp,b.bPeakDbtp,b.ceilingDbtp}) out.writeDouble(v);
        out.writeBool(b.gainKnown); out.writeBool(b.originalFallback);
    }
}
bool readCaptureEvidence(juce::MemoryInputStream& in,ACaptureData& d)
{
    if(in.getNumBytesRemaining()<23) return false;
    d.terminationReason=in.readInt();
    d.clockSignature.input=std::uint32_t(in.readInt()); d.clockSignature.output=std::uint32_t(in.readInt()); d.clockSignature.presentation=std::uint8_t(in.readByte());
    d.clockSignature.inputValid=in.readBool(); d.clockSignature.outputValid=in.readBool();
    const auto count=in.readInt();
    if(d.terminationReason<0 || d.terminationReason>5 || count<0 || count>7200
        || (count && std::uint64_t(count)!=(d.frames+std::uint64_t(d.rate)-1)/std::uint64_t(d.rate))
        || in.getNumBytesRemaining()<juce::int64(count)*64+4) return false;
    d.units.resize(size_t(count));
    for(auto& u:d.units)
    {
        if(in.read(u.digest,sizeof(u.digest))!=sizeof(u.digest)) return false;
        for(auto& v:u.bands) v=in.readShort(); for(auto& v:u.rms) v=in.readFloat(); for(auto& v:u.peak) v=in.readFloat();
        for(int c=0;c<2;++c) if(!std::isfinite(u.rms[c]) || !std::isfinite(u.peak[c]) || u.rms[c]<0 || u.peak[c]<u.rms[c]-1e-6f) return false;
    }
    const int receipts=in.readInt(); if(receipts<0 || receipts>16) return false;
    for(int i=0;i<receipts;++i)
    {
        const auto start=in.getPosition(); CaptureBindingReceipt b;
        b.captureId=in.readString(); b.sourceHash=in.readString(); b.sourcePcmHash=in.readString(); b.work=in.readString(); b.calibrationHash=in.readString();
        b.hostAnchor=in.readInt64(); b.sourceAnchor=in.readInt64(); b.probeStart=in.readInt64(); b.probeEnd=in.readInt64();
        b.revision=std::uint64_t(in.readInt64()); b.pairedBlocks=std::uint64_t(in.readInt64());
        b.rate=in.readInt(); b.channels=in.readInt(); b.policy=in.readInt();
        b.fullGainDb=in.readDouble(); b.displayGainDb=in.readDouble(); b.aPeakDbtp=in.readDouble(); b.bPeakDbtp=in.readDouble(); b.ceilingDbtp=in.readDouble();
        b.gainKnown=in.readBool(); b.originalFallback=in.readBool();
        if(!b.valid() || b.captureId!=d.id || b.rate!=d.rate || b.channels!=d.channels || in.getPosition()-start>4096
            || b.probeStart<d.hostStart || b.probeEnd>d.hostStart+std::int64_t(d.frames)) return false;
        for(const auto& old:d.bindings) if(old.sourceHash==b.sourceHash) return false;
        d.bindings.push_back(std::move(b));
    }
    return in.isExhausted();
}
}
