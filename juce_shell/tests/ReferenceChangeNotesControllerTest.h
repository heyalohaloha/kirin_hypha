#pragma once
#include "reference_runtime_test_entries.h"
#include "ReferenceChangeNotesRepositoryTest.h"
#include "ReferenceManualService.h"
#include "ReferenceWholeSongAudioFixture.h"
#include "reference_library_manifest_fixture.h"
#include "../src/reference_audition/ReferenceComparisonController.h"

namespace
{
// Real repository/source/page/notes reads and controller publication, driven by
// the existing manual service after stopping RuntimeV2 workers. This is NOT an
// ordinary asynchronous-worker or real DAW test. No injected published Snapshot.
void verifyChangeNotesController(const juce::File& sandbox)
{
    namespace control=hypha::tests::reference_control;
    namespace notes=hypha::tests::change_notes;
    const auto root=sandbox.getChildFile("change-notes-controller");
    require(root.createDirectory().wasOk(),"notes controller isolated directory");
    const auto file=root.getChildFile("version.wav");
    const auto fixture=makeWholeSongFixture(root,file,"22222222-2222-4222-8222-222222222222",
        "33333333-3333-4333-8333-333333333333");
    auto preset=bindRuntimeV2WorkVersionPresetToSource("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999",fixture.receipt,juce::SHA256(file).toHexString(),fixture.pcmHash,
        "22222222-2222-4222-8222-222222222222","33333333-3333-4333-8333-333333333333",fixture.audio.getNumSamples());
    auto* p=preset.getDynamicObject(); p->setProperty("format","kirin_hypha_reference_library_preset");
    p->setProperty("version","1.0"); p->removeProperty("work_id"); p->removeProperty("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty("preparation_status","prepared");
    auto manifest=independentLibraryManifest(root,preset,1);
    auto descriptor=juce::JSON::parse(root.getChildFile("library/versions/"+manifest["versions"][0]["sha256"].toString()+".json"));
    const auto entry=descriptor["entry_id"].toString();
    const auto revision=manifest["versions"][0]["revision_id"].toString();
    require(entry.isNotEmpty() && revision==entry,"fixture has a concrete Version entry/revision");
    auto body=notes::fixture();
    body.getDynamicObject()->setProperty("source_identity",descriptor["candidate"]["source_identity"].clone());
    body.getDynamicObject()->setProperty("source_artifact_sha256",fixture.receipt.sha256);
    body.getDynamicObject()->setProperty("sample_rate_hz",48000);
    body.getDynamicObject()->setProperty("total_sample_frames",fixture.audio.getNumSamples());
    body["notes"][0].getDynamicObject()->setProperty("at_seconds",1);
    body["notes"][0].getDynamicObject()->setProperty("at_sample",48000);
    const auto attach=[&] {
        const auto r=stageWholeSongArtifact(root,"notes",body);
        auto* receipt=new juce::DynamicObject(); receipt->setProperty("relative_path",r.relativePath);
        receipt->setProperty("sha256",r.sha256); receipt->setProperty("bytes",r.bytes);
        descriptor.getDynamicObject()->setProperty("change_notes_artifact",juce::var(receipt));
    };
    const auto publish=[&](int n) {
        require(descriptor["entry_id"].toString()==entry && manifest["versions"][0]["revision_id"].toString()==revision,
            "receipt updates retain same Version entry and revision");
        const auto r=stageWholeSongArtifact(root,"library/versions",descriptor);
        auto* receipt=manifest["versions"][0].getDynamicObject();
        receipt->setProperty("relative_path",r.relativePath); receipt->setProperty("sha256",r.sha256); receipt->setProperty("bytes",r.bytes);
        manifest.getDynamicObject()->setProperty("revision",n);
        require(writeJson(root.getChildFile("library/manifest.json"),manifest),"publish controller notes manifest");
    };
    attach(); publish(1);
    ref::ReferenceComparisonController controller(root,[](bool){return true;},[](bool){return true;});
    control::stopWorkers(controller);
    controller.configure({"notes-controller-post",{},42,true},48000,2);
    controller.setPresented(true);
    const auto pump=[&] { for(int i=0;i<12;++i) control::service(controller); };
    pump();
    require(!controller.snapshot().versions.empty(),"controller discovers actual Version catalog");
    const auto id=controller.snapshot().versions.front().id;
    require(controller.selectVersion(id,true),"public controller selects actual Version");
    require(controller.selectVisualSlot(1),"public controller displays V before testing its Blind entry"); pump();
    const auto selected=[&] {
        const auto s=controller.snapshot();
        require(s.selectedVersionId==id && s.versionSelection,"selected Version survives notes changes");
        require(!s.versions.empty() && s.versionSelection->candidateId.isNotEmpty(),"Version and source candidate remain present");
        return s.versionSelection;
    };
    const auto first=selected();
    require(first->changeNotes && first->changeNotes->notes.size()==1,"controller publishes selected notes through real reader");
    const auto oldNotes=first->changeNotes;
    const auto oldHash=oldNotes->artifactSha256;
    const auto originalText=oldNotes->notes[0].text;
    body["notes"][0].getDynamicObject()->setProperty("text","Updated controller instruction.");
    attach(); publish(2); pump();
    const auto changed=selected();
    require(changed->changeNotes && changed->changeNotes->artifactSha256!=oldHash
        && changed->changeNotes->notes[0].text=="Updated controller instruction.","same entry revision refresh replaces old receipt notes");
    require(oldNotes->artifactSha256==oldHash && oldNotes->notes[0].text==originalText,"retained previous publication is immutable");
    descriptor.getDynamicObject()->removeProperty("change_notes_artifact"); publish(3); pump();
    require(!selected()->changeNotes && selected()->changeNotesIssue.isEmpty(),"removed notes receipt clears published notes only");
    descriptor.getDynamicObject()->setProperty("change_notes_artifact","invalid"); publish(4); pump();
    require(!selected()->changeNotes && selected()->changeNotesIssue.isNotEmpty(),"malformed receipt keeps Version but rejects notes");
    body.getDynamicObject()->setProperty("open_count",999); attach(); publish(5); pump();
    require(!selected()->changeNotes && selected()->changeNotesIssue.isNotEmpty(),"invalid notes body keeps Version but rejects notes");
    body.getDynamicObject()->setProperty("open_count",1); attach(); publish(6); pump();
    require(selected()->changeNotes && selected()->changeNotes->notes[0].text=="Updated controller instruction.","fixed receipt restores notes without reselecting Version");
    std::cout<<"REFERENCE_CHANGE_NOTES_CONTROLLER_RECEIPTS=6 MODE=manual-runtime-service\n";

    // Public VERSION BLIND start after actual PCM correspondence. A failed start
    // is a failing test, never replaced by writing a Blind phase/visibility flag.
    juce::AudioBuffer<float> input(2,256);
    for(int position=0;position<192000;position+=256) {
        for(int ch=0;ch<2;++ch) input.copyFrom(ch,0,fixture.audio,ch,position,256);
        controller.observeTransport(position,true,true);
        controller.observeAInput(input,position,true,true,true);
        control::service(controller); controller.servicePendingAudition(-22,-6,true);
    }
    pump();
    const auto calibrated=controller.snapshot();
    std::cout<<"NOTES_BLIND_PRECONDITION slot="<<calibrated.comparisonSlot<<" ready="<<calibrated.versionReady
        <<" eligible="<<calibrated.blindEligible<<" versionEligible="<<(calibrated.versionSelection && calibrated.versionSelection->blindEligible)<<std::endl;
    require(calibrated.blindEligible,"real PCM correspondence makes VERSION BLIND eligible");
    require(controller.startBlind(-22,-6),"public VERSION BLIND starts for notes secrecy");
    const auto hidden=controller.snapshot();
    require(hidden.versionSelection && !hidden.versionSelection->changeNotes && !hidden.changeNotes,
        "actual VERSION BLIND hides notes from both snapshots");
    controller.endBlind(); pump(); controller.servicePendingAudition(-22,-6,true); pump();
    require(controller.snapshot().versionSelection
        && controller.snapshot().versionSelection->blindPhase==ref::BlindPhase::inactive,"public END finishes notes Blind trial");
    require(selected()->changeNotes!=nullptr,"notes can return only after VERSION BLIND ends");
    std::cout<<"REFERENCE_CHANGE_NOTES_CONTROLLER_BLIND=1 MODE=public-start-manual-runtime-service\n";
}
}
