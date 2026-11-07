#pragma once
#include "../src/reference_audition/ReferenceTargetCapture.h"
#include "../src/reference_audition/ReferenceTargetPeakWindow.h"

namespace
{
void testTargetLoudnessComposition()
{
    for (const int rate : { 40000, 44100, 48000, 96000, 192000 })
        for (const int channels : { 1, 2 })
        {
            const int tick = rate / 10;
            ref::TargetLoudnessWindow window;
            std::vector<float> pcm (static_cast<size_t> (tick * channels));
            const auto fill = [&] (int block) {
                const auto gain = block < 30 ? 0.8 : block < 40 ? 0.002 : 0.07;
                for (int n = 0; n < tick; ++n)
                    for (int c = 0; c < channels; ++c)
                    {
                        const auto t = double (block * tick + n) / rate;
                        pcm[static_cast<size_t> (n * channels + c)] = static_cast<float> (gain * (c == 0 ? 1.0 : -0.6)
                            * (0.4 * std::sin (6.283185307179586 * 37.0 * t) + 0.6 * std::cos (6.283185307179586 * 1500.0 * t)));
                    }
            };
            for (int block = 0; block < 60; ++block) { fill (block); window.push (pcm.data(), rate, channels); }
            for (const int count : { 30, 45, 60 })
            {
                auto* oracle = kirin_reference_visual_capture_create (static_cast<std::uint32_t> (rate), static_cast<std::uint32_t> (channels));
                require (oracle != nullptr, "isolated loudness oracle");
                for (int block = 60 - count; block < 60; ++block)
                {
                    fill (block);
                    for (size_t at = 0; at < pcm.size();)
                    {
                        const auto size = std::min (pcm.size() - at, static_cast<size_t> (4096 * channels));
                        require (kirin_reference_visual_push (oracle, pcm.data() + at, size), "isolated oracle PCM");
                        at += size;
                    }
                }
                double integrated = 0, peak = 0;
                require (kirin_reference_visual_seal (oracle, &integrated, &peak), "isolated oracle seal");
                kirin_reference_visual_drop (oracle);
                require (std::abs (window.window (count).lufs - integrated) < 0.0001,
                         "numeric filter-state composition matches a fresh full-file meter across rates, channels, gates and window lengths");
            }
            window.reset();
            require (window.window (30).blocks == 0, "reset cannot expose previous target-window facts");
        }
}

void testTargetLoudnessBoundaries (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    constexpr int rate = 48000, tick = rate / 10;
    for (const int channels : { 1, 2 })
        for (const float amplitude : { 0.0f, 0.001f, 0.05f })
        {
            const auto label = "isolated-" + juce::String (channels) + "-" + juce::String (amplitude, 4);
            const auto file = sandbox.getChildFile (label + ".wav");
            juce::AudioBuffer<float> quiet (channels, rate * 3);
            for (int n = 0; n < quiet.getNumSamples(); ++n)
                for (int c = 0; c < channels; ++c)
                    quiet.setSample (c, n, amplitude * static_cast<float> (std::sin (6.283185307179586 * n / 48.0)));
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, static_cast<unsigned int> (channels), 24, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (quiet, 0, quiet.getNumSamples()), "isolated loudness fixture");
            writer.reset();
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            require (reader && reader->read (&quiet, 0, quiet.getNumSamples(), 0, true, channels == 2), "quantized window PCM");
            ref::ReferenceComparisonController controller (sandbox.getChildFile (label));
            controller.configure ({ label, {}, 42, true }, rate, channels);
            controller.setPresented (true);
            control::await ([&] { const auto s = controller.snapshot(); return s.visualTimeline && s.visualTimeline->observing; });
            controller.selectVisualSlot (2);
            require (controller.selectAWindow (controller.snapshot().aWindowTarget, ref::AWindowChoice::seconds3), "real C three-second selection");
            juce::AudioBuffer<float> block (channels, tick);
            for (int b = 0; b < 40; ++b)
            {
                for (int n = 0; n < tick; ++n)
                    for (int c = 0; c < channels; ++c)
                        block.setSample (c, n, b < 10
                            ? 0.8f * static_cast<float> (std::sin (6.283185307179586 * (b * tick + n) * 50.0 / rate))
                            : quiet.getSample (c, (b - 10) * tick + n));
                controller.observeTransport (b * tick, true, true);
                controller.observeAInput (block, b * tick, true, true, true);
                control::await ([&] {
                    const auto s = controller.snapshot();
                    return s.visualTimeline && s.visualTimeline->aTicks && s.visualTimeline->aTicks->size() == static_cast<size_t> (b + 1);
                }, "the real observer publishes each completed tick");
            }
            require (controller.makeTarget(), "the real MAKE entrance accepts the isolated three-second window");
            const auto saved = controller.savedSettings();
            const auto& captured = saved.targets.items.front();
            const auto measured = ref::measureTargetFile (file, [] { return false; });
            const auto agrees = [] (double a, double b) { return (std::isnan (a) && std::isnan (b)) || std::abs (a - b) < 0.0001; };
            require (measured.target.valid() && captured.channels == channels
                && agrees (captured.integrated, measured.target.integrated),
                ("large 50 Hz before silence/quiet stays outside MAKE Integrated: " + juce::String (captured.integrated, 9)
                    + "/" + juce::String (measured.target.integrated, 9)).toRawUTF8());
            require (captured.spectrum.medianDb == measured.target.spectrum.medianDb, "MAKE keeps A's original spectrum");
            controller.restoreSettings (saved);
            require (agrees (controller.savedSettings().targets.items.front().integrated, measured.target.integrated),
                     "controller restore preserves the isolated measurements");
        }
}

