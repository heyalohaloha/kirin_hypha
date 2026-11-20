#pragma once
#include "ReferenceControllerTestAccess.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include "ReferenceChangeNotesRepositoryTest.h"
#include "ReferenceWholeSongAudioFixture.h"
#include "reference_library_manifest_fixture.h"
#include <condition_variable>
#include <mutex>

#ifndef KIRIN_REFERENCE_TEST_COMMIT_OBSERVER
#error This fixture requires the instrumented control worker
#endif

namespace hypha::tests::notes_race
{
inline void onBoundary(const void*,const char*,std::uint64_t);
struct Boundary
{
    std::mutex mutex;
    std::condition_variable changed;
    const void* owner=nullptr;
    juce::String hash;
    std::uint64_t generation=0;
    bool armed=false, reached=false, released=false, exited=false, timedOut=false;
};
inline Boundary boundary;
// Declared after the controller: unwinding releases the worker before controller teardown joins it.
class Guard final
{
public:
    Guard(const void* owner,const juce::String& hash,std::uint64_t generation)
    {
        auto& s=boundary; const std::lock_guard<std::mutex> lock(s.mutex);
        if(s.owner) throw std::runtime_error("notes boundary already owned");
        s.owner=owner;s.hash=hash;s.generation=generation;
        s.armed=true;s.reached=s.released=s.exited=s.timedOut=false;
        reference_audition::referenceTestNotesBoundaryHook.store(onBoundary,std::memory_order_release);
    }
    ~Guard()
    {
        release();
        auto& s=boundary; std::unique_lock<std::mutex> lock(s.mutex);
        if(s.reached && !s.changed.wait_for(lock,std::chrono::seconds(30),[&]{return s.exited;})) s.timedOut=true;
        reference_audition::referenceTestNotesBoundaryHook.store(nullptr,std::memory_order_release);
        s.owner=nullptr;s.armed=false;
    }
    bool arrived()
    {
        auto& s=boundary;std::unique_lock<std::mutex> lock(s.mutex);
        return s.changed.wait_for(lock,std::chrono::seconds(30),[&]{return s.reached;}) && !s.timedOut;
    }
    void release() noexcept
    {
        auto& s=boundary;
        {const std::lock_guard<std::mutex> lock(s.mutex);s.released=true;s.armed=false;}
        s.changed.notify_all();
    }
    bool completed()
    {
        auto& s=boundary;std::unique_lock<std::mutex> lock(s.mutex);
        return s.changed.wait_for(lock,std::chrono::seconds(30),[&]{return s.exited;}) && !s.timedOut;
    }
};
}

namespace hypha::tests::notes_race
{
inline void onBoundary(const void* owner,const char* hash,std::uint64_t generation)
{
    auto& s=hypha::tests::notes_race::boundary;
    std::unique_lock<std::mutex> lock(s.mutex);
    if(!s.armed || s.owner!=owner || s.hash!=hash || s.generation!=generation) return;
    s.armed=false;s.reached=true;s.changed.notify_all();
    if(!s.changed.wait_for(lock,std::chrono::seconds(30),[&]{return s.released;})) s.timedOut=true;
    s.exited=true;s.changed.notify_all();
}
}

