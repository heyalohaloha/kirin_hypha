#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "reference_rt_probe.h"

inline void testReferenceVersionSwitch (const juce::File& sandbox, bool edited = false)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile (edited ? "version-switch-edited" : "version-switch");
    require (root.createDirectory().wasOk(), "version switch fixture directory");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    juce::Array<juce::var> entries;
    juce::StringArray ids;
    juce::var manifest;
    for (int n = 1; n <= 3; ++n)
    {
        const int rate = n == 3 ? 192000 : 48000;
        const bool shifted = n == 2 || (edited && n == 3);
        manifest = hypha::tests::version_formats::publish (root, base.audio, rate, n == 2,
                                                         shifted ? rate * 137 / 1000 : 0, n,
                                                         edited && n == 3 ? 0.06f : 0.0f);
        const auto entry = manifest["versions"][0];
        const auto id = entry["entry_id"].toString();
        entries.add (entry); ids.add (id + "/" + id + "/" + id);
    }
    manifest.getDynamicObject()->setProperty ("versions", entries);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish all format versions together");
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"switch-post", {}, 42, true}, 48000, 2);
    control::service (c); require (c.selectVersion (ids[0]), "select initial Version");
    c.setPresented (true); control::service (c);
    juce::AudioBuffer<float> input (2, 256);
    int cursor = 0;
    const auto feed = [&] (bool render)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor, 256);
        beginReferenceRtProbe();
        c.observeTransport (cursor, true, true); c.observeAInput (input, cursor, true, true, true);
        const bool sounded = render && c.renderSelectedB (input, cursor, true, true, true);
        require (endReferenceRtProbe() == 0, "version switch callback allocates nothing");
        if (sounded && !edited)
        {
            double error = 0.0, energy = 0.0;
            for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 256; ++frame)
            {
                const double a = base.audio.getSample (ch, cursor + frame);
                const double delta = input.getSample (ch, frame) - a;
                energy += a * a; error += delta * delta;
            }
            require (energy > 0.0 && error < energy * 0.25,
                     "decoded and converted V plays the current passage, not unrelated future PCM");
        }
        cursor += 256; control::service (c); c.servicePendingAudition (-22, -6, true);
        return sounded;
    };
    const auto verifyOutput = [&] (int number)
    {
        const int ratio = number == 3 ? 4 : 1;
        const bool shifted = number == 2 || (edited && number == 3);
        const auto file = root.getChildFile ("version-" + juce::String (number) + (number == 2 ? ".flac" : ".wav"));
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        std::vector<float> expected;
        require (reader && ref::readReferenceProbe (*reader,
            static_cast<std::int64_t> (cursor - 256 + (shifted ? 6576 : 0)) * ratio,
            256, 48000, 2, expected), "read the independently known current passage of the chosen version");
        const auto gain = std::pow (10.0, c.snapshot().versionSelection->appliedGainDb / 20.0);
        for (int frame = 0; frame < 256; ++frame) for (int ch = 0; ch < 2; ++ch)
            require (std::abs (input.getSample (ch, frame) - expected[static_cast<size_t> (frame * 2 + ch)] * gain) < 1.0e-4,
                     "the chosen format actually sounds at its known aligned position and gain");
    };
    while (cursor < 192000) feed (false);
    require (c.snapshot().versionReady && c.requestAudition (1, -22, -6), "initial V is matched and explicitly started");
    require (feed (true), "positive control: initial V is audible");
    // Change again while the previous choice is still fading/preparing. The last
    // explicit choice must own the pending start, never an older publication.
    require (c.selectVersion (ids[1]), "select a shifted FLAC while V plays");
    require (c.selectVersion (ids[2]), "select a 192 kHz version before FLAC preparation finishes");
    for (int n = 0; n < 4; ++n) feed (true);
    if (!c.snapshot().versionReady)
        std::cout << "192 kHz pending reason: " << c.snapshot().versionSelection->rejectionCode << '\n';
    require (c.snapshot().selectedVersionId == ids[2] && c.snapshot().versionReady
        && c.snapshot().versionSelection->sourceSampleRateHz == 192000,
             "the latest 192 kHz choice aligns without another click");
    require (feed (true), "the latest continuous-switch choice resumes automatically");
    verifyOutput (3);
    require (c.selectVersion (ids[1]), "return to the shifted FLAC during playback");
    for (int n = 0; n < 4; ++n) feed (true);
    require (c.snapshot().selectedVersionId == ids[1] && c.snapshot().versionReady && feed (true),
             "FLAC with a known leading silence aligns and resumes after repeated switches");
    verifyOutput (2);
    const auto alignment = control::calibrationFor (control::roleFor (c, {1}), {});
    require (alignment.bStartSample - alignment.aStartSample == 6576,
             "the independent 137 ms fixture construction determines the expected FLAC position");
    c.selectA();
}