void testTargetPeakBoundaries (const juce::File& sandbox)
{
    for (const int rate : { 48000, 96000, 192000 })
        for (const int pattern : { 0, 1, 2, 3 })
        {
            const auto file = sandbox.getChildFile ("peak-window-" + juce::String (rate) + "-" + juce::String (pattern) + ".wav");
            juce::AudioBuffer<float> audio (2, rate * 3);
            audio.clear();
            // Impulses immediately before the window, at its tail, across an internal tick,
            // and immediately after it distinguish isolated edges from continuous FIR support.
            const auto sample = [=] (std::int64_t at, int channel) {
                const auto first = static_cast<std::int64_t> (rate);
                const auto last = first + rate * 3;
                const bool impulse = pattern == 0 ? at == first - 1
                    : pattern == 1 ? at == last - 2 || at == last - 1
                    : pattern == 2 ? at == first + rate / 10 - 1 || at == first + rate / 10
                    : at == last;
                return impulse ? (channel == 0 ? 0.875f : -0.5f) : 0.0f;
            };
            for (int n = 0; n < audio.getNumSamples(); ++n)
                for (int c = 0; c < 2; ++c) audio.setSample (c, n, sample (rate + n, c));
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (file.createOutputStream().release(), rate, 2, 24, {}, 0));
            require (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "TP boundary file");
            writer.reset();
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            require (reader && reader->read (&audio, 0, audio.getNumSamples(), 0, true, true), "the A window uses the file's actual quantized PCM");
            const auto quantized = [&] (std::int64_t at, int channel) {
                return at >= rate && at < rate * 4 ? audio.getSample (channel, static_cast<int> (at - rate)) : sample (at, channel);
            };
            AWindowRing ring;
            ring.observation.configure (rate, 2); ring.drain();
            for (int remaining = rate * 4 / 240; remaining > 0;)
            {
                const auto count = std::min (remaining, 200);
                ring.enqueueSamples (count, quantized); ring.drain(); remaining -= count;
            }
            // Include a partial next tick: it must not enter the last completed window.
            ring.enqueueSamples (1, quantized); ring.drain();
            const auto timeline = ring.window (30);
            const auto captured = ref::targetFromA (*timeline);
            const auto measured = ref::measureTargetFile (file, [] { return false; });
            require (captured.valid() && measured.target.valid()
                && std::abs (captured.maximumTruePeak - measured.target.maximumTruePeak) < 1.0e-9,
                ("window/file TP agrees at both FIR edges and internal joins: rate " + juce::String (rate)
                    + " pattern " + juce::String (pattern) + " / " + juce::String (captured.maximumTruePeak, 9)
                    + " / " + juce::String (measured.target.maximumTruePeak, 9)).toRawUTF8());
        }
}

