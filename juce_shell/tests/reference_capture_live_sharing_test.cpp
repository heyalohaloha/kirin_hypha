#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "../src/reference_audition/ReferenceCaptureEvidence.h"
#include <iostream>
void testCaptureLiveSharing();
static void captureLiveSharing(const juce::File& sandbox);
void testCaptureLiveSharing() {
    const auto sandbox=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hypha-b885-review-"+juce::Uuid().toString());
    require(sandbox.createDirectory(),"review sandbox");
    // The controller and its workers end with captureLiveSharing(). Windows cannot delete a file a
    // worker still holds open, so the sandbox is removed only after that, as in the other suites.
    captureLiveSharing(sandbox);
    require(sandbox.deleteRecursively(),"review sandbox cleanup");
}
static void captureLiveSharing(const juce::File& sandbox) {
    const auto root=sandbox.getChildFile("capture-evidence"); require(root.createDirectory(),"capture evidence fixture");
    const auto file=root.getChildFile("version.wav");
    const juce::String recording="22222222-2222-4222-8222-222222222222",version="33333333-3333-4333-8333-333333333333";
    const auto fixture=makeWholeSongFixture(root,file,recording,version);
    auto preset=bindRuntimeV2WorkVersionPresetToSource("88888888-8888-4888-8888-888888888888","99999999-9999-4999-8999-999999999999",
        fixture.receipt,juce::SHA256(file).toHexString(),fixture.pcmHash,recording,version,fixture.audio.getNumSamples());
    auto* p=preset.getDynamicObject(); p->setProperty("format","kirin_hypha_reference_library_preset"); p->setProperty("version","1.0");
    p->removeProperty("work_id"); p->removeProperty("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty("preparation_status","prepared");
    libraryManifest(root,preset,1);
    require(writeJson(root.getChildFile("library/manifest.json"),independentLibraryManifest(root,preset,2)),"independent Version catalog");
    ref::ReferenceComparisonSettings selection; selection.viewedSlot=1;
    selection.version={preset["source_template_artifact"]["preset_id"].toString(),preset["checks"][0]["check_id"].toString(),
        preset["checks"][0]["candidates"][0]["candidate_id"].toString(),preset["checks"][0]["candidates"][0]["default_cue_id"].toString()};
    ref::ReferenceComparisonController controller(root,[](bool){return true;},[](bool){return true;});
    controller.configure({"capture-evidence-post",{},42,true},48000,2); controller.setPresented(true);
    const auto access=controller.snapshot().captureAccess;
    require(access && access->request(ref::ACaptureAccess::start),"Capture A begins without selected B");
    const auto wait=[](const auto& test){ for(int i=0;i<800;++i){ if(test())return true;juce::Thread::sleep(5); }return false; };
    require(wait([&]{return access->active.load();}),"Capture A admitted");
    const auto feed=[&](float gain, bool paceLive=false) {
        if(paceLive)
            require(wait([&]{const auto s=controller.snapshot();return s.visualTimeline
                && s.visualTimeline->observing && s.visualTimeline->pairedObserving;}),
                "LIVE comparison is observing before the synthetic host runs");
        else if (access->snapshot().held)
            require (wait ([&] { controller.setPresented (true);
                return controller.captureObservationReady(); }),
                "local A observation is armed before the synthetic host starts");
        juce::AudioBuffer<float> input(2,4800);
        for(int at=0;at<fixture.audio.getNumSamples();at+=4800) {
            const auto index = size_t (at / 4800);
            const auto unitIndex = size_t (at / 48000);
            const bool unitBoundary = ((at + 4800) % 48000) == 0;
            std::uint64_t previousUnitPass = 0;
            const auto observationBefore = access->snapshot();
            if (unitBoundary && unitIndex < observationBefore.unitPass.size())
                previousUnitPass = observationBefore.unitPass[unitIndex];
            std::uint64_t previousLivePass = 0;
            if (paceLive)
            {
                const auto current = controller.snapshot();
                if (current.visualTimeline && index < current.visualTimeline->bins.size())
                    previousLivePass = current.visualTimeline->bins[index].pass;
            }
            for(int c=0;c<2;++c) input.copyFrom(c,0,fixture.audio,c,at,4800); input.applyGain(gain);
            const auto processedBefore=access->framesProcessed.load(std::memory_order_acquire);
            controller.observeTransport(at,true,true); controller.observeAInput(input,at,true,true,true,1);
            controller.setPresented(true);
            if(access->active.load(std::memory_order_acquire))
                require(wait([&]{return !access->active.load(std::memory_order_acquire)
                    || access->framesProcessed.load(std::memory_order_acquire)>=processedBefore+4800;}),
                    "synthetic host waits for finite Capture A worker capacity");
            else if(paceLive)
            {
                if (! wait ([&] { const auto s = controller.snapshot(); return s.visualTimeline
                    && index < s.visualTimeline->bins.size()
                    && s.visualTimeline->bins[index].pass > previousLivePass; }))
                {
                    const auto s = controller.snapshot();
                    std::cerr << "LIVE worker timeout index=" << index
                              << " timeline=" << bool (s.visualTimeline)
                              << " observing=" << (s.visualTimeline && s.visualTimeline->observing)
                              << " paired=" << (s.visualTimeline && s.visualTimeline->pairedObserving)
                              << " bins=" << (s.visualTimeline ? s.visualTimeline->bins.size() : 0u)
                              << " previous_pass=" << previousLivePass
                              << " current_pass=" << (s.visualTimeline && index < s.visualTimeline->bins.size()
                                                        ? s.visualTimeline->bins[index].pass : 0)
                              << std::endl;
                    require (false, "synthetic host waits for the LIVE comparison worker");
                }
            }
            else if (unitBoundary && observationBefore.held)
                require (wait ([&] { const auto state = access->snapshot(); return unitIndex < state.unitPass.size()
                    && state.unitPass[unitIndex] > previousUnitPass; }),
                    "synthetic host waits for each held-capture comparison unit");
            else if (! observationBefore.held)
                juce::Thread::sleep (12);
            // The held-A comparison and local A observation have independent workers. Do not
            // outrun either bounded queue: each exact 100 ms host block must be consumed before
            // the next block, even when the CI machine is slower than real-time.
            if (observationBefore.held)
                require (wait ([&] { return controller.captureObservationQueueDrained(); }),
                         "synthetic host waits for the local A observation worker");
            if (! paceLive && observationBefore.held && unitBoundary && unitIndex % 4 == 3)
            {
                bool ready = false;
                for (int attempt = 0; attempt < 4000 && ! ready; ++attempt)
                {
                    const auto state = controller.snapshot();
                    ready = state.versionSelection && state.versionSelection->aCaptureAvailable;
                    if (! ready) juce::Thread::sleep (5);
                }
                if (! ready)
                {
                    const auto state = controller.snapshot();
                    const auto& versionState = state.versionSelection ? *state.versionSelection : state;
                    std::cerr << "local A observation timeout: version_state="
                              << static_cast<int> (versionState.state)
                              << " reason=" << versionState.rejectionCode
                              << " selected=" << state.selectedVersionId
                              << " version_candidate=" << versionState.candidateId
                              << " library=" << versionState.libraryReceived
                              << " transport=" << versionState.transportPlaying
                              << " A_capture=" << versionState.aCaptureAvailable
                              << " held=" << bool (access->snapshot().held) << '\n';
                }
                require (ready, "synthetic host waits for the four-second local A observation");
            }
        }
    };
    feed(0.5f); juce::AudioBuffer<float> stopped(2,16); stopped.clear();
    controller.observeAInput(stopped,384000,true,false,true,1);
    require(wait([&]{const auto state=access->snapshot();return !access->active && state.held;}),
        "whole A capture publishes its terminal held snapshot");
    const auto initial=access->snapshot().held;
    require(initial && initial->units.size()==8 && initial->bindings.empty(),"Capture does not need B or fabricate correspondence");
    selection.captureState=controller.savedSettings().captureState; selection.capturedView=true;
    controller.restoreSettings(selection);
    require(controller.savedSettings().captureState==selection.captureState,"configured controller immediately saves pending restoration");
    // 2026-10-04：製品は A を取り込まない。設定（DAW の曲）に入っている取り込みは復元しない（ここで取った A のまま）。
    juce::Thread::sleep(200);
    require(access->snapshot().held && !access->snapshot().held->restored,"a saved Capture in the settings is not restored");
    // 取り込みは製品で使わない（2026-10-04）。保存した取り込みとの照合・V の根拠はここまで。

}
