#include "reference_runtime_test_support.h"
#include "reference_runtime_test_entries.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "ReferenceWholeSongInputClock.h"
#include "reference_rt_probe.h"
#include "TestTemporaryDirectory.h"
#include "../src/reference_audition/ReferenceContentAlignment.h"
#include "../src/reference_audition/ReferenceAudioPages.h"

namespace
{
void equalChannels (const std::vector<float>& pcm)
{
    require (! pcm.empty() && pcm.size() % 2 == 0, "complete stereo probe");
    bool audible = false;
    for (size_t i = 0; i < pcm.size(); i += 2)
    {
        require (std::memcmp (&pcm[i], &pcm[i + 1], sizeof (float)) == 0, "dual mono has bit-identical channels");
        audible = audible || std::abs (pcm[i]) > 0.001f;
    }
    require (audible, "channel equality also contains actual audio");
}
void contentAndBlind (const juce::File& root, const juce::String& mode)
{
    require (root.createDirectory().wasOk(), "isolated mono source directory");
    const auto file = root.getChildFile ("mono.wav");
    const auto fixture = makeWholeSongFixture (root, file, "22222222-2222-4222-8222-222222222222",
                                               "33333333-3333-4333-8333-333333333333", 1);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
    require (reader != nullptr, "mono source opens");
    if (mode == "probe" || mode == "all")
    {
        for (int rate : {48000, 44100})
        {
            std::vector<float> pcm;
            require (ref::readReferenceProbe (*reader, 48000, rate, rate, 2, pcm), "mono probe plays on stereo at native and converted rates");
            equalChannels (pcm);
        }
        const auto stereoFile = root.getChildFile ("stereo.wav");
        makeWholeSongFixture (root, stereoFile, "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
        auto stereo = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (stereoFile));
        std::vector<float> refused;
        require (stereo && ! ref::readReferenceProbe (*stereo, 0, 4800, 48000, 1, refused), "stereo probe remains refused on mono");
        if (mode == "probe") return;
    }
    auto source = std::make_shared<ref::RuntimeSource>();
    source->sourceKind = "work_version"; source->absolutePath = file.getFullPathName();
    source->sourceFileSha256 = juce::SHA256 (file).toHexString(); source->sourcePcmSha256 = fixture.pcmHash;
    source->audio = {48000, 1, fixture.audio.getNumSamples()};
    source->measurementSummary.emplace(); source->measurementSummary->maximumTruePeakDbtp = -6;
    ref::RuntimeDetailedMeasurement measured;
    measured.audio = source->audio; measured.sourceFileSha256 = source->sourceFileSha256; measured.sourcePcmSha256 = fixture.pcmHash;
    measured.waveform.emplace(); measured.waveform->framesPerBin = 4800; measured.waveform->rmsMillidbfs.resize (1);
    for (int at = 0; at < fixture.audio.getNumSamples(); at += 4800)
        measured.waveform->rmsMillidbfs[0].push_back (static_cast<std::int64_t> (std::llround (
            20000 * std::log10 (fixture.audio.getRMSLevel (0, at, 4800)))));
    auto a = std::make_shared<ref::RuntimeACaptureAudio>();
    a->sampleRateHz = 48000; a->channels = 2; a->startSample = 96000; a->frameCount = 192000;
    a->dawRevisionId = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    for (int f = 0; f < a->frameCount; ++f)
        for (int c = 0; c < 2; ++c) a->interleaved.push_back (fixture.audio.getSample (0, 48000 + f) * 0.5f);
    a->cuePcmSha256 = ref::referenceProbePcmHash (a->interleaved);
    ref::RuntimeV2Blind blind;
    if (mode == "alignment" || mode == "all")
    {
        const auto alignment = ref::alignReferenceContent (*a, *source, measured, false);
        require (alignment.established && alignment.sourceStartSample == 48000, "mono Version aligns to the known stereo passage");
        require (alignment.alignedProbe.size() == a->interleaved.size(), "aligned stereo probe covers the full observation");
        equalChannels (alignment.alignedProbe);
        require (blind.prepareWholeSong (*a, *source, alignment), "mono Version prepares whole-song Blind on stereo");
        require (std::abs (blind.snapshot().pairedLoudnessDeltaDb + 6.0206) < 0.01, "initial gain measures the audible dual-mono PCM");
        ref::AudioPages pages {ref::AudioPages::ServiceMode::manual};
        require (pages.open (*source, 48000, 2, false).isEmpty() && blind.start(), "whole-song mono audition starts explicitly");
        juce::AudioBuffer<float> output (2, 512);
        for (int stimulus : {1, 2})
        {
            if (stimulus == 2) require (blind.requestStimulus (stimulus), "anonymous mono stimulus can be selected");
            for (int n = 0; n < 8; ++n)
            {
                const int at = 48000 + n * 512;
                pages.request (at); pages.service(); output.clear();
                beginReferenceRtProbe();
                const bool rendered = blind.render (output, a->startSample + n * 512, true, &pages, at);
                const auto heap = endReferenceRtProbe();
                require (rendered && heap == 0, "mono Blind render has no callback heap operations");
                for (int f = 0; f < 512; ++f)
                    require (std::memcmp (output.getReadPointer (0) + f, output.getReadPointer (1) + f, sizeof (float)) == 0, "mono Blind renders equal stereo channels");
            }
        }
        blind.forceClearAfterAudioStopped();
        if (mode == "alignment") return;
    }
    ref::RuntimeCandidate candidate;
    candidate.sourceKind = "work_version"; candidate.sourceWorkId = workId;
    candidate.sourceRecordingId = "22222222-2222-4222-8222-222222222222";
    candidate.sourceVersionId = "33333333-3333-4333-8333-333333333333";
    const ref::RuntimeCue cue {"cccccccc-cccc-4ccc-8ccc-cccccccccccc", "Passage", 48000, 0, 384000, false};
    require (blind.prepare (a, candidate, cue, source, false), "mono work-bound Blind prepares on stereo");
    require (std::abs (blind.snapshot().bGainDb + 6.0206) < 0.01, "work-bound Blind measures audible dual mono without a second offset");
    require (blind.start(), "work-bound mono Blind starts explicitly");
    juce::AudioBuffer<float> output (2, 512); output.clear();
    beginReferenceRtProbe();
    const auto rendered = blind.render (output, a->startSample, true);
    const auto heap = endReferenceRtProbe();
    require (rendered && heap == 0 && output.getMagnitude (0, 512) > 0.001f, "work-bound mono Blind renders actual audio without callback heap operations");
    for (int f = 0; f < 512; ++f)
        require (std::memcmp (output.getReadPointer (0) + f, output.getReadPointer (1) + f, sizeof (float)) == 0,
                 "work-bound frozen mono PCM has identical stereo channels");
    blind.forceClearAfterAudioStopped();
    auto inconsistent = std::make_shared<ref::RuntimeSource> (*source); inconsistent->audio.channels = 3;
    require (! blind.prepare (a, candidate, cue, inconsistent, false), "other declared channel layouts remain refused");
    std::cout << "mono source probe/alignment/Blind PASS\n";
}
void versionFollow (const juce::File& root, int trackChannels, int sourceChannels = 1)
{
    require (root.createDirectory().wasOk(), "isolated Version tracking directory");
    const auto file = root.getChildFile ("version.wav");
    const juce::String recording = "22222222-2222-4222-8222-222222222222", version = "33333333-3333-4333-8333-333333333333";
    const auto fixture = makeWholeSongFixture (root, file, recording, version, sourceChannels, true);
    const auto fileHash = juce::SHA256 (file).toHexString();
    auto preset = bindRuntimeV2WorkVersionPresetToSource ("88888888-8888-4888-8888-888888888888",
        "99999999-9999-4999-8999-999999999999", fixture.receipt, fileHash, fixture.pcmHash,
        recording, version, fixture.audio.getNumSamples());
    auto* object = preset.getDynamicObject(); object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0"); object->removeProperty ("work_id"); object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    libraryManifest (root, preset, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), independentLibraryManifest (root, preset, 2)), "publish measured Version catalog");
    ref::ReferenceComparisonSettings settings; settings.viewedSlot = 1;
    settings.version = {preset["source_template_artifact"]["preset_id"].toString(), preset["checks"][0]["check_id"].toString(),
        preset["checks"][0]["candidates"][0]["candidate_id"].toString(), preset["checks"][0]["candidates"][0]["default_cue_id"].toString()};
    ref::ReferenceComparisonController controller (root);
    controller.restoreSettings (settings); controller.configure ({"mono-version-follow", {}, 42, true}, 48000, trackChannels);
    controller.setPresented (true);
    namespace control = hypha::tests::reference_control;
    const auto watchdog = [] { return std::chrono::steady_clock::now() + std::chrono::minutes (5); };
    require (control::waitUntil (watchdog(), [&] { return ! controller.snapshot().versions.empty(); }), "Version catalog publishes");
    if (sourceChannels == 2 && trackChannels == 1)
    {
        require (control::waitUntil (watchdog(), [&] {
            const auto state = controller.snapshot();
            return state.versionSelection && state.versionSelection->rejectionCode == "channel_layout_unsupported";
        }), "stereo Version remains refused on a mono track");
        require (! controller.snapshot().bSelected, "refused Version leaves A selected");
        return;
    }
    int cursor = 0; WholeSongInputClock clock;
    require (clock.waitFor (controller, fixture, cursor,
        [] (const auto& state) { return state.versionReady; }, trackChannels), "Version calibration worker completes");
    require (controller.snapshot().versionReady, "mono Version finishes acoustic calibration");
    controller.observeTransport (std::max (0, cursor - wholeSongObservationFrames), true, true);
    require (controller.selectB (-22, -6), "calibrated mono Version can be selected explicitly");
    const auto audible = control::roleFor (controller, {1}).snapshot();
    require (std::abs (audible.sourceLoudnessOffsetLu - (trackChannels == 2 ? 10 * std::log10 (2.0) : 0.0)) < 1.0e-6,
             "Version tracking carries exactly the audible channel offset");
    const auto initial = controller.snapshot().appliedGainDb;
    require (std::abs (initial) < 0.01, "identical audible content initially matches at unity gain");
    std::vector<KirinMeterHistoryEntry> history;
    const auto aLufs = trackChannels == 2 ? -16.98970004336 : -20.0;
    for (int n = 0; n < 40; ++n)
    {
        KirinMeterHistoryEntry entry {};
        entry.measurement_epoch = 3; entry.run_id = 7; entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        entry.first_timeline_endpoint_samples = entry.last_timeline_endpoint_samples = (n + 1) * 4800;
        entry.lufs_m = {aLufs, aLufs, aLufs}; history.push_back (entry);
    }
    require (controller.followAudition (history, -6) == ref::TrackingAction::keep
        && std::abs (controller.snapshot().appliedGainDb - initial) < 0.01, "mono Version first follow does not jump by the dual-mono loudness difference");
    for (auto& entry : history) entry.lufs_m = {aLufs + 1, aLufs + 1, aLufs + 1};
    require (controller.followAudition (history, -6) == ref::TrackingAction::move
        && std::abs (controller.snapshot().appliedGainDb - 1.0) < 0.01, "a real one-dB change still follows");
    require (juce::SHA256 (file).toHexString() == fileHash, "Version audition preserves the source bytes");
    controller.selectA();
    std::cout << "mono Version follow on " << trackChannels << " channels PASS\n";
}
}

