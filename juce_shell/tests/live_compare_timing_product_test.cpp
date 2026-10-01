#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"
#include "LiveBlindLoopFixture.h"
#include "LiveTimingProductDiagnostic.h"
#include <array>
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
    if (! ok) { std::cerr << "timing preparation product: " << why << '\n'; std::exit (EXIT_FAILURE); }
}
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace
{
using Processor = KirinHyphaProcessorBase;
using Stage = hypha::live_compare::BlindStage;
juce::Component* find (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id) return &root;
    for (int i = 0; i < root.getNumChildComponents(); ++i)
        if (auto* found = find (*root.getChildComponent (i), id)) return found;
    return nullptr;
}

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo value; value.setIsPlaying (playing);
        if (playing)
        {
            value.setTimeInSamples (position);
            value.setTimeInSeconds (static_cast<double> (position) / 48000.0);
            loop->decorate (value, position);
        }
        return value;
    }
    std::int64_t position = 0;
    bool playing = false;
    const LiveBlindLoopFixture* loop = nullptr;
};

// Test-only compressor and level-dependent band attenuation. Envelopes continue across wraps;
// neither state is reset each lap. This is not certification of any third-party plug-in.
struct DynamicChain
{
    float process (float input, int channel)
    {
        auto& state = channels[static_cast<std::size_t> (channel)];
        state.low += 0.08f * (input - state.low);
        const float high = input - state.low;
        const float magnitude = std::abs (input);
        state.envelope += (magnitude > state.envelope ? 0.005f : 0.00014f) * (magnitude - state.envelope);
        const float compression = state.envelope > 0.08f ? std::sqrt (0.08f / state.envelope) : 1.0f;
        const float bandGain = 1.0f / (1.0f + 3.0f * state.envelope);
        minimum = std::min (minimum, compression); maximum = std::max (maximum, compression);
        return (state.low + high * bandGain) * compression;
    }
    struct State { float low = 0, envelope = 0; };
    std::array<State, 2> channels {};
    float minimum = 1, maximum = 0;
};

