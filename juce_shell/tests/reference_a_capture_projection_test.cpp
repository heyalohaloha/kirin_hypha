#include "../src/reference_audition/ReferenceACaptureProjection.h"
#include "reference_runtime_test_support.h"
#include <iostream>
void testReferenceACaptureProjection(const juce::File&,const std::shared_ptr<const ref::ACaptureData>&,const juce::var&);
void testReferenceACaptureProjection(const juce::File& root,const std::shared_ptr<const ref::ACaptureData>& original,const juce::var& stat)
{
    const auto file=root.getChildFile("capture-source.wav");
    auto source=std::make_shared<ref::RuntimeSource>(); source->absolutePath=file.getFullPathName();
    source->audio={48000,2,48000*8}; source->sourceFileSha256=juce::SHA256(file).toHexString();
    source->sourceKind="work_version"; source->sourceIdentityKey="capture-work:recording:version";
    // Reuse the exact file-revision reader used by the producer-shaped source fixtures.
    source->revision={stat["device_id"].toString(),stat["file_id"].toString(),stat["size_bytes"].toString(),stat["mtime_ns"].toString(),stat["ctime_ns"].toString()};
    auto access=std::make_shared<ref::ACaptureAccess>(); access->capturedView=true; access->analysisAvailable=true;
    auto captured=std::make_shared<ref::ACaptureData>(*original); captured->verifiedWork="capture-work";
    ref::CaptureBindingReceipt proof; proof.captureId=captured->id; proof.sourceHash=source->sourceFileSha256;
    proof.rate=48000; proof.channels=2; proof.hostAnchor=24000; proof.probeStart=24000; proof.probeEnd=216000;
    proof.calibrationHash=juce::String::repeatedString("a",64); proof.pairedBlocks=27; proof.gainKnown=true;
    require(proof.valid(),"fixture has explicit Capture-specific position and gain evidence"); captured->bindings.push_back(proof);
    ref::ACaptureState state; state.shown=state.held=captured; access->publish(state);
    juce::CriticalSection lock; ref::VisualBinding map; map.source=source; map.hostRate=48000; map.channels=2;
    map.hostAnchor=24000; map.aligned=true; map.key="capture-version";
    ref::ACaptureProjection projection(access,[&]{const juce::ScopedLock guard(lock);return map;}); projection.setPresented(true);
    const auto wait=[&](const auto& f){ for(int i=0;i<600;++i){const auto t=projection.snapshot();if(t && f(*t))return true;juce::Thread::sleep(5);}return false;};
    require(wait([](const auto& t){return t.bins.size()==40 && t.bins.back().b.frames==4800;}),"captured A is projected to complete matching B windows");
    for(const auto& pair:projection.snapshot()->bins) {
        require(pair.a.frames==pair.b.frames && std::abs(pair.a.rms[0]-pair.b.rms[0])<1e-9,"captured A and B use identical sample windows");
        if(std::isfinite(pair.a.short_lufs)) require(std::abs(pair.a.short_lufs-pair.b.short_lufs)<0.001,"captured and B three-second endpoints agree");
    }
    {const juce::ScopedLock guard(lock);map.gainDb=-6;map.hostAnchor+=48000;map.key="capture-version-gain";}
    juce::Thread::sleep(150);
    require(wait([](const auto& t){return std::abs(t.binding.gainDb)<1e-9 && t.binding.hostAnchor==24000 && t.bins.back().b.frames==4800;}),"live gain and one-second anchor shift cannot change a saved comparison");
    for(const auto& pair:projection.snapshot()->bins) require(std::abs(pair.a.rms[0]-pair.b.rms[0])<1e-9,"historical sample windows remain exact after live recalibration");
    captured=std::make_shared<ref::ACaptureData>(*captured);captured->restored=true;state.held=state.shown=captured;access->publish(state);
    require(wait([](const auto& t){return t.binding.source && t.capture->restored && t.binding.hostAnchor==24000;}),"restored historical receipt retains its own position");
    auto legacy=std::make_shared<ref::ACaptureData>(*captured); legacy->bindings.clear(); state.held=state.shown=legacy; state.revisitedWork="capture-work"; access->publish(state);
    require(wait([](const auto& t){return !t.binding.source;}),"same Work and current aligned audio cannot certify old captures without receipts");
    state.held=state.shown=captured; access->publish(state);
    require(wait([](const auto& t){return t.binding.source && t.bins.back().b.frames==4800;}),"return to exact historical receipt");
    require(file.deleteFile(),"simulate missing B source");
    require(wait([](const auto& t){return !t.binding.source && t.capture->frames==192000;}),"B loss preserves A-only capture");
    std::cout<<"Capture A projection: exact B windows, preserved A, gain, restore identity, missing B PASS\n";
}