struct TargetPeakMeterTag {};
KirinReferenceVisualMeter* isolatedPeakMeter (ref::TargetPeakWindow&, TargetPeakMeterTag);
template <auto Member> struct TargetPeakMeterAccess
{ friend KirinReferenceVisualMeter* isolatedPeakMeter (ref::TargetPeakWindow& window, TargetPeakMeterTag) { return (window.*Member).get(); } };
template struct TargetPeakMeterAccess<&ref::TargetPeakWindow::isolated>;

void testTargetPeakRecovery()
{
    constexpr int rate = 48000, tick = rate / 10;
    ref::TargetPeakWindow window;
    std::vector<float> pcm (tick * 5, 0.0f);
    pcm[tick * 2 - 1] = 0.99f; // Outside the recovered window.
    pcm[pcm.size() - 1] = pcm[pcm.size() - 2] = 0.875f;
    window.push (pcm.data(), tick, rate, 1);
    double loudness = 0, peak = 0;
    require (kirin_reference_visual_seal (isolatedPeakMeter (window, {}), &loudness, &peak), "seal a real meter to force its next push to fail");
    for (int at = tick; at < static_cast<int> (pcm.size()); at += 256)
        window.push (pcm.data() + at, std::min (256, static_cast<int> (pcm.size()) - at), rate, 1);
    auto* oracle = kirin_reference_visual_capture_create (rate, 1);
    for (int at = tick * 2; at < static_cast<int> (pcm.size()); at += 256)
        require (kirin_reference_visual_push (oracle, pcm.data() + at,
            static_cast<size_t> (std::min (256, static_cast<int> (pcm.size()) - at))), "recovered-window oracle input");
    require (kirin_reference_visual_seal (oracle, &loudness, &peak), "recovered-window oracle tail");
    kirin_reference_visual_drop (oracle);
    require (std::isfinite (window.window (3)) && std::abs (window.window (3) - 20.0 * std::log10 (peak)) < 1.0e-5,
             "a failed tick discards old facts and resumes on the original boundary, including the rest of its callback");
}

void testMakeTargetRestoreCrossing (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    ref::ReferenceComparisonController controller (sandbox.getChildFile ("target-restore"));
    controller.configure ({ "target-restore", {}, 42, true }, 48000, 2);
    controller.setPresented (true);
    control::await ([&] { const auto state = controller.snapshot(); return state.visualTimeline && state.visualTimeline->observing; });
    juce::AudioBuffer<float> block (2, 4800);
    block.clear();
    for (int tick = 0; tick < 30; ++tick)
    {
        controller.observeTransport (tick * 4800, true, true);
        controller.observeAInput (block, tick * 4800, true, true, true);
        control::await ([&] {
            const auto state = controller.snapshot();
            return state.visualTimeline && state.visualTimeline->aKirin && state.visualTimeline->aKirin->frames == tick + 1;
        }, "the real MAKE entrance has a complete A window");
    }
    require (controller.makeTarget(), "MAKE creates a target before the restore race");
    auto restored = controller.savedSettings();
    restored.viewedSlot = 3;
    for (int round = 0; round < 2; ++round)
    {
        juce::WaitableEvent reached, release;
        controller.setBeforeMakePublishForTest ([&] { reached.signal(); release.wait (-1); });
        bool made = true;
        std::thread make ([&] { made = controller.makeTarget(); });
        require (reached.wait (30000), "MAKE pauses after the real A snapshot and before commit");
        controller.restoreSettings (restored);
        release.signal(); make.join();
        controller.setBeforeMakePublishForTest ({});
        const auto facts = controller.targetService().snapshot();
        require (! made && facts.collection.items.size() == restored.targets.items.size()
            && facts.collection.selected == restored.targets.selected && facts.issue.isEmpty()
            && controller.savedSettings().viewedSlot == 3,
            "a stale MAKE cannot add, select, change page or overwrite the restored state");
    }
    require (controller.makeTarget() && controller.savedSettings().viewedSlot == 2,
             "the first current MAKE works after the crossed request is discarded");
}

