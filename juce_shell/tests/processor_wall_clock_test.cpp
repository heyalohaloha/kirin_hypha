#include "ProductTestClock.h"
#include "LiveBlindStatusProductChecks.h"
#include "AnalysisWorkerReceipt.h"
#include "ReferenceValidationStorageSandbox.h"
#include <iostream>
#include "TestProcessorStorage.h"
#include "PrecisionTraceTest.h"
#include "DoublePrecisionLiveAuditionTest.h"
#include "SpaceLiveContract.h"
namespace
{
using Processor = KirinHyphaProcessorBase;
void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
struct Host : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true); p.setTimeInSamples (position);
        p.setKirinAuxiliaryClockSource (source);
        p.setKirinAuxiliaryClockSamples (position + 960000);
        p.setKirinAuUsesHostTransportTimeline (true);
        return p;
    }
    std::int64_t position = 0;
    int source = 1;
};

template <typename Float>
void verifySpaceAaxChunkClock (Processor::Role role)
{
    hypha::tests::ProductTestClock clock;
    struct ChunkClock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setIsPlaying (true); p.setTimeInSamples (project);
            p.setPpqPosition (double (project) / 24000.0); p.setBpm (120.0);
            p.setKirinAuxiliaryClockSource (3);
            if (available) p.setKirinAuxiliaryClockSamples (engine);
            return p;
        }
        std::int64_t project = 0, engine = 960000;
        bool available = true;
    } host;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
    Processor processor (role);
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    require (processor.setBusesLayout (layout), "AAX chunk SPACE negotiates actual stereo buses");
    processor.setProcessingPrecision (sizeof (Float) == sizeof (double)
        ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
    processor.setPlayHead (&host); hypha::tests::prepareFixtureProcessor (processor, 48'000, 256);
    LiveTimingFixtureAccess::configureAaxClock (processor);
    hypha::observatory::View view (role == Processor::Role::Pre ? hypha::observatory::Role::pre : hypha::observatory::Role::post);
    view.setVisible (true); view.setSpaceLiveTap (&processor.spaceLive()); view.setDomain (hypha::observatory::Domain::space);
    auto& screen = hypha::tests::space_live_contract::display (view);
    juce::AudioBuffer<Float> audio (2, 256); juce::MidiBuffer midi;
    std::size_t written = 0;
    const auto sample = [] (std::size_t index, int channel) {
        return float (index + 1) / 131072.0f * (channel == 0 ? 1.0f : -1.0f);
    };
    const auto process = [&] {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i)
            audio.setSample (c, i, Float (sample (written + std::size_t (i), c)));
        juce::AudioBuffer<Float> original; original.makeCopyOf (audio);
        processor.processBlock (audio, midi); screen.refresh (clock.nanos()); written += 256;
        require (! screen.frame().samples.empty(), "AAX chunk publishes fresh original A on every callback");
        const auto count = screen.frame().samples.size();
        for (std::size_t i = 0; i < count; ++i)
            require (hypha::tests::space_live_contract::sameSample (screen.frame().samples[i].left, sample (written - count + i, 0))
                     && hypha::tests::space_live_contract::sameSample (screen.frame().samples[i].right, sample (written - count + i, 1)),
                     "AAX chunk SPACE keeps every latest sample in order, without duplicate or missing PCM");
        for (int c = 0; c < 2; ++c)
            require (std::memcmp (audio.getReadPointer (c), original.getReadPointer (c), 256 * sizeof (Float)) == 0,
                     "AAX chunk SPACE preserves every A bit in both precisions");
        host.engine += 256; clock.advanceBlock (256, 48'000);
    };
    const auto chunk = [&] (bool expectFresh = false) {
        const auto previous = screen.frame().span;
        for (int sub = 0; sub < 4; ++sub)
        {
            process();
            if (sub == 0 && expectFresh)
                require (screen.frame().samples.size() == 256 && screen.frame().span != previous,
                         "seek, Reset or engine proof loss immediately discards the old SPACE window");
        }
        host.project += 1024; // same project position for four callbacks, then an exact chunk step
    };
    const auto fill = [&] {
        for (int i = 0; i < 5; ++i) chunk();
        require (screen.frame().samples.size() == 3840,
                 "proven AAX chunks accumulate the full ordered 80 ms SPACE window");
        const auto span = screen.frame().span; chunk();
        require (screen.frame().span == span && screen.frame().samples.size() == 3840,
                 "subsequent AAX chunks retain full display density without false seek resets");
    };
    chunk(); require (screen.frame().samples.size() == 256, "unclosed repeated reports never prove a chunk");
    fill();
    for (const auto destination : { 32768, -8192 })
    { host.project = destination; chunk (true); fill(); }
    processor.kirinHostTimelineReset (true, true); processor.reset(); chunk (true); fill();
    host.engine += 256; chunk (true); fill(); // project alone appears continuous
    host.available = false; chunk (true);
    require (screen.frame().samples.size() == 256, "missing engine clock cannot retain a stale chunk proof");
    host.available = true; chunk (true); fill();
    LiveTimingFixtureAccess::configureAaxClock (processor, false); chunk (true);
    require (screen.frame().samples.size() == 256, "uncertified AAX reports cannot borrow an old chunk proof");
    LiveTimingFixtureAccess::configureAaxClock (processor); chunk (true); fill();
    require (processor.spaceLive().enabled() && screen.running() && processor.getLatencySamples() == 0,
             "chunk recovery retains SPACE selection, visible demand and zero latency");
    view.setSpaceLiveTap (nullptr); processor.setPlayHead (nullptr); processor.releaseResources();
}

template <typename Float>
void verifySpaceAuRenderClock (Processor::Role role)
{
    hypha::tests::ProductTestClock clock;
    struct RenderClock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; // No transport callback: playing remains false.
            p.setTimeInSamples (position); p.setKirinAuUsesHostTransportTimeline (false);
            return p;
        }
        std::int64_t position = 0;
    } host;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnit);
    Processor processor (role);
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    require (processor.setBusesLayout (layout), "AU render-clock fixture negotiates actual stereo buses");
    processor.setProcessingPrecision (sizeof (Float) == sizeof (double)
        ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
    processor.setPlayHead (&host); hypha::tests::prepareFixtureProcessor (processor, 48'000, 64);
    hypha::observatory::View view (role == Processor::Role::Pre ? hypha::observatory::Role::pre : hypha::observatory::Role::post);
    view.setVisible (true); view.setSpaceLiveTap (&processor.spaceLive()); view.setDomain (hypha::observatory::Domain::space);
    auto& screen = hypha::tests::space_live_contract::display (view);
    juce::AudioBuffer<Float> audio (2, 64); juce::MidiBuffer midi;
    int block = 0;
    const auto process = [&] (std::size_t expected) {
        ++block;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i)
            audio.setSample (c, i, Float (0.1 + block * 0.01 + i * 0.00001 + c * 0.003));
        juce::AudioBuffer<Float> original; original.makeCopyOf (audio);
        processor.processBlock (audio, midi); screen.refresh (clock.nanos());
        if (screen.frame().samples.size() != expected)
            std::cerr << "AU SPACE role=" << int (role) << " precision=" << sizeof (Float) * 8
                      << " block=" << block << " samples=" << screen.frame().samples.size() << " expected=" << expected << '\n';
        require (screen.frame().samples.size() == expected, "AU render-only clock keeps only the continuous input span");
        for (int c = 0; c < 2; ++c)
            require (std::memcmp (audio.getReadPointer (c), original.getReadPointer (c), 64 * sizeof (Float)) == 0,
                     "AU render-only SPACE preserves every A bit in both precisions");
        for (std::size_t i = 0; i < 64; ++i)
        {
            const auto sample = screen.frame().samples[expected - 64 + i];
            require (hypha::tests::space_live_contract::sameSample (sample.left, float (original.getSample (0, int (i))))
                     && hypha::tests::space_live_contract::sameSample (sample.right, float (original.getSample (1, int (i)))),
                     "AU seek/reset recovery contains the exact fresh input samples");
        }
        host.position += 64; clock.advanceBlock (64, 48'000);
    };
    process (64); process (128);
    auto span = screen.frame().span;
    host.position = 4096; process (64);
    require (screen.frame().span != span, "forward render seek without transport cannot mix old points");
    process (128); span = screen.frame().span;
    host.position = 0; process (64);
    require (screen.frame().span != span, "backward render seek without transport cannot mix old points");
    process (128);
    for (const auto transport : { 0, 1, 2 })
    {
        span = screen.frame().span;
        // The AU wrapper's actual processor notification, followed by its normal reset call.
        // Render position stays continuous: only the forwarded Reset generation can cut it.
        processor.kirinHostTimelineReset (transport != 0, transport == 1); processor.reset();
        process (64);
        require (screen.frame().span != span && processor.spaceLive().enabled(),
                 "AU Reset cuts the window without cancelling visible SPACE demand");
        process (128);
    }
    require (processor.getLatencySamples() == 0, "AU render-only SPACE adds zero latency");
    view.setSpaceLiveTap (nullptr); processor.setPlayHead (nullptr); processor.releaseResources();
}

void verifySpaceBurstExpiry (Processor::Role role, juce::AudioProcessor::WrapperType wrapper)
{
    hypha::tests::ProductTestClock clock;
    Host host;
    juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
    Processor processor (role);
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    require (processor.setBusesLayout (layout), "SPACE burst fixture negotiates actual stereo buses");
    processor.setPlayHead (&host); hypha::tests::prepareFixtureProcessor (processor, 48'000, 512);
    hypha::observatory::View view (role == Processor::Role::Pre ? hypha::observatory::Role::pre : hypha::observatory::Role::post);
    view.setVisible (true); view.setSpaceLiveTap (&processor.spaceLive()); view.setDomain (hypha::observatory::Domain::space);
    auto& screen = hypha::tests::space_live_contract::display (view);
    juce::AudioBuffer<float> buffer (2, 512); juce::MidiBuffer midi;
    const auto process = [&] (int block) {
        for (int i = 0; i < 512; ++i)
        {
            const float sample = float (block * 512 + i + 1) / 8192.0f;
            buffer.setSample (0, i, sample); buffer.setSample (1, i, -sample);
        }
        juce::AudioBuffer<float> original; original.makeCopyOf (buffer);
        processor.processBlock (buffer, midi);
        for (int c = 0; c < 2; ++c)
            require (std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), 512 * sizeof (float)) == 0,
                     "SPACE burst leaves every original A bit intact");
        host.position += 512;
    };
    for (int i = 0; i < 8; ++i)
    { if (i != 0) clock.advance (std::chrono::milliseconds (1)); process (i); }
    screen.refresh (clock.nanos());
    require (screen.frame().samples.size() == 3840, "processBlock burst reaches the visible SPACE window");
    const auto previous = screen.frame().span;
    // Suspend the writer inside a publication. The real timer must clear its last accepted
    // processBlock trace even when every intervening read reports contention.
    using namespace hypha::tests::space_live_contract;
    (processor.spaceLive().*displayMember (BeginWriteMember {}))();
    clock.advance (std::chrono::milliseconds (120)); tick (screen);
    require (screen.frame().samples.size() == 3840, "busy timer retains the trace at the exact deadline");
    clock.advance (std::chrono::nanoseconds (1)); tick (screen);
    require (screen.frame().samples.empty() && screen.running() && processor.spaceLive().enabled(),
             "real display timer expires busy PCM without cancelling the selected page");
    clock.advance (std::chrono::milliseconds (16)); tick (screen);
    require (screen.frame().samples.empty(), "repeated busy timer reads cannot revive expired PCM");
    (processor.spaceLive().*displayMember (EndWriteMember {})) (screen.frame().callbackNanos);
    clock.advance (std::chrono::milliseconds (14)); process (8); tick (screen);
    require (screen.frame().span != previous && screen.frame().samples.size() == 512,
             "first resumed processBlock shows a fresh span immediately, retaining SPACE selection");
    for (int i = 0; i < 512; ++i)
    {
        const float sample = float (8 * 512 + i + 1) / 8192.0f;
        require (hypha::tests::space_live_contract::sameSample (screen.frame().samples[std::size_t (i)].left, sample)
                 && hypha::tests::space_live_contract::sameSample (screen.frame().samples[std::size_t (i)].right, -sample),
                 "visible recovery contains only the exact new callback samples");
    }
    require (processor.getLatencySamples() == 0, "SPACE burst adds no latency");
    view.setSpaceLiveTap (nullptr); processor.setPlayHead (nullptr); processor.releaseResources();
}

