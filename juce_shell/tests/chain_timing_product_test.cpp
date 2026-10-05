#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "ValidationStorageSandbox.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

static void require (bool ok, const char* why)
{
    if (! ok) { std::cerr << "chain timing product: " << why << '\n'; std::exit (EXIT_FAILURE); }
}
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// The real PRE and POST processors, paired through the product's own discovery and shared ring,
// run by a synthetic host that spends a known time between their callbacks. The host times that
// gap itself, from PRE's return to POST's call: an oracle the chain timing never sees. This is
// not a DAW: it shows the product path end to end, not how any host schedules a track.
// With --empty-chain the host spends nothing between the callbacks, which prints the floor of the
// reading: the two callbacks' own edges.
namespace
{
using Processor = KirinHyphaProcessorBase;
using Steady = std::chrono::steady_clock;
using View = hypha::live_compare::ChainTimingView;
using Reason = hypha::live_compare::ChainTimingReason;
enum class Mode { serial, reversed, otherThread, stopped };

constexpr int blockFrames = 256;
constexpr auto chainTime = std::chrono::microseconds (300);
constexpr auto spikeTime = std::chrono::microseconds (900); // every 50th block

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo value;
        value.setIsPlaying (playing);
        if (playing)
        {
            value.setTimeInSamples (position);
            value.setTimeInSeconds (static_cast<double> (position) / 48000.0);
        }
        return value;
    }
    std::int64_t position = 0;
    bool playing = false;
};

double nanos (Steady::duration value)
{
    return static_cast<double> (std::chrono::duration_cast<std::chrono::nanoseconds> (value).count());
}

class Contract final : private juce::Timer
{
public:
    explicit Contract (bool emptyChainIn) : emptyChain (emptyChainIn)
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "real processors negotiate stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&clock);
            instance->setNonRealtime (false);
            instance->prepareToPlay (48000, blockFrames);
            require (instance->getLatencySamples() == 0, "the measurement adds no latency");
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        started = checkpoint = Steady::now();
        helper = std::thread ([this] { runPreElsewhere(); });
        audio = std::thread ([this] { processAudio(); });
        startTimer (50);
    }
    ~Contract() override
    {
        stopTimer();
        running.store (false);
        if (audio.joinable()) audio.join();
        if (helper.joinable()) helper.join();
        pre->releaseResources();
        post->releaseResources();
    }
    bool passed = false;

