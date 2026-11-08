#pragma once
#include "../src/reference_audition/ReferenceTargetService.h"
#include "../src/reference_audition/ReferenceTargetCapture.h"
#include "ReferenceTargetBoundaryTest.h"
#include <clocale>

namespace
{
void testTargetWindowBoundary (const juce::File& sandbox)
{
    namespace target = hypha::reference_audition;
    constexpr int rate = 48000, tick = rate / 10;
    const auto file = sandbox.getChildFile ("window-target.wav");
    juce::AudioBuffer<float> quiet (1, rate * 3);
    for (int n = 0; n < quiet.getNumSamples(); ++n)
        quiet.setSample (0, n, 0.05f * static_cast<float> (std::sin (6.283185307179586 * n / 48.0)));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, 1, 24, {}, 0));
    require (writer && writer->writeFromAudioSampleBuffer (quiet, 0, quiet.getNumSamples()), "quiet window fixture");
    writer.reset();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    require (reader && reader->read (&quiet, 0, quiet.getNumSamples(), 0, true, false), "quantized quiet PCM");
    target::KirinSpectrumMeter spectrum;
    require (spectrum.configure (rate, 1, 40), "continuous A meter");
    target::DynamicsTicks ticks;
    target::TargetPeakWindow peaks;
    std::vector<float> loud (tick);
    for (int n = 0; n < tick; ++n) loud[static_cast<size_t> (n)] = 0.8f * static_cast<float> (std::sin (6.283185307179586 * n / 48.0));
    for (int block = 0; block < 10; ++block)
    { spectrum.push (loud.data(), tick); ticks.push (loud.data(), tick, rate, 1); peaks.push (loud.data(), tick, rate, 1); }
    for (int block = 0; block < 30; ++block)
    {
        const auto* pcm = quiet.getReadPointer (0, block * tick);
        spectrum.push (pcm, tick); ticks.push (pcm, tick, rate, 1);
        peaks.push (pcm, tick, rate, 1);
    }
    target::VisualTimeline timeline;
    timeline.observing = true; timeline.aKirin = spectrum.window (30); timeline.aTicks = ticks.bins();
    timeline.aTickChannels = 1; timeline.aTargetLoudness = peaks.levels (30);
    timeline.aTargetTruePeak = peaks.window (30);
    const auto captured = target::targetFromA (timeline);
    const auto measured = target::measureTargetFile (file, [] { return false; });
    require (captured.valid() && measured.target.valid() && std::abs (captured.duration - 3.0) < 1.0e-9, "the last three seconds create a target");
    require (std::abs (captured.integrated - measured.target.integrated) < 0.01,
             ("a loud preceding second does not enter the quiet window's Integrated: "
              + juce::String (captured.integrated, 9) + "/" + juce::String (measured.target.integrated, 9)).toRawUTF8());
    require (captured.spectrum.p10Db == measured.target.spectrum.p10Db
        && captured.spectrum.medianDb == measured.target.spectrum.medianDb
        && captured.spectrum.p90Db == measured.target.spectrum.p90Db,
        "continuous A and a file use the same last-window spectrum");
    for (size_t i = 0; i < 4; ++i)
        require (std::abs (captured.spectrum.balanceDb[i] - measured.target.spectrum.balanceDb[i]) < 1.0e-10,
                 "continuous A and the file use the same Balance window");
    auto endpoints = std::make_shared<std::vector<KirinReferenceVisualBin>> (*timeline.aTicks);
    const auto start = endpoints->size() - 30;
    for (size_t i = start; i < endpoints->size(); ++i) (*endpoints)[i].momentary_lufs = i < start + 3 ? -4.0 : -40.0;
    timeline.aTicks = endpoints;
    const auto levels = target::targetWindowLevels (timeline);
    require (levels.gatedBlocks == 27 && std::abs (levels.lufs - timeline.aTargetLoudness.lufs) < 1.0e-12,
             "continuous A endpoints cannot overwrite the isolated target-window loudness");
}

