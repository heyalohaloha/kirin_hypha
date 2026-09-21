#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceContentAlignment.h"
#include "kirin_hypha_reference_ffi.h"
namespace hypha::reference_audition
{
void RuntimeV2Controller::setCaptureObservation(const juce::String& id,std::int64_t anchor)
{
    const juce::ScopedLock lock(stateLock);
    if(captureTargetId==id && captureGridAnchor==anchor) return;
    captureTargetId=id; captureGridAnchor=anchor; captureProbeKey.clear(); publishedCaptureEvidence.reset();
}
void RuntimeV2Controller::serviceCaptureEvidence(const RuntimeACaptureAudio& audio,const RuntimeSource& source,const RuntimeContentAlignment& aligned)
{
    juce::String id,key; std::int64_t anchor=0;
    {
        const juce::ScopedLock lock(stateLock); id=captureTargetId; anchor=captureGridAnchor;
        if(id.isEmpty() || !aligned.established || audio.sampleRateHz<8000 || audio.startSample<anchor
            || (audio.startSample-anchor)%audio.sampleRateHz || audio.frameCount!=audio.sampleRateHz*4) return;
        key=id+":"+source.sourceFileSha256+":"+referenceObservationIdentity(audio);
        if(captureProbeKey==key) return;
        captureProbeKey=key;
    }
    if(aligned.alignedProbe.size()!=audio.interleaved.size()) return;
    KirinReferenceGainFacts gain{};
    if(!kirin_hypha_analyze_reference_gain(audio.interleaved.data(),aligned.alignedProbe.data(),size_t(audio.frameCount),uint32_t(audio.sampleRateHz),uint32_t(audio.channels),&gain)) return;
    auto result=std::make_shared<ACaptureReceipt>(); auto& proof=result->evidence;
    proof.captureId=id; proof.sourceHash=source.sourceFileSha256; proof.sourcePcmHash=source.sourcePcmSha256;
    proof.work=source.sourceIdentityKey.upToFirstOccurrenceOf(":",false,false); proof.calibrationHash=audio.cuePcmSha256;
    proof.rate=int(audio.sampleRateHz); proof.channels=audio.channels;
    proof.hostAnchor=proof.probeStart=audio.startSample; proof.probeEnd=audio.startSample+audio.frameCount;
    proof.sourceAnchor=std::int64_t(std::llround(static_cast<long double>(aligned.sourceStartSample)*proof.rate/source.audio.sampleRateHz));
    proof.pairedBlocks=gain.paired_block_count; proof.fullGainDb=gain.paired_loudness_delta_median_millilu/1000.0;
    proof.aPeakDbtp=gain.a_cue_true_peak_millidbtp/1000.0;
    const auto sourcePeak=source.measurementSummary ? source.measurementSummary->maximumTruePeakDbtp : std::nullopt;
    proof.bPeakDbtp=sourcePeak.value_or(gain.b_cue_true_peak_millidbtp/1000.0);
    proof.ceilingDbtp=std::max({-1.0,proof.aPeakDbtp,proof.bPeakDbtp});
    proof.originalFallback=proof.fullGainDb>0 && (!sourcePeak || proof.fullGainDb>proof.ceilingDbtp-proof.bPeakDbtp+1e-9);
    proof.displayGainDb=proof.originalFallback ? 0 : proof.fullGainDb; proof.gainKnown=true;
    auto* index=kirin_reference_index_create(uint32_t(proof.rate),uint32_t(proof.channels),uint32_t(proof.rate));
    if(!index) return;
    bool valid=true;
    for(int unit=0;unit<4 && valid;++unit)
    {
        for(int at=0;at<proof.rate;)
        {
            const int frames=std::min(8192,proof.rate-at);
            if(!kirin_reference_index_push(index,audio.interleaved.data()+size_t((std::int64_t(unit)*proof.rate+at)*proof.channels),size_t(frames*proof.channels))) { valid=false; break; }
            at+=frames;
        }
        valid=valid && kirin_reference_index_finish(index,&result->units[size_t(unit)])==uint32_t(proof.rate);
    }
    kirin_reference_index_drop(index);
    if(!valid || !proof.valid() || sourceRepository.verifySourceRevision(source).isNotEmpty()) return;
    result->completeProbe=true; result->verified=true; result->rate=proof.rate; result->work=proof.work; result->hostPosition=proof.probeEnd;
    const juce::ScopedLock lock(stateLock);
    if(captureTargetId==id && captureGridAnchor==anchor && captureProbeKey==key) publishedCaptureEvidence=std::move(result);
}
}
