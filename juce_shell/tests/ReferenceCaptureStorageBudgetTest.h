#pragma once
#include "../src/reference_audition/ReferenceComparisonSettings.h"
inline void testCaptureStorageBudget(const ref::ACaptureData& base)
{
    for(int rate:{8000,44100,48000,96000,192000,384000,768000}) for(int channels:{1,2})
    {
        auto data=base; data.rate=rate; data.channels=channels; data.frames=std::uint64_t(rate)*7200;
        data.tonal.sampleRate=rate; data.tonal.channels=channels; data.tonal.frames=data.frames;
        data.tonal.artifactSha256=juce::String::repeatedString("e",64);
        data.tonal.recoveryKey=juce::String::repeatedString("f",64);
        data.tonal.artifactBytes=16*1024*1024;
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
        settings.tonal.source=ref::TonalDisplayState::Source::captured;
        settings.tonal.selectedBand=59; settings.tonal.rangeStart=0.25; settings.tonal.rangeEnd=7199.75;
        settings.tonal.genreId=juce::String::repeatedString("g",160); settings.tonal.captureId=data.id;
        settings.tonal.artifactSha256=juce::String::repeatedString("a",64);
        settings.tonal.publicationRevision=juce::String::repeatedString("r",160);
        settings.workflow.mode=ref::WorkflowResumeState::Mode::review;
        settings.workflow.reviewId="11111111-1111-4111-8111-111111111111";
        settings.workflow.reviewRevisionId="22222222-2222-4222-8222-222222222222";
        settings.workflow.attemptId="33333333-3333-4333-8333-333333333333";
        settings.workflow.checkpointId="44444444-4444-4444-8444-444444444444";
        settings.workflow.conditionRevisionId="55555555-5555-4555-8555-555555555555";
        settings.workflow.journalHeadSha256=juce::String::repeatedString("b",64);
        settings.workflow.returnSlot=2;
        require(settings.captureState.isNotEmpty(),"max-duration max-bins max-receipts capture fits encoding");
        juce::XmlElement xml("Settings"); settings.write(xml);
        const auto encodedBytes=xml.toString().getNumBytesAsUTF8();
        require(encodedBytes<=ref::referenceStateMaximumEncodedBytes
            && ref::referenceStateHardLimitBytes-encodedBytes>=ref::referenceStateMinimumMarginBytes,
            "combined Capture, Tonal and workflow state retains the fixed 16 KiB margin");
        const auto state=ref::ReferenceComparisonSettings::read(xml);
        require(state.tonal.valid() && state.workflow.valid(),"bounded Tonal and workflow state round trip");
        const auto restored=ref::decodeACapture(state.captureState);
        require(restored != nullptr,"maximum storage fixture decodes");
        require(restored->units.size()==7200,"maximum storage round trip retains all units");
        require(restored->bins.size()==2048,"maximum storage round trip retains all bins");
        require(restored->bindings.size()==16,"maximum storage round trip retains all receipts");
    }
}