void testReferenceMonoVersion (const juce::File& sandbox, const juce::String& mode)
{
    require (mode == "all" || mode == "probe" || mode == "alignment" || mode == "blind" || mode == "follow" || mode == "refusal",
             "select a known mono regression group");
    if (mode == "all" || mode == "probe" || mode == "alignment" || mode == "blind")
        contentAndBlind (sandbox.getNonexistentChildFile ("mono-version", {}, false), mode);
    if (mode == "all" || mode == "follow")
        for (int channels : {2, 1}) versionFollow (sandbox.getNonexistentChildFile ("mono-follow", {}, false), channels);
    if (mode == "all" || mode == "refusal") versionFollow (sandbox.getNonexistentChildFile ("mono-refusal", {}, false), 1, 2);
}

bool runReferenceMonoInvocation (int argc, char** argv)
{
    if (argc != 2 || ! juce::String (argv[1]).startsWith ("--mono-reference-")) return false;
    const auto root = hypha::tests::temporaryParent().getNonexistentChildFile ("mono", {}, false);
    testReferenceMonoVersion (root, juce::String (argv[1]).fromFirstOccurrenceOf ("--mono-reference-", false, false));
    require (hypha::tests::removeFixtureDirectory (root), "mono regression fixture cleanup");
    return true;
}