private:
    void enter (Mode next, int nextStage)
    {
        mode.store (static_cast<int> (next));
        checkpoint = Steady::now();
        stage = nextStage;
    }
    // The report covers about five seconds, so a new condition needs that long to own it.
    bool settled (const View& view, View::State state, Reason reason, const char* overdue)
    {
        if (view.state == state && view.reason == reason) return true;
        if (Steady::now() - checkpoint >= std::chrono::seconds (12))
            std::cerr << "state=" << static_cast<int> (view.state) << " reason="
                      << static_cast<int> (view.reason) << " counted=" << view.countedShare << '\n';
        require (Steady::now() - checkpoint < std::chrono::seconds (12), overdue);
        return false;
    }
    void timerCallback() override
    {
        const auto now = Steady::now();
        require (now - started < std::chrono::seconds (75), "the contract timed out");
        require (transparencyErrors.load() == 0, "PRE and POST pass their input through bit for bit");
        require (pre->chainTimingView().state == View::State::waiting, "PRE shows no chain timing");
        const auto view = post->chainTimingView();
        switch (stage)
        {
            case 0:
            {
                require (view.state != View::State::measuring, "an unpaired POST measures nothing");
                if (pre->instanceId().isEmpty()) break;
                if (! preview) preview = post->createPairPreview();
                if (! requested)
                {
                    if (! hypha::pair_preview::request (preview)) break;
                    requested = true; checkpoint = now;
                }
                KirinPairPreviewValue value {};
                if (! kirin_hypha_pair_preview_poll (preview.get(), &value) || ! value.complete || ! value.has_single)
                {
                    if (now - checkpoint >= std::chrono::milliseconds (1050) && hypha::pair_preview::request (preview))
                        checkpoint = now;
                    break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "PRE pairing is explicit");
                preview.reset(); ++stage; break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true);
                enter (Mode::serial, 2);
                break;
            case 2:
            {
                if (now - checkpoint < std::chrono::seconds (4)
                    || ! settled (view, View::State::measuring, Reason::none, "a serial pair was never measured"))
                    break;
                const auto count = static_cast<double> (oracleBlocks.load());
                const double oracleMs = static_cast<double> (oracleNanos.load()) / count / 1.0e6;
                const double oraclePeakMs = static_cast<double> (oraclePeakNanos.load()) / 1.0e6;
                std::cout << "serial: chain typical " << view.typicalMs << " ms (host oracle " << oracleMs
                          << "), peak " << view.peakMs << " ms (host oracle " << oraclePeakMs << "), "
                          << view.typicalLoad * 100.0 << "% of a " << view.blockMs << " ms block, counted "
                          << view.countedShare * 100.0 << "% of callbacks; PRE "
                          << static_cast<double> (preNanos.load()) / count / 1000.0 << " us, POST "
                          << static_cast<double> (postNanos.load()) / count / 1000.0 << " us per callback\n";
                require (view.typicalMs >= oracleMs - 0.01 && view.typicalMs <= oracleMs + 0.05,
                         "the typical time is the host's own gap, within the two callbacks' edges");
                require (view.peakMs <= oraclePeakMs + 0.05, "the peak is no more than the host's longest gap");
                if (emptyChain)
                {
                    std::cout << "chain timing floor: the reading above is the two callbacks' own edges\n";
                    passed = true;
                    stopTimer();
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                    break;
                }
                require (oracleMs >= 0.3, "the synthetic chain ran for its time");
                require (view.peakMs >= 0.9, "the peak block is the host's longest gap, not a smoothed value");
                require (std::abs (view.blockMs - blockFrames / 48.0) < 1.0e-6
                             && std::abs (view.typicalLoad - view.typicalMs / view.blockMs) < 1.0e-9,
                         "the share is the time over the block's own length");
                require (view.countedShare > 0.9, "nearly every serial block is counted");
                enter (Mode::reversed, 3);
                break;
            }
            case 3:
                if (settled (view, View::State::unavailable, Reason::orderUnproven,
                             "POST before PRE kept a number"))
                    enter (Mode::otherThread, 4);
                break;
            case 4:
                if (settled (view, View::State::unavailable, Reason::otherThread,
                             "PRE on another thread was not named as the reason"))
                    enter (Mode::stopped, 5);
                break;
            case 5:
                if (settled (view, View::State::unavailable, Reason::notPlaying,
                             "a stopped transport was not named as the reason"))
                    enter (Mode::serial, 6);
                break;
            case 6:
                if (view.state != View::State::measuring)
                {
                    require (now - checkpoint < std::chrono::seconds (12), "the measurement did not return");
                    break;
                }
                require (view.typicalMs >= 0.29 && view.typicalMs <= 0.6, "the returned measurement is current");
                std::cout << "chain timing product PASS: serial pair measured against the host's own gap; "
                             "reversed order, another thread and a stopped transport give a reason, not a number\n";
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default: break;
        }
    }
    // PRE's callback on a second thread, one at a time, while the audio thread waits for it.
    void runPreElsewhere()
    {
        juce::MidiBuffer midi;
        while (running.load())
        {
            if (elsewhere.load (std::memory_order_acquire) != 1) { std::this_thread::yield(); continue; }
            pre->processBlock (*shared, midi);
            elsewhere.store (2, std::memory_order_release);
        }
    }
    void processAudio()
    {
        juce::AudioBuffer<float> buffer (2, blockFrames), input (2, blockFrames);
        juce::MidiBuffer midi;
        shared = &buffer;
        std::int64_t emitted = 0;
        std::uint32_t token = 1;
        std::uint64_t block = 0;
        auto next = Steady::now();
        while (running.load())
        {
            const auto current = static_cast<Mode> (mode.load());
            clock.playing = play.load() && current != Mode::stopped;
            clock.position = emitted;
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    token = token * 1664525u + 1013904223u;
                    input.setSample (c, f, static_cast<float> (token >> 16) / 65536.0f * 0.5f - 0.25f);
                }
            buffer.makeCopyOf (input, true);
            const auto chain = [this, &block]
            {
                if (emptyChain) return;
                const auto until = Steady::now() + (block % 50 == 49 ? spikeTime : chainTime);
                while (Steady::now() < until) {}
            };
            if (current == Mode::reversed)
            {
                post->processBlock (buffer, midi);
                chain();
                pre->processBlock (buffer, midi);
            }
            else
            {
                const auto preBegin = Steady::now();
                if (current == Mode::otherThread)
                {
                    elsewhere.store (1, std::memory_order_release);
                    while (elsewhere.load (std::memory_order_acquire) != 2) std::this_thread::yield();
                    elsewhere.store (0, std::memory_order_release);
                }
                else
                    pre->processBlock (buffer, midi);
                const auto preEnd = Steady::now();
                chain();
                const auto postBegin = Steady::now();
                post->processBlock (buffer, midi);
                const auto postEnd = Steady::now();
                if (current == Mode::serial && clock.playing)
                {
                    const auto gap = static_cast<std::uint64_t> (nanos (postBegin - preEnd));
                    oracleNanos.fetch_add (gap);
                    if (gap > oraclePeakNanos.load()) oraclePeakNanos.store (gap);
                    preNanos.fetch_add (static_cast<std::uint64_t> (nanos (preEnd - preBegin)));
                    postNanos.fetch_add (static_cast<std::uint64_t> (nanos (postEnd - postBegin)));
                    oracleBlocks.fetch_add (1);
                }
            }
            for (int c = 0; c < 2; ++c)
                if (std::memcmp (buffer.getReadPointer (c), input.getReadPointer (c),
                                 sizeof (float) * blockFrames) != 0)
                    transparencyErrors.fetch_add (1);
            if (clock.playing) emitted += blockFrames;
            ++block;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    const bool emptyChain;
    Clock clock;
    std::unique_ptr<Processor> pre, post;
    std::thread audio, helper;
    juce::AudioBuffer<float>* shared = nullptr;
    hypha::pair_preview::Ticket preview;
    int stage = 0;
    bool requested = false;
    Steady::time_point started, checkpoint;
    std::atomic<bool> running { true }, play { false };
    std::atomic<int> mode { static_cast<int> (Mode::serial) }, elsewhere { 0 }, transparencyErrors { 0 };
    std::atomic<std::uint64_t> oracleNanos { 0 }, oraclePeakNanos { 0 }, oracleBlocks { 0 };
    std::atomic<std::uint64_t> preNanos { 0 }, postNanos { 0 };
};
}

int main (int argc, char** argv)
{
    require (argc == 1 || (argc == 2 && std::strcmp (argv[1], "--empty-chain") == 0),
             "usage: chain timing product test [--empty-chain]");
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    hypha::i18n::holdLanguage (true);
    Contract contract (argc == 2);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
