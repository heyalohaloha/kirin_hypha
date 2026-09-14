#pragma once
#include <juce_core/juce_core.h>
#include "kirin_hypha_reference_index_ffi.h"
#include <vector>
#include <array>
namespace hypha::reference_audition
{
struct RuntimeACaptureAudio;
struct CaptureClockSignature
{
    std::uint32_t input=0,output=0;
    std::uint8_t presentation=0;
    bool inputValid=false,outputValid=false;
    bool operator==(const CaptureClockSignature& v) const noexcept
    { return input==v.input && output==v.output && presentation==v.presentation && inputValid==v.inputValid && outputValid==v.outputValid; }
    bool operator!=(const CaptureClockSignature& v) const noexcept { return !(*this==v); }
};
struct CaptureBindingReceipt
{
    juce::String captureId,sourceHash,sourcePcmHash,work,calibrationHash;
    std::int64_t hostAnchor=0,sourceAnchor=0,probeStart=0,probeEnd=0;
    std::uint64_t revision=1,pairedBlocks=0;
    int rate=0,channels=0,policy=1;
    double fullGainDb=0,displayGainDb=0,aPeakDbtp=0,bPeakDbtp=0,ceilingDbtp=-1;
    bool gainKnown=false,originalFallback=false;
    bool valid() const;
};
struct ACaptureReceipt
{
    juce::String work; std::int64_t hostPosition=0; int rate=0; bool verified=false;
    CaptureBindingReceipt evidence;
    std::array<KirinReferenceCaptureUnit,4> units{};
    bool completeProbe=false;
};
bool captureDigestEqual(const KirinReferenceCaptureUnit&,const KirinReferenceCaptureUnit&) noexcept;
bool captureProbeMatches(const std::vector<KirinReferenceCaptureUnit>&,std::int64_t hostStart,int rate,int channels,const RuntimeACaptureAudio&);
bool captureUniqueSequence(const std::vector<KirinReferenceCaptureUnit>&,size_t first);
}