class Contract final : private juce::Timer
{
public:
    explicit Contract (std::vector<float> signalIn) : signal (std::move (signalIn))
    {
        preClock.loop = postClock.loop = &loop;
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "real processors negotiate stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
            instance->setNonRealtime (false);
            instance->prepareToPlay (48000, blockFrames);
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        editor.reset (post->createEditorIfNeeded());
        require (editor != nullptr, "real editor opens");
        editor->setSize (900, 600); editor->setVisible (true);
        started = checkpoint = std::chrono::steady_clock::now();
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }
    ~Contract() override
    {
        stopTimer(); running.store (false);
        if (audio.joinable()) audio.join();
        if (editor != nullptr) post->editorBeingDeleted (editor.get());
        editor.reset(); pre->releaseResources(); post->releaseResources();
    }
    bool passed = false;

private:
    bool click (const char* id)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        if (button == nullptr || ! button->isEnabled() || ! button->isVisible() || ! button->onClick) return false;
        button->onClick(); ++clicks; return true;
    }
    bool verifyContentOffset()
    {
        const auto estimate = post->measureLiveCompareOffset(); // non-RT, real content window
        if (! estimate.determined) return false; // wait for a coherent window, within the existing deadline
        require (std::llabs (estimate.lagFrames) <= 1,
                 "broadband dynamic processing retains a determined zero content offset");
        std::cout << "dynamic content offset: lag=" << estimate.lagFrames
                  << " peak=" << estimate.peak << " dominance=" << estimate.dominance << '\n';
        return true;
    }
    bool finishAudit()
    {
        // Withdraw the test-only audit and wait for its in-flight writer before reading final
        // counters. A last audio increment must not race the next Source's baseline.
        audit.store (false, std::memory_order_seq_cst);
        return auditReaders.load (std::memory_order_seq_cst) == 0;
    }
    void timerCallback() override
    {
        diagnostic.setPhase (stage);
        diagnostic.printReady();
        const auto now = std::chrono::steady_clock::now();
        if (now - started >= std::chrono::seconds (60))
            std::cerr << "stage=" << stage << " blind=" << static_cast<int> (post->liveBlindStatus().stage)
                      << " verdict=" << static_cast<int> (post->liveCompareStatus().verdict)
                      << " reason=" << static_cast<int> (post->liveCompareStatus().reason) << '\n';
        require (now - started < std::chrono::seconds (60), "linear -> loop -> late Blind timed out");
        require (pcmErrors.load() == 0 && rawErrors.load() == 0 && preErrors.load() == 0, "independent full-frame PCM oracle");
        if (stage >= 5 && stage <= 8)
        {
            const auto status = post->liveCompareStatus();
            require (post->liveBlindStatus().stage == Stage::active && status.matchReady
                && std::fabs (status.gain - fixedGain) <= 0.0f, "loop and dynamic processing retain the same MATCH and Blind trial");
        }
        switch (stage)
        {
            case 0:
            {
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
                    if (now - checkpoint >= std::chrono::milliseconds (1050) && hypha::pair_preview::request (preview)) checkpoint = now;
                    break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "PRE pairing is explicit");
                preview.reset(); ++stage; break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true); checkpoint = now; ++stage; break;
            case 2:
                if (now - checkpoint < std::chrono::seconds (2)) break;
                require (! post->liveCompareStatus().active && ! post->liveCompareStatus().matched,
                         "ordinary playback prepares clocks without starting an audition or MATCH");
                loop.requested.store (true); ++stage; break;
            case 3:
                if (loop.laps.load() < 3) break;
                rawAudit.store (false);
                if (! click ("observatory-local-blind")) break; // first audition action, already looping
                ++stage; break;
            case 4:
            {
                const auto trial = post->liveBlindStatus();
                require (trial.stage != Stage::invalidated && trial.stage != Stage::failed && trial.stage != Stage::approval,
                         "quiet dynamic chain needs no approval and normal loops never invalidate preparation");
                if (trial.stage != Stage::active || trial.trial.played != 1) break;
                fixedGain = post->liveCompareStatus().gain;
                require (fixedGain > 0 && fixedGain < 1 && pre->getLatencySamples() == 0 && post->getLatencySamples() == 0,
                         "MATCH attenuates the audition copy without adding latency");
                require (find (*editor, "live-blind-screen")->isVisible(), "anonymous screen is visible");
                expectedGain.store (fixedGain);
                observedBlock = blocks.load(); stage = 5; break;
            }
            case 5:
                if (blocks.load() <= observedBlock + 3) break;
                audit.store (true); checkpointLap = loop.laps.load(); stage = 6; break;
            case 6:
                if (loop.laps.load() < checkpointLap + 10) break;
                if (! verifyContentOffset()) break;
                if (! finishAudit()) break;
                firstPre = preFrames.load(); firstPost = postFrames.load();
                require ((firstPre >= 9 * LiveBlindLoopFixture::length && firstPost == 0)
                    || (firstPost >= 9 * LiveBlindLoopFixture::length && firstPre == 0), "Source 1 is exactly one verified physical occurrence");
                if (! click ("live-blind-source-2")) break;
                observedBlock = blocks.load(); stage = 7; break;
            case 7:
                if (post->liveBlindStatus().trial.played != 3 || blocks.load() <= observedBlock + 3) break;
                audit.store (true); checkpointLap = loop.laps.load(); stage = 8; break;
            case 8:
                if (loop.laps.load() < checkpointLap + 10) break;
                if (! verifyContentOffset()) break;
                if (! finishAudit()) break;
                require (firstPre > 0 ? postFrames.load() >= 9 * LiveBlindLoopFixture::length && preFrames.load() == firstPre
                                     : preFrames.load() >= 9 * LiveBlindLoopFixture::length && postFrames.load() == firstPost,
                         "Source 2 is the other correctly timed full-frame signal");
                if (! click ("live-blind-reveal")) break;
                require (post->liveBlindStatus().trial.revealed
                    && post->liveBlindStatus().trial.firstPre == (firstPre > 0), "reveal agrees with the independently observed PCM");
                require (click ("live-blind-end"), "one END ends the comparison");
                stage = 9; break;
            case 9:
                post->serviceLiveCompare();
                if (post->liveCompareStatus().active || post->liveCompareStatus().finishing) break;
                require (post->liveCompareStatus().postActual == 1.0f && ! find (*editor, "live-blind-screen")->isVisible(),
                         "END restores ordinary unity output and dismisses the screen");
                rawAudit.store (true); observedBlock = blocks.load(); stage = 10; break;
            case 10:
                if (blocks.load() <= observedBlock + 8) break;
                require (clicks == 4 && minimumGain.load() + 0.1f < maximumGain.load(),
                         "four audition clicks; compressor gain truly changes with the input");
                std::cout << "clock preparation product PASS: late loop BLIND, compressor/dynamic band, 20 audited laps, "
                          << preFrames.load() + postFrames.load() << " full-frame samples/side, 4 clicks, bit-identical ordinary A\n";
                passed = true; stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop(); break;
            default: break;
        }
    }
    void processAudio()
    {
        juce::AudioBuffer<float> buffer (2, blockFrames);
        juce::MidiBuffer midi;
        std::array<std::array<float, 4096>, 2> physicalDelay {};
        std::array<std::array<float, blockFrames>, 2> input {}, delayed {}, processed {};
        DynamicChain dynamics;
        std::size_t head = 0;
        std::int64_t emitted = 0;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            preClock.playing = postClock.playing = play.load();
            preClock.position = emitted; postClock.position = emitted - 4096;
            loop.advance (emitted);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    const auto phase = (emitted + f) % LiveBlindLoopFixture::length;
                    const float envelope[] { 0.9f, 0.2f, 0.7f, 0.1f };
                    const auto index = static_cast<std::size_t> ((phase + c * 12) % static_cast<std::int64_t> (signal.size()));
                    // A fixed, rendered-like broadband loop, not new random material each lap.
                    // Its S-1 component is checked from disk; the fixed broadband component
                    // challenges the full-frame source/gain oracle beyond a single sine wave.
                    auto token = static_cast<std::uint32_t> (phase + c * 37 + 1) * 0x9e3779b1u;
                    token = (token ^ (token >> 16)) * 0x85ebca6bu;
                    token = (token ^ (token >> 13)) * 0xc2b2ae35u;
                    token ^= token >> 16;
                    const float noise = static_cast<float> (token & 0xffffu) / 32768.0f - 1.0f;
                    input[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)]
                        = (signal[index] * 0.35f + noise * 0.2f) * envelope[phase / 6000];
                    buffer.setSample (c, f, input[static_cast<std::size_t> (c)][static_cast<std::size_t> (f)]);
                }
            pre->processBlock (buffer, midi);
            for (int f = 0; f < blockFrames; ++f)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const auto channel = static_cast<std::size_t> (c), frame = static_cast<std::size_t> (f);
                    const float original = buffer.getSample (c, f);
                    if (std::memcmp (&original, &input[channel][frame], sizeof (float)) != 0) preErrors.fetch_add (1);
                    delayed[channel][frame] = physicalDelay[channel][head];
                    physicalDelay[channel][head] = original;
                    processed[channel][frame] = dynamics.process (delayed[channel][frame], c);
                    buffer.setSample (c, f, processed[channel][frame]);
                }
                head = (head + 1) % 4096;
            }
            auditReaders.fetch_add (1, std::memory_order_seq_cst);
            const bool ordinary = rawAudit.load(), comparing = audit.load (std::memory_order_seq_cst);
            const float gain = expectedGain.load();
            post->processBlock (buffer, midi);
            diagnostic.observe (*pre, *post, blocks.load());
            bool preMatch = true, postMatch = true;
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    const auto channel = static_cast<std::size_t> (c), frame = static_cast<std::size_t> (f);
                    const float actual = buffer.getSample (c, f);
                    const float expectedPost = processed[channel][frame];
                    if (ordinary && rawAudit.load() && std::memcmp (&actual, &expectedPost, sizeof (float)) != 0) rawErrors.fetch_add (1);
                    preMatch = preMatch && std::fabs (actual - delayed[channel][frame] * gain) <= 0.0f;
                    postMatch = postMatch && std::fabs (actual - expectedPost) <= 0.0f;
                }
            if (comparing && audit.load())
            {
                if (! preMatch && ! postMatch) pcmErrors.fetch_add (1);
                if (preMatch) preFrames.fetch_add (blockFrames);
                if (postMatch) postFrames.fetch_add (blockFrames);
            }
            auditReaders.fetch_sub (1, std::memory_order_seq_cst);
            minimumGain.store (dynamics.minimum); maximumGain.store (dynamics.maximum);
            blocks.fetch_add (1); emitted += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }
    static constexpr int blockFrames = 1024;
    Clock preClock, postClock;
    LiveBlindLoopFixture loop;
    LiveTimingProductDiagnostic diagnostic;
    std::vector<float> signal;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    hypha::pair_preview::Ticket preview;
    int stage = 0, clicks = 0, observedBlock = 0;
    bool requested = false;
    float fixedGain = 1;
    std::int64_t checkpointLap = 0;
    std::uint64_t firstPre = 0, firstPost = 0;
    std::chrono::steady_clock::time_point started, checkpoint;
    std::atomic<bool> running { true }, play { false }, audit { false }, rawAudit { true };
    std::atomic<unsigned> auditReaders { 0 };
    std::atomic<int> blocks { 0 }, preErrors { 0 }, rawErrors { 0 }, pcmErrors { 0 };
    std::atomic<float> expectedGain { 1 }, minimumGain { 1 }, maximumGain { 0 };
    std::atomic<std::uint64_t> preFrames { 0 }, postFrames { 0 };
};
}

int main (int argc, char** argv)
{
    require (argc == 2, "usage: timing product test S-1.wav");
    auto signal = readFixture (argv[1]);
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    hypha::i18n::holdLanguage (true);
    Contract contract (std::move (signal));
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
