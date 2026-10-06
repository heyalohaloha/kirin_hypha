// B・V の追従 gain（1 秒ごと・0.5 dB 以内は動かさない・50 ms の ramp・上限で止めて知らせる）と、
// C の固定 Match（Kirin OS の Cue の値 × A の同じ長さ）。
#include "reference_runtime_v2_analysis_test_support.h"
#include "KirinLibraryFixture.h"
#include "../src/reference_audition/ReferenceComparisonController.h"
#include "reference_whole_song_fixture.h"
#include "reference_library_manifest_fixture.h"
#include "reference_rt_probe.h"
#include "../src/reference_audition/ReferenceCueMatch.h"
#include "../src/reference_audition/ReferenceSourceRanges.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"
#include "../src/reference_audition/ReferenceTrackingGain.h"

void testReferenceTrackingRules();
void testReferenceCueMatch (const juce::File&);

namespace
{
constexpr std::int64_t unknownTimeline = std::numeric_limits<std::int64_t>::min();

KirinMeterHistoryEntry block (double lufs, std::int64_t timelineEnd = unknownTimeline, std::uint64_t run = 7)
{
    KirinMeterHistoryEntry value {};
    value.measurement_epoch = 3;
    value.run_id = run;
    value.observation_count = 1;
    value.resolution = KIRIN_METER_HISTORY_10_HZ;
    value.first_timeline_endpoint_samples = value.last_timeline_endpoint_samples = timelineEnd;
    value.lufs_m = { lufs, lufs, lufs };
    value.true_peak = { -3.0, -3.0, -3.0 };
    return value;
}

std::vector<KirinMeterHistoryEntry> steady (double lufs, int count = 100, std::uint64_t run = 7)
{
    return std::vector<KirinMeterHistoryEntry> (static_cast<std::size_t> (count), block (lufs, unknownTimeline, run));
}

// Windows の windef.h は near と far を空のマクロにするので、名前に使わない。
bool closeTo (double value, double expected, double tolerance = 1.0e-9) { return std::abs (value - expected) <= tolerance; }

void rampsAndSteps()
{
    namespace r = ref;
    require (closeTo (r::referenceGainHeadroomDb (-6.0, -2.0), 5.0) && closeTo (r::referenceGainHeadroomDb (-6.0, -0.5), 5.5)
                 && closeTo (r::referenceGainHeadroomDb (-0.5, -3.0), 0.0) && closeTo (r::referenceGainHeadroomDb (NAN, -3.0), 0.0),
             "the ceiling is the louder of -1 dBTP, A's peak and the reference's own peak");
    require (r::trackingStep (2.3, 2.0, -6.0, -2.0).action == r::TrackingAction::keep
                 && r::trackingStep (NAN, 2.0, -6.0, -2.0).action == r::TrackingAction::keep
                 && r::trackingStep (150.0, 2.0, -6.0, -2.0).action == r::TrackingAction::keep,
             "within 0.5 dB, or nothing measured, keeps the gain");
    const auto down = r::trackingStep (-10.0, 2.0, -6.0, -2.0);
    const auto up = r::trackingStep (4.5, 2.0, -6.0, -2.0);
    const auto withinShortfall = r::trackingStep (5.5, 2.0, -6.0, -2.0);  // 上限（+5.0）に 0.5 dB 届かない
    const auto over = r::trackingStep (5.6, 2.0, -6.0, -2.0);  // 0.6 dB 届かない
    require (down.action == r::TrackingAction::move && closeTo (down.gainDb, -10.0)
                 && up.action == r::TrackingAction::move && closeTo (up.gainDb, 4.5) && closeTo (up.shortfallDb, 0.0)
                 && withinShortfall.action == r::TrackingAction::move && closeTo (withinShortfall.gainDb, 5.0, 1.0e-9) && closeTo (withinShortfall.shortfallDb, 0.5, 1.0e-9)
                 && over.action == r::TrackingAction::stopCeiling,
             "lowering always moves; raising moves within the ceiling, stops at the ceiling 0.5 dB short at most, "
             "and otherwise stops following");
    require (closeTo (r::referencePeakShortfallDb (5.5, -6.0, -2.0), 0.5, 1.0e-9) && r::referencePeakShortfallDb (4.0, -6.0, -2.0) == 0.0
                 && closeTo (r::referencePeakShortfallDb (10.0, -2.0, -20.0, -8.0), 1.0, 1.0e-9),
             "the shortfall is how far the gain would pass the ceiling, after any A lowering");
    require (r::trackingStep (-4.0, 2.0, -6.0, -2.0, 2.0).action == r::TrackingAction::move
                 && r::trackingStep (-4.5, 2.0, -6.0, -2.0, 2.0).action == r::TrackingAction::stopRange
                 && r::trackingStep (-4.5, 2.0, -6.0, -2.0).action == r::TrackingAction::move,
             "following stays within 6 dB of the MATCH gain (as live compare AUTO) and stops beyond it");

    r::TrackingGainRamp ramp;
    ramp.settle (1.0f);
    float first = ramp.next (2.0f, 2400), middle = 0.0f, last = 0.0f;
    for (int frame = 2; frame <= 2400; ++frame) (frame == 1200 ? middle : last) = ramp.next (2.0f, 2400);
    require (closeTo (first, 1.0 + 1.0 / 2400.0, 1.0e-6) && closeTo (middle, 1.5, 1.0e-6) && closeTo (last, 2.0, 0.0)
                 && closeTo (ramp.next (2.0f, 2400), 2.0, 0.0),
             "a new gain arrives on a straight 50 ms line and then stays");
    ramp.next (1.0f, 2400);
    ramp.settle (0.5f);
    require (closeTo (ramp.next (0.5f, 2400), 0.5, 0.0), "while nothing is heard the gain is set at once");
}

void pairsVersionContent()
{
    // V は A より 2 秒先（位置合わせ：DAW の位置 + 96000 = V の位置）。V の LUFS-M は A より 3 dB 小さい。
    auto source = std::make_shared<ref::RuntimeSource>();
    source->audio = { 48'000, 2, 48'000 * 20 };
    auto overview = std::make_shared<ref::RuntimeDetailedMeasurement>();
    ref::RuntimeMeasurementTimeline loudness;
    loudness.hopSamples = 4'800;
    ref::RuntimeNullableIntegerSeries series (200);
    for (std::size_t index = 0; index < series.size(); ++index)
        series[index] = -17'000 - static_cast<std::int64_t> (index % 3) * 100;
    loudness.series.emplace ("lufs_m_millilu", series);
    overview->loudness = loudness;
    ref::VisualBinding binding;
    binding.source = source;
    binding.overview = overview;
    binding.hostRate = 48'000;
    binding.hostAnchor = 0;
    binding.sourceAnchor = 96'000;
    binding.aligned = true;
    std::vector<KirinMeterHistoryEntry> history;
    for (int index = 0; index < 100; ++index)
    {
        const auto end = static_cast<std::int64_t> (index + 1) * 4'800;
        const auto vValue = static_cast<double> (*series[static_cast<std::size_t> ((end + 96'000) / 4'800 - 1)]) / 1000.0;
        history.push_back (block (vValue + 3.0, index == 50 ? unknownTimeline : end));
    }
    const auto paired = ref::pairedWindowLoudness (history, binding);
    require (paired.pairs == 99 && closeTo (paired.a.lufs - paired.reference.lufs, 3.0, 1.0e-9),
             "A and V are compared on the same content, and an unknown position is left out");
    binding.aligned = false;
    require (ref::pairedWindowLoudness (history, binding).pairs == 0, "without alignment V keeps its gain");
}
}

void testReferenceTrackingRules()
{
    rampsAndSteps();
    pairsVersionContent();
    std::cout << "Reference tracking rules PASS\n";
}

namespace
{
// Kirin OS が書いた本物の ranges から、B セットの曲の「サビ候補」の Cue の値を読む。
void readsKirinOsCueLevel (const juce::File& sandbox)
{
    const auto root = kirin_library_fixture::copy (sandbox, "cue-level");
    require (root != juce::File(), "the Kirin OS library fixture must be copied");
    ref::RuntimeV2Repository repository (root);
    const auto loaded = repository.refreshLibrary();
    require (loaded.usable() && loaded.workspace->librarySets, "the fixture library must be read");
    const auto& song = loaded.workspace->librarySets->songSets[0].songs[0];
    ref::RuntimeSource source;
    source.sourceFileSha256 = juce::String::repeatedString ("e", 64);
    source.sourcePcmSha256 = juce::String::repeatedString ("f", 64);
    source.audio = { 48'000, 2, 96'000 };
    const auto level = ref::readCueLevel (root, *loaded.workspace, song, song.cues[0], source);
    require (level && closeTo (level->integratedLoudness, -14.06) && closeTo (level->maximumTruePeakDbtp, -1.002),
             "the chorus Cue's Integrated and True Peak come from Kirin OS");
    // 2026-10-04：Cue が曲のどの部分か。名前は「サビ候補」（訳された名前）でも、自動区間の回と同じ範囲ならサビ。
    require (level->part == ref::CuePart::chorus, "a Cue on a chorus occurrence is the chorus, whatever its name");
    int chorusSongs = 0, unknownSongs = 0;
    for (const auto& [entry, facts] : loaded.workspace->librarySets->songFacts)
    {
        chorusSongs += facts.part == ref::CuePart::chorus && closeTo (facts.partStartSeconds, 0.2) && closeTo (facts.partEndSeconds, 0.8);
        unknownSongs += facts.part == ref::CuePart::unknown;
    }
    require (chorusSongs == 1 && unknownSongs == 1,
             "a prepared B song says which part its default Cue is, with its times; a song still being prepared says nothing");
    ref::RuntimeSourceRanges sections;
    sections.totalSampleFrames = 96'000;
    sections.sectionFamilies.push_back ({ true, 0, 1'500, 640, { { 9'600, 38'400 } } });
    require (ref::cuePartOf (sections, 0, 96'000, "Mine") == ref::CuePart::whole
                 && ref::cuePartOf (sections, 9'600, 38'400, "Verse") == ref::CuePart::chorus
                 && ref::cuePartOf (sections, 4'800, 52'800, "Loudest 30 s") == ref::CuePart::loudest
                 && ref::cuePartOf (sections, 4'800, 52'800, "Verse") == ref::CuePart::cue
                 && ref::cuePartOf (sections, 9'600, 9'600, "Chorus candidate") == ref::CuePart::unknown,
             "the part comes from the range; only Kirin OS's own loudest 30 s is known by its name");
    auto other = song.cues[0];
    other.endSample -= 4'800;
    auto foreign = source;
    foreign.sourcePcmSha256 = juce::String::repeatedString ("0", 64);
    require (! ref::readCueLevel (root, *loaded.workspace, song, other, source)
                 && ! ref::readCueLevel (root, *loaded.workspace, song, song.cues[0], foreign),
             "a Cue Kirin OS did not measure, or values for other audio, are not used");
    auto withoutSets = *loaded.workspace;
    withoutSets.librarySets.reset();
    require (! ref::readCueLevel (root, withoutSets, song, song.cues[0], source), "without sets.json the song value is used");
    require (ref::cueWindowBlocks (0, 48'000 * 30, 48'000) == 300 && ref::cueWindowBlocks (0, 48'000 * 4, 48'000) == 100
                 && ref::cueWindowBlocks (0, 48'000 * 900, 48'000) == 6'000,
             "C measures A over the Cue's length: at least 10 s, at most the 10 minutes of history");
}

juce::var cueRanges (const juce::String& fileHash, const juce::String& pcmHash)
{
    auto* content = new juce::DynamicObject();
    content->setProperty ("sha256_file", fileHash);
    content->setProperty ("sha256_pcm", pcmHash);
    auto* audio = new juce::DynamicObject();
    audio->setProperty ("sample_rate_hz", 48'000);
    audio->setProperty ("channels", 2);
    audio->setProperty ("total_sample_frames", 96'000);
    auto* range = new juce::DynamicObject();
    range->setProperty ("start_sample", 0);
    range->setProperty ("end_sample", 96'000);
    range->setProperty ("lufs_i_millilu", -16'000);
    range->setProperty ("max_true_peak_millidbtp", -4'000);
    // Cue の 64 帯域（中央値は帯域ごとに −20 − band dB、p10・p90 は ±3 dB）と 4 帯域 Balance。
    juce::Array<juce::var> centers, p10, median, p90;
    for (int band = 0; band < 64; ++band)
    {
        centers.add (20.0 * std::pow (1'000.0, band / 63.0));
        median.add ((-20 - band) * 1'000);
        p10.add ((-23 - band) * 1'000);
        p90.add ((-17 - band) * 1'000);
    }
    auto* spectrum = new juce::DynamicObject();
    spectrum->setProperty ("frame_count", 20);
    spectrum->setProperty ("p10_millidbfs", p10);
    spectrum->setProperty ("median_millidbfs", median);
    spectrum->setProperty ("p90_millidbfs", p90);
    range->setProperty ("spectrum", juce::var (spectrum));
    range->setProperty ("balance_millidbfs", juce::Array<juce::var> { -21'000, -18'500, -24'250, -36'000 });
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "kirin_hypha_reference_ranges");
    root->setProperty ("version", "1.3");
    root->setProperty ("loudness_standard", "itu_r_bs_1770");
    root->setProperty ("source_content", juce::var (content));
    root->setProperty ("audio", juce::var (audio));
    root->setProperty ("spectrum_band_centers_hz", centers);
    root->setProperty ("balance_edges_hz", juce::Array<juce::var> { 20, 250, 2'000, 8'000, 20'000 });
    root->setProperty ("sections", juce::var());
    root->setProperty ("fingerprint", juce::var());
    root->setProperty ("ranges", juce::Array<juce::var> { juce::var (range) });
    return juce::var (root);
}

juce::var setsFor (const juce::String& sourceSha, const ref::RuntimeContentReceipt& ranges)
{
    auto* receipt = new juce::DynamicObject();
    receipt->setProperty ("relative_path", ranges.relativePath);
    receipt->setProperty ("sha256", ranges.sha256);
    receipt->setProperty ("bytes", ranges.bytes);
    auto* entry = new juce::DynamicObject();
    entry->setProperty ("source_artifact_sha256", sourceSha);
    entry->setProperty ("ranges_artifact", juce::var (receipt));
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "kirin_hypha_reference_library_sets");
    root->setProperty ("version", "1.0");
    root->setProperty ("revision", 1);
    root->setProperty ("manifest_revision", 1);
    root->setProperty ("song_sets", juce::Array<juce::var>());
    root->setProperty ("check_sets", juce::Array<juce::var>());
    root->setProperty ("source_ranges", juce::Array<juce::var> { juce::var (entry) });
    return juce::var (root);
}

// 曲（一定の 0.1）を Cue の値 −16 LUFS / −4 dBTP（曲全体は −18 / −6）で置き、C の固定と B の追従を通す。
void matchesAndFollows (const juce::File& sandbox)
{
    const auto root = sandbox.getChildFile ("abcv-tracking");
    require (root.createDirectory().wasOk(), "tracking fixture directory");
    const auto file = root.getChildFile ("cue.wav");
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48'000, 2, 32, {}, 0));
        juce::AudioBuffer<float> data (2, 96'000);
        for (int channel = 0; channel < 2; ++channel) juce::FloatVectorOperations::fill (data.getWritePointer (channel), 0.1f, 96'000);
        require (writer && writer->writeFromAudioSampleBuffer (data, 0, 96'000), "known Cue samples");
    }
    const auto fileHash = juce::SHA256 (file).toHexString();
    const auto pcmHash = juce::String::repeatedString ("7", 64);
    auto sourceValue = makeRuntimeV2Source (file, fileHash, pcmHash);
    addRuntimeV2MeasurementSummary (sourceValue, -18.0, -6.0);
    const auto source = stageRuntimeV2Artifact (root, "sources", sourceValue);
    auto preset = bindRuntimeV2PresetToSource ("88888888-8888-4888-8888-888888888888",
                                               "99999999-9999-4999-8999-999999999999", source, fileHash, pcmHash);
    auto* object = preset.getDynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_preset");
    object->setProperty ("version", "1.0");
    object->removeProperty ("work_id");
    object->removeProperty ("source_preset_artifact");
    preset["checks"][0]["candidates"][0].getDynamicObject()->setProperty ("preparation_status", "prepared");
    const auto ranges = stageRuntimeV2Artifact (root, "ranges", cueRanges (fileHash, pcmHash));
    require (writeJson (root.getChildFile ("library/sets.json"), setsFor (source.sha256, ranges))
                 && writeJson (root.getChildFile ("library/manifest.json"), libraryManifest (root, preset, 1)),
             "the library with Cue values must be published");

    ref::RuntimeV2Controller controller (root, [] (bool) { return true; });
    controller.configure ({ "abcv-tracking", {}, 42, true }, 48'000.0, 2);
    std::int64_t position = 0;
    juce::AudioBuffer<float> buffer (2, 480);
    const auto render = [&]
    {
        buffer.clear();  // A is silent: the output is B alone once its entry fade is done
        beginReferenceRtProbe();
        controller.observeTransport (position, true, true);
        const bool rendered = controller.renderSelectedB (buffer, position, true);
        require (endReferenceRtProbe() == 0, "following adds no heap operation to the audio callback");
        position += buffer.getNumSamples();
        return rendered;
    };
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        controller.observeTransport (position, true, true);
        const auto state = controller.snapshot();
        if (state.state == ref::RuntimeState::ready && state.auditionBuffered && state.cueLevelAvailable) break;
        juce::Thread::sleep (10);
    }
    const auto ready = controller.snapshot();
    require (ready.cueLevelAvailable && closeTo (ready.cueIntegratedLoudness, -16.0) && closeTo (ready.cueMaximumTruePeakDbtp, -4.0)
                 && ready.cueWindowBlocks == 100,
             "the selected Cue's Kirin OS values are available to MATCH");
    require (ready.cueSpectrum && ready.cueSpectrum->centersHz.size() == 64 && ready.cueSpectrum->frames == 20
                 && closeTo (ready.cueSpectrum->medianDb[5], -25.0) && closeTo (ready.cueSpectrum->p10Db[5], -28.0)
                 && closeTo (ready.cueSpectrum->p90Db[63], -80.0) && closeTo (ready.cueSpectrum->balanceDb[2], -24.25)
                 && closeTo (ready.cueStartSeconds, 0.0) && closeTo (ready.cueEndSeconds, 2.0)
                 && closeTo (ready.sourceDurationSeconds, 2.0),
             "the Cue's spectrum, Balance and place in the song reach the C page");

    // C：Cue の値で合わせ（曲全体の −18 なら +4 dB のところ +2 dB）、聴いているあいだ動かない。
    require (controller.selectB (-14.0, -2.0), "C matches with the Cue value");
    auto state = controller.snapshot();
    require (closeTo (state.appliedGainDb, 2.0) && closeTo (state.adjustedBIntegratedLoudness, -14.0)
                 && closeTo (state.adjustedBMaximumTruePeakDbtp, -2.0) && state.tracking == ref::TrackingState::fixed,
             "C uses the Cue's Integrated and True Peak, and its gain is fixed");
    render(); render();
    require (closeTo (buffer.getSample (0, 479), 0.1 * std::pow (10.0, 2.0 / 20.0), 1.0e-6), "C sounds at the matched gain");
    require (controller.followSelection (steady (-20.0), -2.0) == ref::TrackingAction::keep
                 && closeTo (controller.snapshot().appliedGainDb, 2.0),
             "C never follows");
    // MATCH をもう一度。今の A の窓（−13.5）で +2.5 dB に決め直して固定する。上限を超える +6 dB には
    // 動かさず理由を返し、A の窓が足りないときも今の gain を保つ。
    require (controller.rematch (-13.5, -2.0) == ref::RematchResult::matched, "MATCH again re-fixes C");
    state = controller.snapshot();
    require (closeTo (state.appliedGainDb, 2.5) && closeTo (state.aIntegratedLoudness, -13.5)
                 && closeTo (state.adjustedBMaximumTruePeakDbtp, -1.5) && state.tracking == ref::TrackingState::fixed,
             "C's new gain comes from the current A window and stays fixed");
    for (int block = 0; block < 6; ++block) render();  // 50 ms の ramp（2,400 サンプル）を越える
    require (closeTo (buffer.getSample (0, 479), 0.1 * std::pow (10.0, 2.5 / 20.0), 1.0e-6), "C sounds at the new gain after the ramp");
    // 2026-10-04：上限に 0.5 dB 以下だけ届かないなら、上限まで上げて合わせ、足りない量を持つ。
    require (controller.rematch (-12.7, -2.0) == ref::RematchResult::matched
                 && closeTo (controller.snapshot().appliedGainDb, 3.0, 1.0e-6) && closeTo (controller.snapshot().peakShortfallDb, 0.3, 1.0e-6),
             "C matched again 0.3 dB short of the ceiling plays at the ceiling and says how far short");
    require (controller.rematch (-13.5, -2.0) == ref::RematchResult::matched && closeTo (controller.snapshot().peakShortfallDb, 0.0),
             "a full match again clears the shortfall");
    require (controller.rematch (-10.0, -2.0) == ref::RematchResult::ceilingExceeded
                 && controller.rematch (std::numeric_limits<double>::quiet_NaN(), -2.0) == ref::RematchResult::levelUnavailable
                 && closeTo (controller.snapshot().appliedGainDb, 2.5),
             "a MATCH over the ceiling or without an A window keeps the gain and says why");
    const auto refusedAgain = controller.snapshot();
    require (refusedAgain.matchFailure == ref::MatchFailure::ceilingExceeded && refusedAgain.neededAttenuationDb < 0.0,
             "the refused MATCH again offers how far A must be lowered");
    require (controller.rematch (-13.5, -2.0) == ref::RematchResult::matched
                 && controller.snapshot().matchFailure == ref::MatchFailure::none
                 && ! (controller.snapshot().neededAttenuationDb < 0.0)
                 && controller.snapshot().matchFailureSerial != refusedAgain.matchFailureSerial,
             "a later MATCH that fits clears the old failure and its offer");
    controller.selectA();
    render(); render();
    require (controller.rematch (-13.5, -2.0) == ref::RematchResult::notPlaying, "MATCH again needs C to be playing");

    // B：同じ曲を追従で鳴らす。A の直近 10 秒が −20 LUFS になると −4 dB へ 50 ms で動く。
    controller.setTrackingEnabled (true);
    require (controller.selectB (-14.0, -2.0) && controller.snapshot().tracking == ref::TrackingState::following,
             "B follows A");
    render(); render();
    const auto before = buffer.getSample (0, 479);
    require (controller.followSelection (steady (-20.0, 20), -2.0) == ref::TrackingAction::keep,
             "2 s of A after a seek are not enough: the gain is held");
    require (controller.followSelection (steady (-14.2), -2.0) == ref::TrackingAction::keep,
             "within 0.5 dB the gain stays");
    require (controller.followSelection (steady (-20.0), -2.0) == ref::TrackingAction::move, "B follows a quieter A");
    state = controller.snapshot();
    require (closeTo (state.appliedGainDb, -4.0) && closeTo (state.aIntegratedLoudness, -20.0)
                 && closeTo (state.loudnessDeltaBMinusA, 0.0),
             "the snapshot shows the followed gain and A's window");
    std::vector<float> heard;
    for (int index = 0; index < 6; ++index) { render(); for (int n = 0; n < 480; ++n) heard.push_back (buffer.getSample (0, n)); }
    const auto target = static_cast<float> (0.1 * std::pow (10.0, -4.0 / 20.0));
    require (std::abs (heard.front() - before) < 0.0005f && std::abs (heard[1199] - (before + target) / 2.0f) < 0.0005f
                 && std::abs (heard[2399] - target) < 1.0e-6f && std::abs (heard.back() - target) < 1.0e-6f,
             "the gain moves on a 50 ms line, never in a step");
    for (std::size_t n = 1; n < heard.size(); ++n)
        require (heard[n] <= heard[n - 1] + 1.0e-7f, "a falling gain never rises on the way");

    // 上げると上限（max(−1, A の −2, Cue の −4) = −1 dBTP）を超える：動かさず止めて、知らせる状態にする。
    require (controller.followSelection (steady (-8.0), -2.0) == ref::TrackingAction::stopCeiling
                 && controller.snapshot().tracking == ref::TrackingState::stoppedCeiling
                 && closeTo (controller.snapshot().appliedGainDb, -4.0),
             "a gain over the ceiling stops following and keeps the current gain");
    require (controller.followSelection (steady (-18.0), -2.0) == ref::TrackingAction::keep, "a stopped follow stays stopped");
    render();
    require (std::abs (buffer.getSample (0, 0) - target) < 1.0e-6f, "the kept gain keeps sounding");
    // 2026-10-05：上限で止めた後でも、A が静かになって下げる向きなら追従を再開する。
    // （上の B は MATCH の +2 dB から −6 dB の端にいるので、MATCH し直して下げる余地のある所で確かめる。）
    controller.selectA();
    require (controller.selectB (-16.0, -2.0) && closeTo (controller.snapshot().appliedGainDb, 0.0), "B matches again at 0 dB");
    render(); render();
    require (controller.followSelection (steady (-9.0), -2.0) == ref::TrackingAction::stopCeiling
                 && controller.snapshot().tracking == ref::TrackingState::stoppedCeiling,
             "a louder A stops the follow at the ceiling");
    require (controller.followSelection (steady (-16.3), -2.0) == ref::TrackingAction::keep
                 && controller.snapshot().tracking == ref::TrackingState::stoppedCeiling,
             "an A asking for a higher gain keeps the follow stopped at the ceiling");
    require (controller.followSelection (steady (-17.0), -2.0) == ref::TrackingAction::move
                 && controller.snapshot().tracking == ref::TrackingState::following
                 && closeTo (controller.snapshot().appliedGainDb, -1.0),
             "a quieter A resumes the follow downwards after a ceiling stop");
    require (controller.followSelection (steady (-9.0), -2.0) == ref::TrackingAction::stopCeiling
                 && closeTo (controller.snapshot().appliedGainDb, -1.0),
             "the resumed follow stops at the ceiling again");
    controller.selectA();
    require (controller.selectB (-18.0, -2.0) && controller.snapshot().tracking == ref::TrackingState::following,
             "choosing B again follows again");
    // 追従は MATCH の gain（−18 − −16 = −2 dB）から ±6 dB まで。−9 dB は超えるので止めて今の gain を保つ。
    require (controller.followSelection (steady (-25.0), -2.0) == ref::TrackingAction::stopRange
                 && controller.snapshot().tracking == ref::TrackingState::stoppedRange
                 && closeTo (controller.snapshot().appliedGainDb, -2.0),
             "a gain more than 6 dB from the MATCH stops following and keeps the current gain");
    // 下げる向きの要求でも、±6 dB で止めた追従は戻らない（上限で止めたものだけが下げる向きで戻る）。
    require (controller.followSelection (steady (-19.0), -2.0) == ref::TrackingAction::keep
                 && controller.snapshot().tracking == ref::TrackingState::stoppedRange
                 && closeTo (controller.snapshot().appliedGainDb, -2.0),
             "a follow stopped 6 dB from the MATCH stays stopped, even when A asks for a lower gain");
    controller.selectA();
    render(); render();
}
}

void testReferenceCueMatch (const juce::File& sandbox)
{
    readsKirinOsCueLevel (sandbox);
    matchesAndFollows (sandbox);
    std::cout << "Reference Cue MATCH and tracking PASS\n";
}
