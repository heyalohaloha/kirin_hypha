#include "ReferenceMonoVisualTest.h"
#include "reference_runtime_v2_manifest_test_support.h"
#include "ValidationStorageSandbox.h"
#include "../src/reference_audition/ReferenceVisualObservation.h"
#include "../src/HyphaReferenceComparisonView.h"
#include "../src/HyphaReferenceVersionPage.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace hypha::tests
{
struct MonoReadoutTag
{
    using Type = juce::String (reference_ui::ComparisonView::*)(double, bool) const;
    friend Type monoReadout (MonoReadoutTag);
};
template <MonoReadoutTag::Type Member> struct MonoReadoutAccess
{ friend MonoReadoutTag::Type monoReadout (MonoReadoutTag) { return Member; } };
template struct MonoReadoutAccess<&reference_ui::ComparisonView::valuesAt>;

void verifyMonoVersionVisual()
try
{
    const auto check = [] (bool ok, const char* message) {
        if (! ok) throw std::runtime_error ((juce::String ("Mono Version visual: ") + message).toStdString());
    };
    ValidationStorageSandbox sandbox;
    const auto file = sandbox.directory().getChildFile ("mono.wav");
    juce::AudioBuffer<float> audio (1, 192000);
    std::uint32_t seed = 713;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        audio.setSample (0, i, (float (seed >> 8) / 16777216.0f - 0.5f) * 0.8f);
    }
    {
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.release(), 48000, 1, 32, {}, 0));
        check (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "write mono PCM fixture");
    }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader && reader->read (&audio, 0, audio.getNumSamples(), 0, true, false), "read back stored mono PCM");
    auto source = std::make_shared<ref::RuntimeSource>();
    source->absolutePath = file.getFullPathName(); source->audio = {48000, 1, audio.getNumSamples()};
    const auto rev = makeRuntimeV2FileRevision (file);
    source->revision = {rev["device_id"].toString(), rev["file_id"].toString(), rev["size_bytes"].toString(),
                        rev["mtime_ns"].toString(), rev["ctime_ns"].toString()};
    auto overview = std::make_shared<ref::RuntimeDetailedMeasurement>();
    overview->waveform.emplace(); overview->waveform->framesPerBin = 4800;
    ref::VisualBinding binding; binding.source = source; binding.overview = overview;
    binding.hostRate = 48000; binding.channels = 2; binding.aligned = binding.matched = true;
    binding.key = "mono-visual"; binding.gainDb = 20 * std::log10 (2.0);
    std::mutex mutex; std::condition_variable changed; std::uint64_t sequence = 0;
    ref::VisualObservation observation ([&] {
        { const std::lock_guard<std::mutex> lock (mutex); ++sequence; }
        changed.notify_all(); return binding;
    });
    const auto wait = [&] (auto predicate) {
        const auto watchdog = std::chrono::steady_clock::now() + std::chrono::minutes (5);
        for (;;)
        {
            std::unique_lock<std::mutex> lock (mutex); const auto before = sequence; lock.unlock();
            if (predicate()) return;
            lock.lock();
            check (changed.wait_until (lock, watchdog, [&] { return sequence != before; }), "observation stopped publishing");
        }
    };
    observation.configure (48000, 2); observation.setPresented (true);
    wait ([&] { const auto s = observation.snapshot(); return s && s->pairedObserving && observation.inputGeneration(); });
    auto* monoMeter = kirin_reference_visual_create (48000, 1);
    check (monoMeter != nullptr, "mono loudness meter opens");
    struct MeterCleanup { KirinReferenceVisualMeter* meter; ~MeterCleanup() { kirin_reference_visual_drop (meter); } } cleanup {monoMeter};
    KirinReferenceVisualBin mono {};
    std::array<float, 512> pcm {};
    // Four seconds of content, A at exactly twice the mono file's amplitude in both channels.
    for (int start = 0; start < 192000; start += 4800)
    {
        check (kirin_reference_visual_push (monoMeter, audio.getReadPointer (0, start), 4800)
            && kirin_reference_visual_finish (monoMeter, &mono), "measure file's mono loudness");
        for (int offset = 0; offset < 4800; offset += 256)
        {
            const int count = std::min (256, 4800 - offset);
            for (int i = 0; i < count; ++i)
                pcm[size_t (2*i)] = pcm[size_t (2*i+1)] = 2 * audio.getSample (0, start+offset+i);
            observation.enqueue (pcm.data(), count, 2, start+offset, observation.inputGeneration(), 1);
        }
        wait ([&] { const auto s = observation.snapshot(); return s && s->bins.size() > size_t (start/4800)
            && s->bins[size_t (start/4800)].pass == s->pass; });
    }
    const auto measured = observation.snapshot(); const auto& bin = measured->bins[39];
    check (std::isfinite (mono.short_lufs) && std::abs (bin.b.short_lufs - mono.short_lufs - 10*std::log10 (2.0)) < 0.001,
           "two-channel observation already includes the mono file's channel energy");
    check (std::abs (bin.a.short_lufs - bin.b.short_lufs - binding.gainDb) < 0.001,
           "measured A independently matches the audible gained reference");
    reference_ui::ComparisonView view; view.setSize (600, 300);
    const auto context = presentation::forEditor (900, 600); view.update (measured, 4, context, false);
    auto tokens = juce::StringArray::fromTokens ((view.*monoReadout (MonoReadoutTag {})) (4, false), " \t", "");
    tokens.removeEmptyStrings();
    check (tokens.size() >= 5 && std::abs (tokens[2].getDoubleValue() - bin.a.short_lufs) <= 0.051
        && std::abs (tokens[4].getDoubleValue() - bin.a.short_lufs) <= 0.051,
           "V readout equals independently measured audible A without an extra channel correction");
    check (measured->aPairKirin && measured->vPairKirin, "same-section spectra come from actual PCM");
    auto equivalent = *measured; equivalent.vPairKirin = measured->aPairKirin;
    equivalent.binding.gainDb = 0;
    for (auto& pair : equivalent.bins) pair.b = pair.a;
    const auto wholeImage = [&] (std::shared_ptr<const ref::VisualTimeline> data) {
        reference_ui::ComparisonView chart; chart.setSize (600, 300); chart.update (data, 4, context, false);
        juce::Image result (juce::Image::ARGB, 600, 300, true); juce::Graphics graphics (result); chart.paint (graphics);
        return std::pair {result, chart.visualLayout().graph.toNearestInt()};
    };
    const auto actualWhole = wholeImage (measured), expectedWhole = wholeImage (std::make_shared<const ref::VisualTimeline> (equivalent));
    int wholeMismatch = 0;
    const auto area = actualWhole.second;
    for (int y = area.getY(); y < area.getBottom(); ++y) for (int x = area.getX(); x < area.getRight(); ++x)
        if (actualWhole.first.getPixelAt (x,y) != expectedWhole.first.getPixelAt (x,y)) ++wholeMismatch;
    check (wholeMismatch < 30, "WHOLE curve and vertical range match the independently measured audible level");
    const auto image = [&] (const auto& timeline, double gain) {
        juce::Image result (juce::Image::ARGB, 600, 320, true); juce::Graphics g (result);
        reference_ui::paintVersionSameSection (g, result.getBounds(), &timeline, "Tone", gain, {"spectrum_full"}, false, context);
        return result;
    };
    const auto actual = image (*measured, binding.gainDb), expected = image (equivalent, 0), unshifted = image (*measured, 0);
    int mismatch = 0, positive = 0;
    for (int y = 60; y < 230; ++y) for (int x = 20; x < 580; ++x)
    {
        if (actual.getPixelAt (x,y) != expected.getPixelAt (x,y)) ++mismatch;
        if (expected.getPixelAt (x,y) != unshifted.getPixelAt (x,y)) ++positive;
    }
    check (mismatch < 30 && positive > 200, "V spectrum follows the independently measured audible level");
    std::cout << "mono Version visual: actual mono/dual-mono loudness, numeric readout and spectrum PASS\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n'; std::exit (1);
}
}
