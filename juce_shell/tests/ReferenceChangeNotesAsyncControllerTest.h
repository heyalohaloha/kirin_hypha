#pragma once
#include "reference_runtime_test_entries.h"
#include "ReferenceChangeNotesControllerTest.h"
#include "ReferenceControllerTestAccess.h"
#include "ReferenceChangeNotesRaceTest.h"
#include "../src/reference_audition/ReferenceTestCommitObserver.h"
#include "ReferenceWholeSongAudioFixture.h"
#include "reference_library_manifest_fixture.h"
#include "../src/reference_audition/ReferenceComparisonController.h"

namespace
{
// Ordinary control workers remain running throughout. No injected publication or manual service.
void verifyChangeNotesAsyncController(const juce::File& sandbox)
{
    namespace control=hypha::tests::reference_control;
    namespace notes=hypha::tests::change_notes;
    const auto root=sandbox.getChildFile("change-notes-async-controller");
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
    juce::String expectedNotesHash;
    const auto attach=[&] {
        const auto r=stageWholeSongArtifact(root,"notes",body);
        expectedNotesHash=r.sha256;
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
    controller.configure({"notes-async-post",{},42,true},48000,2);
    controller.setPresented(true);
    auto& version=control::roleFor(controller,{1});
    control::await([&]{ return !controller.snapshot().versions.empty(); },"ordinary worker discovers Version catalog");
    const auto id=controller.snapshot().versions.front().id;
    require(controller.selectVersion(id,true),"public Version selection accepted");
    require(controller.selectVisualSlot(1),"V view selected");
    const auto snapshot=[&]{ return controller.snapshot(); };
    const auto matches=[&](const juce::String& text) {
        const auto s=snapshot(); const auto v=s.versionSelection;
        return s.selectedVersionId==id && v && v->candidateId.isNotEmpty() && v->changeNotes
            && v->changeNotesSelectionGeneration==v->selectionGeneration
            && v->selectionGeneration==version.requestedGeneration()
            && v->changeNotes->artifactSha256==expectedNotesHash
            && v->changeNotes->notes.size()==1 && v->changeNotes->notes[0].text==text;
    };
    const auto initialText=body["notes"][0]["text"].toString();
    control::await([&]{return matches(initialText);},"ordinary worker publishes initial notes");
    const auto initial=snapshot().versionSelection;
    const auto previous=initial->changeNotes;
    const auto previousText=previous->notes[0].text;
    body["notes"][0].getDynamicObject()->setProperty("text","Updated asynchronous instruction.");
    attach(); publish(2);
    // No forced refresh: this transition proves the normal publication poll sees receipt changes.
    require(expectedNotesHash!=previous->artifactSha256,"changed notes have a distinct receipt hash");
    control::await([&]{return matches("Updated asynchronous instruction.");},
        "natural worker poll updates same Version receipt");
    require(previous->notes[0].text==previousText,"previous publication remains immutable");
    const auto refresh=[&] {
        const auto ticket=ref::requestReferenceTestWorkspaceRefresh(&version);
        require(ticket!=0,"one targeted worker refresh admitted");
        control::await([&]{return ref::referenceTestWorkspaceRefreshConsumed(ticket);},"worker consumes refresh request");
        // Consumption is not publication success; every caller waits for its actual snapshot below.
    };
    const auto empty=[&](bool issue) {
        const auto s=snapshot(); const auto v=s.versionSelection;
        return s.selectedVersionId==id && !s.versions.empty() && v && v->candidateId.isNotEmpty()
            && v->selectionGeneration==version.requestedGeneration()
            && !v->changeNotes && (v->changeNotesIssue.isNotEmpty()==issue);
    };
    descriptor.getDynamicObject()->removeProperty("change_notes_artifact"); publish(3); refresh();
    control::await([&]{return empty(false);},"ordinary worker removes notes without removing Version");
    descriptor.getDynamicObject()->setProperty("change_notes_artifact","invalid"); publish(4); refresh();
    control::await([&]{return empty(true);},"ordinary worker rejects malformed receipt independently");
    // Restore a visible state before another rejection, so the old rejected state cannot satisfy it.
    attach(); publish(5); refresh();
    control::await([&]{return matches("Updated asynchronous instruction.");},"ordinary worker restores valid receipt");
    body.getDynamicObject()->setProperty("open_count",999); attach(); publish(6); refresh();
    control::await([&]{return empty(true);},"ordinary worker rejects bad body after a visible publication");
    body.getDynamicObject()->setProperty("open_count",1); attach(); publish(7); refresh();
    control::await([&]{return matches("Updated asynchronous instruction.");},"ordinary worker recovers valid body");
    std::cout<<"REFERENCE_CHANGE_NOTES_ASYNC_RECEIPTS=7 MODE=ordinary-workers\n";
    verifyChangeNotesSelectionRace(controller,root,manifest.clone(),descriptor.clone(),body.clone(),id);
}
}

void testReferenceChangeNotesContract (const juce::File& sandbox)
{
    verifyChangeNotesRepository(sandbox);
    verifyChangeNotesController(sandbox);
    verifyChangeNotesAsyncController(sandbox);
}