// Exercise the real asynchronous RESET entrance at a half-tick, retaining the older range history.
void testTargetResetBoundary()
{
    namespace target = hypha::reference_audition;
    for (const bool queued : { false, true })
    {
        AWindowRing ring;
        ring.enqueue (210, 0.8f); // One second and a half-tick, with a peak outside the new window.
        if (! queued) ring.drain();
        ring.observation.resetAWindow();
        // Even an empty queue must cross the worker's next RESET check before publication.
        const auto reset = ring.drain();
        require (! reset->aKirin && reset->aTicks && reset->aTicks->size() == 10,
                 "RESET preserves the old range bins while clearing the displayed spectrum");
        target::KirinSpectrumMeter spectrum;
        target::DynamicsTicks ticks;
        target::TargetPeakWindow peaks;
        require (spectrum.configure (48000, 2, 30), "reset-window oracle configured");
        std::array<float, 480> pcm {};
        const auto feed = [&] (int blocks, float amplitude) {
            for (int b = 0; b < blocks; ++b)
            {
                for (int n = 0; n < 240; ++n)
                    pcm[static_cast<size_t> (n * 2)] = pcm[static_cast<size_t> (n * 2 + 1)]
                        = amplitude * static_cast<float> (std::sin (6.283185307179586 * (n % 48) / 48.0));
                spectrum.push (pcm.data(), 240); ticks.push (pcm.data(), 240, 48000, 2);
                peaks.push (pcm.data(), 240, 48000, 2);
            }
            ring.enqueue (blocks, amplitude); ring.drain();
        };
        feed (10, 0.4f); // A burst immediately after RESET must survive the old half-tick boundary.
        feed (200, 0.05f); feed (200, 0.05f); feed (180, 0.05f);
        require (! target::targetFromA (*ring.observation.snapshot()).valid(), "MAKE waits for three seconds after RESET");
        feed (10, 0.2f); // Exactly three seconds, including the final half-tick.
        target::VisualTimeline expected;
        expected.observing = true; expected.aKirin = spectrum.window (30); expected.aTicks = ticks.bins();
        expected.aTickChannels = 2; expected.aTargetLoudness = peaks.levels (30);
        expected.aTargetTruePeak = peaks.window (30);
        const auto oracle = target::targetFromA (expected);
        const auto observed = ring.observation.snapshot();
        const auto captured = target::targetFromA (*observed);
        require (captured.valid() && std::abs (captured.duration - 3.0) < 1.0e-9 && observed->aTicks->size() == 40,
                 "a target uses exactly the thirty complete post-RESET ticks, without dropping range history");
        require (captured.spectrum.medianDb == oracle.spectrum.medianDb
            && captured.spectrum.p10Db == oracle.spectrum.p10Db && captured.spectrum.p90Db == oracle.spectrum.p90Db,
                 "RESET preserves the immediate burst in Spectrum and the target percentile bands");
        for (size_t band = 0; band < 4; ++band)
            require (std::abs (captured.spectrum.balanceDb[band] - oracle.spectrum.balanceDb[band]) < 1.0e-10,
                     "RESET preserves the immediate burst in Balance");
        require (std::abs (captured.integrated - oracle.integrated) < 0.01
            && std::abs (captured.maximumTruePeak - oracle.maximumTruePeak) < 0.01,
                 "a half-tick RESET excludes the preceding peak and includes the final peak");
    }
}

void testTargetFormatBoundary()
{
    namespace target = hypha::reference_audition;
    AWindowRing ring;
    ring.enqueue (10, 0.8f); ring.drain(); // Leave a partial tick at the previous rate.
    const auto generation = ring.observation.inputGeneration();
    ring.observation.configure (96000, 2); ring.drain();
    require (ring.observation.inputGeneration() != generation, "the real format entrance changes the input generation");
    target::KirinSpectrumMeter spectrum;
    target::DynamicsTicks ticks;
    target::TargetPeakWindow peaks;
    require (spectrum.configure (96000, 2, 30), "new-format oracle configured");
    std::array<float, 480> pcm {};
    for (int batch = 0; batch < 6; ++batch)
    {
        for (int b = 0; b < 200; ++b)
        {
            for (int n = 0; n < 240; ++n)
                pcm[static_cast<size_t> (n * 2)] = pcm[static_cast<size_t> (n * 2 + 1)]
                    = 0.05f * static_cast<float> (std::sin (6.283185307179586 * (n % 48) / 48.0));
            spectrum.push (pcm.data(), 240); ticks.push (pcm.data(), 240, 96000, 2);
            peaks.push (pcm.data(), 240, 96000, 2);
        }
        ring.enqueue (200, 0.05f); ring.drain();
    }
    target::VisualTimeline expected;
    expected.observing = true; expected.aKirin = spectrum.window (30); expected.aTicks = ticks.bins();
    expected.aTickChannels = 2; expected.aTargetLoudness = peaks.levels (30);
    expected.aTargetTruePeak = peaks.window (30);
    const auto oracle = target::targetFromA (expected);
    const auto captured = target::targetFromA (*ring.observation.snapshot());
    require (captured.valid() && std::abs (captured.duration - 3.0) < 1.0e-9
        && captured.spectrum.medianDb == oracle.spectrum.medianDb
        && std::abs (captured.integrated - oracle.integrated) < 0.01
        && std::abs (captured.maximumTruePeak - oracle.maximumTruePeak) < 0.01,
             "a format change aligns both summaries in the new generation without the old partial tick");
}

