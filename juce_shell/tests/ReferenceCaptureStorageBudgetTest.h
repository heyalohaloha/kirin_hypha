#pragma once
#include "../src/reference_audition/ReferenceComparisonSettings.h"
inline void testCaptureStorageBudget(const ref::ACaptureData& base)
{
    for(int rate:{8000,44100,48000,96000,192000,384000,768000}) for(int channels:{1,2})
    {
        auto data=base; data.rate=rate; data.channels=channels; data.frames=std::uint64_t(rate)*7200;
        data.units.assign(7200,{}); data.bindings.clear(); data.bins.resize(2048); data.hop=data.frames/2048;
        std::uint64_t offset=0;
        for(size_t i=0;i<data.bins.size();++i) {
            auto& bin=data.bins[i]; bin={}; bin.offset=offset;
            bin.value.frames=data.hop+(i+1==data.bins.size() ? data.frames%2048 : 0); offset+=bin.value.frames;
        }
        for(int i=0;i<16;++i) {
            ref::CaptureBindingReceipt proof; proof.captureId=data.id;
            proof.sourceHash=juce::String(i).paddedLeft('0',64); proof.sourcePcmHash=proof.sourceHash;
            proof.work=juce::String::repeatedString("w",160); proof.calibrationHash=juce::String::repeatedString("c",64);
            proof.rate=rate; proof.channels=channels; proof.probeStart=data.hostStart; proof.probeEnd=data.hostStart+std::int64_t(rate)*4;
            require(proof.valid(),"maximum-size derived receipt is valid"); data.bindings.push_back(proof);
        }
        ref::ReferenceComparisonSettings settings; settings.captureState=ref::encodeACapture(data);
        require(settings.captureState.isNotEmpty(),"max-duration max-bins max-receipts capture fits encoding");
        juce::XmlElement xml("Settings"); settings.write(xml);
        require(xml.toString().getNumBytesAsUTF8()<1024*1024,"encoded state including XML overhead stays below 1 MiB at every rate/layout");
        const auto restored=ref::decodeACapture(ref::ReferenceComparisonSettings::read(xml).captureState);
        require(restored && restored->units.size()==7200 && restored->bins.size()==2048 && restored->bindings.size()==16,"maximum storage round trip loses no units, bins or receipts");
    }
}
