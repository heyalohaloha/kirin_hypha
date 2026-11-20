#include "TestPathBudget.h"
#include "TestTemporaryDirectory.h"
#include "AtomicFile.h"
#include "reference_audition/ReferenceMetricSetRuntime.h"
#include "reference_audition/ReferenceTestCommitObserver.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

// These fixtures exercise the real source path; no SetRuntime::testWork hook.
namespace hypha::reference_audition
{
void stageReferenceTestWorker (const void*, const char*) noexcept {}
void notifyReferenceTestCommit() noexcept {}
}
namespace ref = hypha::reference_audition;
namespace metric = ref::metrics;
namespace revision = ref::revision_detail;
namespace
{
void require (bool condition, const char* message)
{ if (!condition) throw std::runtime_error (message); }
void trace (const char* caseName, const char* stage, std::size_t step = 0)
{
    static const auto began = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - began).count();
    // Fixed case labels and numeric steps only: never emit receipts or source paths.
    std::cerr << "[metric-set-source] case=" << caseName << " stage=" << stage
              << " step=" << step << " elapsed_s=" << elapsed << std::endl;
}
constexpr int rate = 8000, channels = 2, length = rate * 4;
struct Song
{
    juce::File audio;
    ref::RuntimeCandidate candidate;
    std::shared_ptr<const ref::RuntimeSource> source;
    std::shared_ptr<revision::SourceSnapshot> snapshot;
    std::array<metric::Summary, 3> expected;
};
struct Fixture
{
    juce::File root = hypha::tests::temporaryParent()
        .getNonexistentChildFile ("hmss", "", false);
    ref::RuntimeV2SourceRepository repository {root};
    Fixture() { require (root.getChildFile ("sources").createDirectory().wasOk(), "fixture source directory"); }
    ~Fixture() { hypha::tests::removeFixtureDirectory (root); }
    Song song (int identity, bool corruptAudio = false)
    {
        Song result;
        result.audio = root.getChildFile ("song-" + juce::String (identity) + ".wav");
        if (corruptAudio)
            require (hypha::storage::writeText (result.audio, "not an audio file", false, false, "\n"), "corrupt audio fixture");
        else
        {
            juce::WavAudioFormat format;
            auto stream = result.audio.createOutputStream();
            require (stream != nullptr, "create WAV output");
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                stream.get(), rate, channels, 24, {}, 0));
            require (writer != nullptr, "create WAV writer"); stream.release();
            juce::AudioBuffer<float> pcm (channels, length);
            for (int channel = 0; channel < channels; ++channel)
                for (int i = 0; i < length; ++i)
                {
                    const auto seconds = static_cast<double> (i) / rate;
                    const auto phase = seconds * juce::MathConstants<double>::twoPi * (170.0 + identity * 83.0);
                    const auto envelope = 0.12 + 0.07 * std::sin (seconds * (2.0 + identity));
                    const auto wave = std::sin (phase + channel * 0.2)
                        + 0.09 * identity * std::sin (phase * 3.0 + channel * 0.4);
                    pcm.setSample (channel, i, static_cast<float> (envelope * wave));
                }
            require (writer->writeFromAudioSampleBuffer (pcm, 0, length), "write full WAV"); writer.reset();
        }
        const auto fileHash = juce::SHA256 (result.audio).toHexString();
        const auto pcmHash = juce::String::repeatedString ("e", 64);
        const auto catalog = "catalog:metric-set-" + juce::String (identity);
        ref::RuntimeSource pathSource; pathSource.absolutePath = result.audio.getFullPathName();
        ref::RuntimeFileRevision revision;
        require (repository.readSourceRevision (pathSource, revision), "real fixture file revision");
        // Same exact source receipt schema as makeRuntimeV2Source in the existing
        // reference_runtime_v2_manifest_test_support.h, without its GUI fixtures.
        auto* doc = new juce::DynamicObject();
        doc->setProperty ("format", "kirin_hypha_reference_source"); doc->setProperty ("version", "2.0");
        doc->setProperty ("source_kind", "catalog_track");
        auto* sourceIdentity = new juce::DynamicObject();
        sourceIdentity->setProperty ("catalog_reference_id", catalog);
        sourceIdentity->setProperty ("sha256_file", fileHash); sourceIdentity->setProperty ("sha256_pcm", pcmHash);
        doc->setProperty ("source_identity", juce::var (sourceIdentity));
        auto* fileRevision = new juce::DynamicObject();
        fileRevision->setProperty ("device_id", revision.deviceId); fileRevision->setProperty ("file_id", revision.fileId);
        fileRevision->setProperty ("size_bytes", revision.sizeBytes);
        fileRevision->setProperty ("mtime_ns", revision.modifiedTimeNs); fileRevision->setProperty ("ctime_ns", revision.changedTimeNs);
        auto* file = new juce::DynamicObject();
        file->setProperty ("absolute_path", result.audio.getFullPathName()); file->setProperty ("revision", juce::var (fileRevision));
        doc->setProperty ("file", juce::var (file));
        auto* audio = new juce::DynamicObject();
        audio->setProperty ("sample_rate_hz", rate); audio->setProperty ("channels", channels);
        audio->setProperty ("total_sample_frames", length); doc->setProperty ("audio", juce::var (audio));
        auto* measurement = new juce::DynamicObject();
        measurement->setProperty ("summary", juce::var()); measurement->setProperty ("detail_artifact", juce::var());
        doc->setProperty ("measurement", juce::var (measurement)); doc->setProperty ("alignment", juce::var());
        const auto json = juce::JSON::toString (juce::var (doc), true);
        const auto staging = root.getChildFile ("source.pending.json");
        require (hypha::storage::writeText (staging, json, false, false, "\n"), "write content-addressed source staging");
        const auto hash = juce::SHA256 (staging).toHexString();
        const auto receiptFile = root.getChildFile ("sources").getChildFile (hash + ".json");
        require (staging.moveFileTo (receiptFile), "publish content-addressed source receipt");
        auto& candidate = result.candidate;
        candidate.candidateId = "member-" + juce::String (identity); candidate.displayName = "Song " + juce::String (identity);
        candidate.sourceKind = "catalog_track"; candidate.sourceIdentityKey = catalog + ":" + fileHash + ":" + pcmHash;
        candidate.sourceArtifact = {"plugin_data/reference/v2/sources/" + hash + ".json", hash, receiptFile.getSize()};
        candidate.cues = {{"all", "All", rate, 0, length, false}}; candidate.defaultCueId = "all";
        return result;
    }
    void prepare (Song& song)
    {
        const auto loaded = repository.load (song.candidate);
        require (loaded.accepted(), "receipt and schema load through real repository");
        song.source = loaded.source;
        const auto verified = repository.verifySourceFile (*song.source, false);
        if (verified.isNotEmpty()) throw std::runtime_error ("real source verify: " + verified.toStdString());
        song.snapshot = revision::snapshotCache().prepared (*song.source);
        require (song.snapshot && song.snapshot->hasPrivateCopy(), "source verification prepared actual private snapshot");
    }
};
std::array<metric::Summary, 3> directNative (const Song& song, metric::Cue cue)
{
    // Decode the on-disk quantized WAV independently, then feed the C ABI directly.
    // No AudioPages, SourceScan or SetRuntime aggregation is reused here.
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (song.audio));
    require (reader && reader->lengthInSamples == length && reader->numChannels == channels, "direct WAV decode");
    juce::AudioBuffer<float> pcm (channels, length);
    require (reader->read (&pcm, 0, length, 0, true, true), "direct WAV PCM read");
    using Handle = std::unique_ptr<KirinReferenceMetrics, decltype (&kirin_reference_metrics_drop)>;
    Handle handle (kirin_reference_metrics_create (rate, channels), &kirin_reference_metrics_drop);
    require (handle != nullptr, "direct native engine");
    std::vector<KirinReferenceMetricFrame> frames;
    std::vector<float> interleaved (rate / 10 * channels);
    KirinReferenceMetricFrame output[8] {};
    for (int start = 0; start < length; start += rate / 10)
    {
        for (int i = 0; i < rate / 10; ++i)
            for (int channel = 0; channel < channels; ++channel)
                interleaved[static_cast<std::size_t> (i * channels + channel)] = pcm.getSample (channel, start + i);
        std::size_t count = 0;
        require (kirin_reference_metrics_push (handle.get(), interleaved.data(), interleaved.size(), output, 8, &count)
            && count <= 8, "direct native push");
        frames.insert (frames.end(), output, output + count);
    }
    std::size_t count = 0;
    require (kirin_reference_metrics_finish (handle.get(), output, 8, &count) && count <= 8, "direct native EOF");
    frames.insert (frames.end(), output, output + count);
    require (frames.size() == 40 && frames.back().end_sample == length, "direct native full aperture sequence");
    std::array<metric::Summary, 3> result;
    for (std::size_t metric = 0; metric < result.size(); ++metric)
    {
        // Independent Cue median, not summarise(): checks aggregation too.
        std::vector<double> values;
        for (const auto& frame : frames)
            if (frame.end_sample > cue.start && frame.end_sample <= cue.end)
            {
                const auto value = metric == 0 ? frame.psr_db : metric == 1 ? frame.crest_db : frame.sharpness_acum;
                if (std::isfinite (value)) values.push_back (value);
            }
        require (!values.empty(), "four-second source has finite PSR/CREST/SHARP");
        std::sort (values.begin(), values.end());
        const auto middle = values.size() / 2;
        result[metric] = {values.size() % 2 ? values[middle] : values[middle - 1] * 0.5 + values[middle] * 0.5,
                          values.size(), true};
    }
    return result;
}
std::shared_ptr<const metric::SetPublication> finish (metric::SetRuntime& runtime, const metric::SetRequest& request, const char* caseName)
{
    trace (caseName, "begin");
    // Completion notification, no short sleeps or fake measurement worker. Every
    // fixture source is already verified, so no timed preparation retry is needed.
    for (std::size_t step = 0; step <= request.members.size() + 1; ++step)
    {
        trace (caseName, "service-before", step);
        runtime.service (request);
        trace (caseName, runtime.activeForTest() ? "service-active" : "service-idle", step);
        if (!runtime.activeForTest()) break;
        trace (caseName, "wait-before", step);
        runtime.waitForTest();
        trace (caseName, "wait-after", step);
    }
    runtime.service (request, false);
    const auto result = runtime.snapshot();
    require (result && !runtime.activeForTest(), "all admitted real source jobs drained");
    trace (caseName, "end");
    return result;
}
void bandsMatch (const metric::SetPublication& result, const std::vector<std::array<metric::Summary, 3>>& expected)
{
    require (result.complete && result.completed == expected.size() && result.expected == expected.size(),
             "all original members must finish before set band");
    for (std::size_t i = 0; i < 3; ++i)
    {
        std::vector<double> medians;
        for (const auto& song : expected) medians.push_back (song[i].median);
        std::sort (medians.begin(), medians.end());
        // With 2 or 3 equal-weight songs, nearest-rank p10/p90 are the endpoints.
        require (std::isfinite (result.bands[i].low) && std::isfinite (result.bands[i].high)
            && std::abs (result.bands[i].low - medians.front()) < 1.0e-10
            && std::abs (result.bands[i].high - medians.back()) < 1.0e-10,
                 "AudioPages/native full-set band equals independent direct-native song medians");
    }
}
void sourceSet()
{
    trace ("setup", "begin");
    Fixture fixture;
    std::array<Song, 3> songs {fixture.song (1), fixture.song (2), fixture.song (3)};
    metric::SetRequest request {"three-real-songs", 1, rate, channels, {}};
    std::vector<std::array<metric::Summary, 3>> expected;
    for (auto& song : songs)
    {
        trace ("setup", "prepare-before", expected.size());
        fixture.prepare (song);
        trace ("setup", "direct-native-before", expected.size());
        song.expected = directNative (song, {0, length});
        trace ("setup", "direct-native-after", expected.size());
        expected.push_back (song.expected); request.members.push_back (song.candidate);
    }
    metric::SetRuntime runtime (fixture.root);
    trace ("setup", "end");
    const auto full = finish (runtime, request, "full-set");
    if (!full->complete) throw std::runtime_error ("real full set incomplete: " + full->reason.toStdString());
    bandsMatch (*full, expected);

    // Change generation and Cue without dropping any songs. Full set must withdraw
    // immediately, then reaggregate the same exact PCM under the new Cue identity.
    ++request.generation; request.membershipKey = "three-real-songs-new-cue";
    for (auto& member : request.members) member.cues[0].startSample = rate * 3;
    runtime.service (request, false);
    require (runtime.snapshot()->generation == 2 && !runtime.snapshot()->complete
        && std::isnan (runtime.snapshot()->bands[0].low), "new generation cannot inherit old ready band");
    trace ("new-cue-expected", "begin");
    expected.clear();
    for (const auto& song : songs) expected.push_back (directNative (song, {rate * 3, length}));
    trace ("new-cue-expected", "end");
    bandsMatch (*finish (runtime, request, "new-cue"), expected);

    // A complete old job not yet accepted must not publish after the generation changes.
    metric::SetRuntime changed (fixture.root);
    trace ("old-generation", "service-before");
    changed.service (request);
    trace ("old-generation", "wait-before");
    changed.waitForTest();
    trace ("old-generation", "wait-after");
    ++request.generation; request.membershipKey = "generation-replaced-before-accept";
    changed.service (request, false);
    require (changed.snapshot()->generation == 3 && changed.snapshot()->completed == 0
        && !changed.snapshot()->complete, "completed old-generation result rejected before publication");
    bandsMatch (*finish (changed, request, "replaced-generation"), expected);

    trace ("failure-fixtures", "begin");
    auto missing = fixture.song (4);
    hypha::tests::recordPathsBeforeRemoval (missing.audio);
    require (missing.audio.deleteFile(), "remove only fixture source");
    auto corrupt = fixture.song (5, true);
    require (fixture.repository.load (corrupt.candidate).accepted(), "broken audio still has valid source receipt");
    trace ("failure-fixtures", "end");
    std::size_t failureCase = 0;
    for (const auto& failed : {missing.candidate, corrupt.candidate})
    {
        metric::SetRequest unavailable {"set-with-failed-member-" + failed.candidateId, 10, rate, channels,
            {songs[0].candidate, songs[1].candidate, failed}};
        metric::SetRuntime withFailure (fixture.root);
        const auto partial = finish (withFailure, unavailable, failureCase++ == 0 ? "missing-member" : "corrupt-member");
        require (!partial->complete && partial->expected == 3 && partial->completed == 2 && partial->reason.isNotEmpty(),
                 "missing or corrupt source stays in set denominator");
        for (const auto& band : partial->bands)
            require (std::isnan (band.low) && std::isnan (band.high), "partial subset cannot fabricate a narrower band");
    }
    runtime.clear(); changed.clear();
    trace ("all-cases", "end");
}
}
int main()
{
    try { sourceSet(); std::cout << "real source metric set PASS\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