void testTargetPublicationCrossing (const juce::File& file, const hypha::reference_audition::TargetCollection& restored)
{
    namespace target = hypha::reference_audition;
    target::TargetService service;
    for (const bool restore : { false, true })
    {
        service.restore (restored);
        juce::WaitableEvent reached, release;
        service.setBeforePublishForTest ([&] { reached.signal(); release.wait (-1); });
        require (service.importFiles ({ file.getFullPathName() }) && reached.wait (30000),
                 "the real worker holds completed measurements immediately before publication");
        if (restore) service.restore (restored); else service.cancel();
        release.signal();
        require (service.waitUntilIdle (30000), "the cancelled worker has physically finished publishing");
        service.setBeforePublishForTest ({});
        const auto result = service.snapshot();
        require (! result.measuring && result.collection.items.size() == restored.items.size()
            && result.collection.selected == restored.selected && result.issue.isEmpty(),
            "completed stale results cannot enter the project after cancel or restore");
    }
}

void testWholeTargetFile (const juce::File& sandbox)
{
    namespace target = hypha::reference_audition;
    constexpr int rate = 40000, seconds = 601;
    const auto file = sandbox.getChildFile ("whole-target.flac");
    juce::FlacAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, 1, 24, {}, 0));
    require (writer != nullptr, "FLAC writer available");
    juce::AudioBuffer<float> second (1, rate);
    std::array<double, 4> loud {}, quiet {};
    target::KirinSpectrumMeter oracle;
    require (oracle.configure (rate, 1, 1), "whole-file oracle");
    for (int s = 0; s < seconds; ++s)
    {
        const float amplitude = s == 0 ? 0.5f : 0.005f;
        for (int n = 0; n < rate; ++n) second.setSample (0, n, amplitude * static_cast<float> (std::sin (6.283185307179586 * n / 40.0)));
        require (writer->writeFromAudioSampleBuffer (second, 0, rate), "stream long FLAC fixture");
        if (s < 2)
        {
            oracle.push (second.getReadPointer (0), rate);
            std::array<float, 64> bands {};
            require (oracle.lastFrame (bands, s == 0 ? loud : quiet), "reference frame power");
        }
    }
    writer.reset();
    const auto existing = sandbox.findChildFiles (juce::File::findFilesAndDirectories, true);
    const auto measured = target::measureTargetFile (file, [] { return false; });
    require (sandbox.findChildFiles (juce::File::findFilesAndDirectories, true) == existing,
             "whole-file measurement creates no audio, cache, or sidecar files");
    require (measured.target.valid() && std::abs (measured.target.duration - seconds) < 1.0e-9, "a file beyond ten minutes is measured whole");
    const auto expected = 10.0 * std::log10 ((std::pow (10.0, loud[1] / 10.0)
        + 600.0 * std::pow (10.0, quiet[1] / 10.0)) / 601.0);
    require (std::abs (measured.target.spectrum.balanceDb[1] - expected) < 0.01, "whole-file Balance retains the first second beyond the A ring limit");
    target::TargetService service;
    require (service.importFiles ({ file.getFullPathName() }), "long import starts on the actual worker");
    require (hypha::tests::reference_control::waitUntil (std::chrono::steady_clock::now() + std::chrono::seconds (30), [&] {
        const auto state = service.snapshot(); return state.measuring && state.progress > 0.001;
    }), "the worker has actually decoded part of the file");
    service.cancel();
    require (service.waitUntilIdle (30000) && ! service.snapshot().measuring && service.snapshot().collection.items.empty(), "in-progress cancellation keeps PCM and partial facts out of the project");
}