void testMakeTargetAfterReset (const juce::File& sandbox)
{
    namespace control = hypha::tests::reference_control;
    for (const int channels : { 1, 2 })
    {
        ref::ReferenceComparisonController controller (sandbox.getChildFile ("reset-make-" + juce::String (channels)));
        controller.configure ({ "reset-make", {}, 42, true }, 48000, channels);
        controller.setPresented (true); controller.selectVisualSlot (2);
        control::await ([&] { const auto s = controller.snapshot(); return s.visualTimeline && s.visualTimeline->observing; });
        require (controller.selectAWindow (controller.snapshot().aWindowTarget, ref::AWindowChoice::seconds3), "RESET/MAKE three-second window");
        juce::AudioBuffer<float> old (channels, 7200); // End halfway through the continuous range meter's tick.
        for (int c = 0; c < channels; ++c)
            for (int n = 0; n < old.getNumSamples(); ++n) old.setSample (c, n, 0.8f);
        controller.observeTransport (0, true, true);
        controller.observeAInput (old, 0, true, true, true);
        control::await ([&] { const auto s = controller.snapshot(); return s.visualTimeline->aKirin && s.visualTimeline->aKirin->frames == 1; });
        require (controller.resetAWindow (controller.snapshot().aWindowTarget), "real RESET entrance");
        control::await ([&] { return ! controller.snapshot().visualTimeline->aKirin; });
        ref::KirinSpectrumMeter spectrum;
        ref::TargetPeakWindow peaks;
        require (spectrum.configure (48000, channels, 30), "post-RESET spectrum oracle");
        juce::AudioBuffer<float> block (channels, 4800);
        std::vector<float> pcm (static_cast<size_t> (4800 * channels));
        for (int tick = 0; tick < 30; ++tick)
        {
            for (int n = 0; n < 4800; ++n)
                for (int c = 0; c < channels; ++c)
                {
                    const float gain = tick == 0 && n < 2400 ? 0.4f : 0.05f;
                    const auto sample = gain * static_cast<float> (std::sin (6.283185307179586 * n / 48.0));
                    block.setSample (c, n, sample); pcm[static_cast<size_t> (n * channels + c)] = sample;
                }
            spectrum.push (pcm.data(), 4800); peaks.push (pcm.data(), 4800, 48000, channels);
            const auto position = 7200 + tick * 4800;
            controller.observeTransport (position, true, true);
            controller.observeAInput (block, position, true, true, true);
            control::await ([&] { const auto s = controller.snapshot(); return s.visualTimeline->aKirin && s.visualTimeline->aKirin->frames == tick + 1; });
            if (tick == 28) require (! controller.makeTarget(), "real MAKE rejects 2.9 seconds after RESET");
        }
        require (controller.makeTarget(), "real MAKE accepts exactly three seconds after a half-tick RESET");
        const auto captured = controller.savedSettings().targets.items.front();
        const auto expected = spectrum.window (30);
        require (captured.duration == 3.0 && captured.spectrum.p10Db == expected->p10Db
            && captured.spectrum.medianDb == expected->medianDb && captured.spectrum.p90Db == expected->p90Db,
            "real RESET and MAKE retain the immediate burst in all spectrum ranks");
        for (size_t band = 0; band < 4; ++band)
            require (std::abs (captured.spectrum.balanceDb[band] - expected->balanceDb[band]) < 1.0e-10,
                     "real RESET and MAKE retain the immediate burst in Balance");
        require (std::abs (captured.integrated - peaks.levels (30).lufs) < 0.0001
            && std::abs (captured.maximumTruePeak - peaks.window (30)) < 0.0001,
            "real MAKE loudness and peak share RESET's spectrum boundary");
    }
}
}