namespace
{
void verifyChangeNotesSelectionRace(ref::ReferenceComparisonController& controller,const juce::File& root,
    juce::var manifest,juce::var descriptorA,juce::var bodyA,const juce::String& idA)
{
    namespace control=hypha::tests::reference_control;
    auto& version=control::roleFor(controller,{1});
    const auto file=root.getChildFile("second-version.wav");
    const juce::String recording="44444444-4444-4444-8444-444444444444";
    const juce::String versionId="55555555-5555-4555-8555-555555555555";
    const auto audio=makeWholeSongFixture(root,file,recording,versionId);
    auto preset=bindRuntimeV2WorkVersionPresetToSource("66666666-6666-4666-8666-666666666666",
        "77777777-7777-4777-8777-777777777777",audio.receipt,juce::SHA256(file).toHexString(),audio.pcmHash,
        recording,versionId,audio.audio.getNumSamples());
    auto* p=preset.getDynamicObject();p->setProperty("format","kirin_hypha_reference_library_preset");
    p->setProperty("version","1.0");p->removeProperty("work_id");p->removeProperty("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty("preparation_status","prepared");
    const auto second=independentLibraryManifest(root,preset,100);
    auto receiptB=second["versions"][0].clone();
    auto descriptorB=juce::JSON::parse(root.getChildFile("library/versions/"+receiptB["sha256"].toString()+".json"));
    auto bodyB=bodyA.clone();
    bodyB.getDynamicObject()->setProperty("source_identity",descriptorB["candidate"]["source_identity"].clone());
    bodyB.getDynamicObject()->setProperty("source_artifact_sha256",audio.receipt.sha256);
    bodyB.getDynamicObject()->setProperty("total_sample_frames",audio.audio.getNumSamples());
    bodyB["notes"][0].getDynamicObject()->setProperty("text","Only the second Version instruction.");
    const auto notesReceipt=[&](juce::var body,juce::var& descriptor) {
        const auto r=stageWholeSongArtifact(root,"notes",body);
        auto* item=new juce::DynamicObject();item->setProperty("relative_path",r.relativePath);
        item->setProperty("sha256",r.sha256);item->setProperty("bytes",r.bytes);
        descriptor.getDynamicObject()->setProperty("change_notes_artifact",juce::var(item));return r.sha256;
    };
    const auto updateVersion=[&](juce::var descriptor,juce::var& receipt) {
        const auto r=stageWholeSongArtifact(root,"library/versions",descriptor);
        receipt.getDynamicObject()->setProperty("relative_path",r.relativePath);
        receipt.getDynamicObject()->setProperty("sha256",r.sha256);receipt.getDynamicObject()->setProperty("bytes",r.bytes);
    };
    const auto hashB=notesReceipt(bodyB,descriptorB);updateVersion(descriptorB,receiptB);
    manifest["versions"].getArray()->add(receiptB);
    const auto publish=[&](int revision) {
        manifest.getDynamicObject()->setProperty("revision",revision);
        require(writeJson(root.getChildFile("library/manifest.json"),manifest),"publish two real Versions");
    };
    publish(8);
    const auto entryB=descriptorB["entry_id"].toString();
    juce::String idB;
    control::await([&]{
        for(const auto& item:controller.snapshot().versions)
            if(item.id!=idA && item.id.contains(entryB)) {idB=item.id;return true;}
        return false;
    },"normal worker discovers the second Version");
    const auto matches=[&](const juce::String& id,const juce::String& hash,const juce::String& text) {
        const auto s=controller.snapshot();const auto v=s.versionSelection;
        return s.selectedVersionId==id && v && v->changeNotes
            && v->selectionGeneration==version.requestedGeneration()
            && v->changeNotesSelectionGeneration==v->selectionGeneration
            && v->changeNotes->artifactSha256==hash && v->changeNotes->notes.size()==1
            && v->changeNotes->notes[0].text==text;
    };
    bodyA["notes"][0].getDynamicObject()->setProperty("text","First Version instruction after refresh.");
    const auto hashA=notesReceipt(bodyA,descriptorA);
    auto receiptA=manifest["versions"][0];updateVersion(descriptorA,receiptA);
    const auto generationA=version.requestedGeneration();
    {
        hypha::tests::notes_race::Guard guard(&version,hashA,generationA);
        publish(9);
        require(guard.arrived(),"worker reaches exact A receipt acceptance boundary");
        require(controller.selectVersion(idB,true),"public API selects B while A read is paused");
        require(version.requestedGeneration()!=generationA,"B selection advances requested generation");
        const auto during=controller.snapshot().versionSelection;
        require(during && !during->changeNotes,"stale A notes hidden immediately after B request");
        guard.release();require(guard.completed(),"acceptance barrier exits without timeout");
        control::await([&]{
            const auto visible=controller.snapshot().versionSelection;
            require(!visible || !visible->changeNotes || visible->changeNotes->artifactSha256==hashB,
                "no observed publication lends A notes to the requested B selection");
            return matches(idB,hashB,"Only the second Version instruction.");},
            "B publishes only its own notes after rejecting stale A work");
    }
    require(controller.selectVersion(idA,true),"return to actual first Version");
    control::await([&]{return matches(idA,hashA,"First Version instruction after refresh.");},
        "first Version remains recoverable with its updated notes");
    std::cout<<"REFERENCE_CHANGE_NOTES_SELECTION_RACE=1 MODE=ordinary-workers-boundary-barrier\n";
}
}