void testMeasuredTargets (const juce::File& sandbox)
{
    namespace target = hypha::reference_audition;
    testTargetLoudnessComposition();
    testTargetLoudnessBoundaries (sandbox);
    testTargetWindowBoundary (sandbox);
    testTargetPeakBoundaries (sandbox);
    testTargetPeakRecovery();
    testMakeTargetRestoreCrossing (sandbox);
    testMakeTargetAfterReset (sandbox);
    testTargetResetBoundary();
    testTargetFormatBoundary();
    testWholeTargetFile (sandbox);
    const auto file = sandbox.getChildFile ("target-source.wav");
    constexpr int rate = 48000, frames = rate * 4;
    juce::AudioBuffer<float> audio (2, frames);
    for (int n = 0; n < frames; ++n)
    {
        const auto gain = n < frames / 2 ? 0.03f : 0.3f;
        audio.setSample (0, n, gain * std::sin (float (n) * 0.13f));
        audio.setSample (1, n, gain * std::sin (float (n) * 0.21f));
    }
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, 2, 24, {}, 0));
    require (writer && writer->writeFromAudioSampleBuffer (audio, 0, frames), "target fixture writes");
    writer.reset();
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    require (reader && reader->read (&audio, 0, frames, 0, true, true), "target fixture reads actual quantized PCM");
    target::KirinSpectrumMeter a;
    require (a.configure (rate, 2, 40), "A spectrum configured");
    target::DynamicsTicks ticks;
    target::TargetPeakWindow peaks;
    std::vector<float> interleaved (9600);
    for (int at = 0; at < frames; at += 4800)
    {
        for (int n = 0; n < 4800; ++n) for (int c = 0; c < 2; ++c)
            interleaved[static_cast<size_t> (n * 2 + c)] = audio.getSample (c, at + n);
        a.push (interleaved.data(), 4800);
        ticks.push (interleaved.data(), 4800, rate, 2);
        peaks.push (interleaved.data(), 4800, rate, 2);
    }
    const auto measured = target::measureTargetFile (file, [] { return false; });
    require (measured.issue.isEmpty() && measured.target.valid(), "file target is measured");
    target::VisualTimeline timeline;
    timeline.observing = true; timeline.aKirin = a.window (40); timeline.aTicks = ticks.bins();
    timeline.aTickChannels = 2; timeline.aTargetLoudness = peaks.levels (40);
    timeline.aTargetTruePeak = peaks.window (40);
    const auto captured = target::targetFromA (timeline);
    require (captured.valid(), "A's displayed window creates a target without reanalysis");
    const auto& expected = *timeline.aKirin;
    for (size_t i = 0; i < 64; ++i)
        require (std::abs (measured.target.spectrum.p10Db[i] - expected.p10Db[i]) < 1.0e-7
            && std::abs (measured.target.spectrum.medianDb[i] - expected.medianDb[i]) < 1.0e-7
            && std::abs (measured.target.spectrum.p90Db[i] - expected.p90Db[i]) < 1.0e-7
            && std::abs (captured.spectrum.medianDb[i] - expected.medianDb[i]) < 1.0e-7, "file and A use identical spectrum ranks");
    for (size_t i = 0; i < 4; ++i)
        require (std::abs (measured.target.spectrum.balanceDb[i] - expected.balanceDb[i]) < 1.0e-10, "file and A use identical Balance power");
    require (std::abs (captured.integrated - measured.target.integrated) < 0.01
        && std::abs (captured.maximumTruePeak - measured.target.maximumTruePeak) < 0.01,
        ("file and A window loudness and TP agree: I " + juce::String (captured.integrated, 6) + "/"
        + juce::String (measured.target.integrated, 6) + " TP " + juce::String (captured.maximumTruePeak, 6) + "/"
        + juce::String (measured.target.maximumTruePeak, 6)).toRawUTF8());
    timeline.aKirin = a.window (29);
    require (! target::targetFromA (timeline).valid(), "A shorter than 3 seconds is unavailable");
    const auto aiffFile = sandbox.getChildFile ("target-source.aiff");
    juce::AiffAudioFormat aiff;
    std::unique_ptr<juce::AudioFormatWriter> aiffWriter (aiff.createWriterFor (aiffFile.createOutputStream().release(), rate, 2, 24, {}, 0));
    require (aiffWriter && aiffWriter->writeFromAudioSampleBuffer (audio, 0, frames), "AIFF target fixture writes");
    aiffWriter.reset();
    const auto aiffTarget = target::measureTargetFile (aiffFile, [] { return false; });
    require (aiffTarget.target.valid() && aiffTarget.target.spectrum.medianDb == measured.target.spectrum.medianDb,
             "AIFF and WAV targets use the same decoded samples and meter");
    target::TargetCollection collection;
    collection.items = { measured.target }; collection.selected = measured.target.id;
    juce::XmlElement state ("KirinHyphaState"); collection.write (state);
    const auto saved = state.toString();
    require (! saved.contains (file.getFullPathName()) && ! saved.contains ("pcm") && ! saved.contains ("audio")
        && saved.length() < 10000, "project state contains only compact numerical facts");
    const juce::String originalLocale (std::setlocale (LC_NUMERIC, nullptr));
    for (const auto* locale : { "de_DE.UTF-8", "de_DE", "German_Germany.1252" })
        if (std::setlocale (LC_NUMERIC, locale) != nullptr) break;
    const auto round = target::TargetCollection::read (state);
    std::setlocale (LC_NUMERIC, originalLocale.toRawUTF8());
    require (round.items.size() == 1 && round.selected == collection.selected
        && round.items[0].spectrum.p10Db == measured.target.spectrum.p10Db, "facts and C selection round trip exactly");
    testTargetPublicationCrossing (file, round);
    juce::XmlElement old ("KirinHyphaState");
    require (target::TargetCollection::read (old).items.empty(), "older project state has no local targets");
    auto* item = state.getChildByName ("MeasuredTargets")->getChildByName ("Target");
    item->setAttribute ("median", "broken");
    require (target::TargetCollection::read (state).items.empty(), "invalid numeric state is ignored without corrupting other state");
    bool dataOnly = false;
    target::TargetService service ({}, [&] (bool selected) { dataOnly = selected; });
    for (int i = 0; i < 8; ++i)
    {
        auto copy = measured.target; copy.id = "target-" + juce::String (i);
        require (service.add (copy), "eight targets fit");
    }
    require (! service.add (captured) && ! service.importFiles ({ file.getFullPathName() })
        && service.snapshot().collection.items.size() == 8, "all entrances enforce eight targets");
    for (const int occupied : { 0, 6 })
    {
        target::TargetCollection prior;
        prior.items.assign (round.items.begin(), round.items.end());
        for (int i = 1; i < occupied; ++i)
        { auto copy = measured.target; copy.id = "prior-" + juce::String (i); prior.items.push_back (copy); }
        if (occupied == 0) prior.items.clear();
        service.restore (prior);
        juce::StringArray files;
        for (int i = 0; i < 10; ++i) files.add (file.getFullPathName());
        require (service.importFiles (files) && service.waitUntilIdle (30000), "an oversized drop starts and settles");
        const auto filled = service.snapshot();
        require (filled.collection.items.size() == 8 && filled.issue == "8 TARGETS MAX",
                 "an oversized drop fills every empty slot and reports only the excess");
        if (occupied > 0) require (filled.collection.items.front().id == prior.items.front().id, "existing targets survive an oversized drop");
    }
    service.restore ({});
    require (service.importFiles ({ file.getFullPathName() }) && service.waitUntilIdle (30000)
        && service.snapshot().collection.items.size() == 1, "real asynchronous file import completes");
    const auto unreadable = sandbox.getChildFile ("invalid-target.wav");
    require (hypha::storage::writeText (unreadable, "not audio"), "unreadable fixture");
    require (service.importFiles ({ unreadable.getFullPathName() }) && service.waitUntilIdle (30000)
        && service.snapshot().collection.items.size() == 1 && service.snapshot().issue == "FILE UNREADABLE",
        "a bad import keeps existing targets and reports a short label");
    require (service.select (service.snapshot().collection.items.front().id) && service.snapshot().issue.isEmpty(),
             "the next target choice dismisses an import failure");
    double now = 1000.0;
    target::TargetService timed ({}, {}, [&] { return now; });
    timed.report ("FILE UNREADABLE");
    require (timed.snapshot().issue == "FILE UNREADABLE", "an explicit failure is initially visible");
    now += 4999.0;
    require (timed.snapshot().issue.isNotEmpty(), "the failure remains readable before its deadline");
    now += 1.0;
    require (timed.snapshot().issue.isEmpty(), "the failure expires without a sleep or another user action");
    bool cancel = false;
    const auto cancelled = target::measureTargetFile (file, [&] { return cancel; }, [&] (double progress) { cancel = progress >= 0.25; });
    require (cancel && ! cancelled.target.valid() && cancelled.issue.isEmpty(), "cancellation during decoding publishes no target");
    require (service.importFiles ({ file.getFullPathName() }), "second async import starts");
    service.cancel();
    require (! service.snapshot().measuring && service.snapshot().collection.items.size() == 1, "cancel keeps prior project facts");
    require (service.importFiles ({ file.getFullPathName() }), "restore-overlap import starts");
    service.restore (round);
    require (service.waitUntilIdle (30000) && service.snapshot().collection.selected == round.selected && ! service.snapshot().measuring,
             "state restore cancels stale work and restores the saved target");
    require (dataOnly && service.select ({}) && ! dataOnly && service.select (round.selected) && dataOnly
        && service.remove (round.selected) && ! dataOnly, "restore, select and remove publish the canonical data-only control flag");
    service.restore (round);
    require (dataOnly, "restoring a numeric target republishes its no-audio selection");
}
}