void verify (Processor::Role role, int channels, int delayMs, bool realControl)
{
    hypha::tests::ProductTestClock clock;
    Host host;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (role);
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
    require (processor.setBusesLayout (layout), "layout admitted");
    constexpr int frames = 2048;
    processor.setPlayHead (&host); hypha::tests::prepareFixtureProcessor (processor, 96000, frames);
    processor.spaceLive().setEnabled (true);
    require (LiveTimingFixtureAccess::configureVst3Clock (processor, "REAPER", "7.71"), "host policy");
    require (! LiveTimingFixtureAccess::timingRingPublished (processor), "regression requires no shared memory");
    juce::AudioBuffer<float> buffer (channels, frames);
    juce::MidiBuffer midi;
    for (int block = 0; block < 6; ++block)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (delayMs));
        if (realControl) hypha::live_compare::testCallbackNanos.store (0);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i) buffer.setSample (ch, i, 0.125f);
        processor.processBlock (buffer, midi);
        hypha::tests::awaitAnalysisInput (processor);
        const auto reading = LiveTimingFixtureAccess::aaxLocalClock (processor);
        require (reading.valid && reading.start == host.position,
                 "slow callback must retain the actual processor's analysis clock");
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
                require (buffer.getSample (ch, i) == 0.125f, "normal A remains bit identical");
        require (processor.getLatencySamples() == 0, "zero latency");
        hypha::space_live::Snapshot screen;
        require (processor.spaceLive().read (screen, 0) == (channels == 2 ? hypha::space_live::Read::ready : hypha::space_live::Read::empty),
                 "optional SPACE copy follows actual callbacks and never invents stereo on mono");
        host.position += frames; clock.advanceBlock (frames, 96000);
    }
    clock.advance (std::chrono::milliseconds (600));
    processor.processBlock (buffer, midi);
    require (! LiveTimingFixtureAccess::aaxLocalClock (processor).valid, "explicit gap retires analysis");
    processor.releaseResources();
}
}
int main (int argc, char** argv)
try
{
    ReferenceValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI init;
    const auto mode = argc == 2 ? juce::String (argv[1]) : juce::String {};
    if (mode.isEmpty() || mode == "--space-aax-only")
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        { verifySpaceAaxChunkClock<float> (role); verifySpaceAaxChunkClock<double> (role); }
        std::cout << "SPACE AAX chunks: PRE/POST float/double, ordered 3840 samples, seek/Reset/engine/certificate recovery PASS\n";
        if (mode == "--space-aax-only") return 0;
    }
    if (mode == "--double-precision-layout-only") { hypha::tests::precision::verifyDoublePrecisionListenLayout(); return 0; }
    if (mode == "--double-precision-live-only") { hypha::tests::precision::verifyDoublePrecisionLiveAudition(); return 0; }
    if (mode == "--double-precision-parity-only")
    {
        hypha::tests::precision::verifyDoublePrecisionListenLayout();
        hypha::tests::precision::verifyDoublePrecisionParity(); hypha::tests::precision::verifyPrecisionTrace();
        return 0;
    }
    // The optional negative control uses the old real-clock route under exactly the same
    // assertion; it must fail with 175 ms callbacks. It is not a normal CTest item.
    if (argc > 1) { verify (Processor::Role::Pre, 2, 175, true); return 0; }
    hypha::tests::verifyBlindInterruptionStatus();
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
    { verifySpaceAuRenderClock<float> (role); verifySpaceAuRenderClock<double> (role); }
    std::cout << "SPACE AU render-only clock: PRE/POST float/double, seek and continuous-clock Reset PASS\n";
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        for (auto wrapper : { juce::AudioProcessor::wrapperType_AudioUnit, juce::AudioProcessor::wrapperType_VST3,
                              juce::AudioProcessor::wrapperType_AAX }) verifySpaceBurstExpiry (role, wrapper);
    std::cout << "SPACE busy expiry: real processBlock -> frame timer, PRE/POST AU/VST3/AAX PASS\n";
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        for (int channels : { 1, 2 })
            for (int delayMs : { 0, 40, 175 }) verify (role, channels, delayMs, false);
    std::cout << "processor clock: PRE/POST mono/stereo, 0/40/175 ms, gap, bit identity, zero latency; "
                 "Blind missing-project callback/status/teardown/recovery PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
