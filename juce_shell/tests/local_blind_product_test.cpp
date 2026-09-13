#include "../src/PluginProcessor.h"
#include "ValidationStorageSandbox.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Blind product: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace
{
using Processor = KirinHyphaProcessorBase;
using Phase = hypha::local_blind::ProductSessionPhase;

juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* child = find (*parent.getChildComponent (i), id)) return child;
    return nullptr;
}

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (position);
        info.setTimeInSeconds (static_cast<double> (position) / 48000.0);
        return info;
    }
    std::int64_t position = 0; // one audio producer; commands are applied between callbacks
    bool playing = false;
};

// This uses the real common processor, Rust C ABI, pair discovery, request/PCM transport,
// preparation, editor actions and audio output. It is not evidence of a DAW wrapper's PDC.
class ProductContract final : private juce::Timer
{
public:
    explicit ProductContract (std::vector<float> signalIn, bool trackMono)
        : signal (std::move (signalIn)), channelCount (trackMono ? 1 : 2)
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            const auto channels = trackMono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set (0, channels);
            layout.outputBuses.set (0, channels);
            require (instance->setBusesLayout (layout), "host negotiates the exact mono/stereo input and output");
            instance->setMeterContextPreference (trackMono ? hypha::meter_context::MeterContext::trackStem
                                                          : hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&clock);
            instance->setNonRealtime (false);
            instance->prepareToPlay (48000, blockFrames);
            require (instance->getLatencySamples() == 0, "processor latency stays zero");
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        editor.reset (post->createEditorIfNeeded());
        require (editor != nullptr, "real product editor opens");
        editor->setSize (600, 400);
        editor->setVisible (true);
        started = std::chrono::steady_clock::now();
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }

    ~ProductContract() override
    {
        stopTimer();
        running.store (false);
        if (audio.joinable()) audio.join();
        closeEditor();
        pre->releaseResources();
        post->releaseResources();
    }

    bool passed = false;

private:
    void closeEditor()
    {
        if (editor != nullptr) post->editorBeingDeleted (editor.get());
        editor.reset();
    }

    bool click (const char* id, bool requireVisible = true)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        require (button != nullptr && button->onClick, id);
        // Processor publication and the editor timer are independent; wait for the actual UI.
        if (! button->isEnabled() || (requireVisible && ! button->isVisible())) return false;
        button->onClick();
        return true;
    }

    void cue (std::int64_t position)
    {
        seek.store (position);
        play.store (true);
    }

    void timerCallback() override
    {
        if (stage != reportedStage)
        {
            std::cout << "stage=" << stage << std::endl;
            reportedStage = stage;
        }
        require (std::chrono::steady_clock::now() - started < std::chrono::seconds (40),
                 "product round trip timed out");
        const auto state = post->localBlindProductView();
        if (state.phase == Phase::failed || state.failure != hypha::local_blind::ProductSessionFailure::none
            || state.trial.failure != hypha::local_blind::TrialFailure::none)
        {
            std::cerr << "stage=" << stage << " phase=" << int (state.phase)
                      << " product_failure=" << int (state.failure)
                      << " trial_failure=" << int (state.trial.failure) << '\n';
            require (false, "product lifecycle failed");
        }
        switch (stage)
        {
            case 0:
            {
                if (pre->instanceId().isEmpty()) break;
                const auto candidates = post->enumeratePreCandidates();
                const bool discovered = std::any_of (candidates.begin(), candidates.end(), [this] (const auto& c)
                    { return c.instanceId == pre->instanceId(); });
                if (! discovered) break;
                require (post->setPairCandidate (pre->instanceId(), {}), "exact discovered PRE is selected");
                ++stage;
                break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true);
                ++stage;
                break;
            case 2:
                if (! post->isPlaying() || ! post->heartbeatLive()) break;
                // The operations menu and this shared editor entry invoke the same owner.
                click ("observatory-local-blind", false);
                if (! click ("local-blind-capture")) break;
                ++stage;
                break;
            case 3:
                if (state.phase != Phase::ready) break;
                std::cout << "prepared frames=" << state.frames << " channels=" << state.channels
                          << " gain_db=" << state.fixedPreGainDb << std::endl;
                require (state.frames == 192000 && state.channels == channelCount, "exact four-second pair prepares");
                require (std::abs (state.fixedPreGainDb + 6.0206) < 0.002,
                         "real FFI gain match measures the known gain within 0.002 dB");
                nativeStart = state.start;
                nativeEnd = state.start + state.frames;
                play.store (false);
                ++stage;
                break;
            case 4:
                if (post->isPlaying()) break;
                if (! click ("local-blind-start"))
                {
                    if (++waitingUi == 25)
                    {
                        const auto* control = find (*editor, "local-blind-start");
                        const auto* status = dynamic_cast<juce::Label*> (find (*editor, "local-blind-status"));
                        std::cout << "start visible=" << control->isVisible()
                                  << " enabled=" << control->isEnabled()
                                  << " status=" << (status != nullptr ? status->getText() : "missing") << std::endl;
                    }
                    break;
                }
                passNumber.store (1);
                cue (nativeStart - 1003);
                ++stage;
                break;
            case 5:
                if (! state.trial.passComplete) break;
                require (! state.trial.canAnswer, "first side alone cannot answer");
                play.store (false);
                ++stage;
                break;
            case 6:
                if (post->isPlaying()) break;
                if (! click ("local-blind-source-2")) break;
                passNumber.store (2);
                cue (nativeStart - 1003);
                ++stage;
                break;
            case 7:
                if (! state.trial.passComplete || ! state.trial.canAnswer) break;
                if (! click ("local-blind-answer-same")) break;
                ++stage;
                break;
            case 8:
                if (state.trial.answer == hypha::local_blind::TrialAnswer::none) break;
                if (! click ("local-blind-reveal")) break;
                ++stage;
                break;
            case 9:
                if (state.phase != Phase::revealed) break;
                require ((correlationOne.load() > 0) == (state.trial.revealedOneSide == 1)
                             && correlationOne.load() * correlationTwo.load() < 0,
                         "revealed assignment agrees with the two distinct captured PCM outputs");
                if (! click ("local-blind-stop")) break;
                ++stage;
                break;
            case 10:
                if (state.phase != Phase::returnPending) break;
                // Reopening recovers explicit return; it must never resume the audition.
                if (! reopened)
                {
                    closeEditor();
                    editor.reset (post->createEditorIfNeeded());
                    editor->setSize (300, 200);
                    editor->setVisible (true);
                    reopened = true;
                }
                if (! click ("local-blind-return")) break;
                ++stage;
                break;
            case 11:
                if (state.phase != Phase::returned) break;
                require (preTransparent.load() && postTransparent.load(), "normal PRE/POST paths stay bit identical");
                require (maximumCopyError.load() < 0.00004, "matched output stays within fixed-gain quantization bound");
                require (auditionSamples.load() >= 192000 * 2, "actual audio output covered both complete sides");
                std::cout << "Blind product: PASS (real C ABI, pair/PCM transport, editor reopen, two complete passes,"
                             " answer/reveal/return); max_copy_error=" << maximumCopyError.load()
                          << " audition_samples=" << auditionSamples.load() << '\n';
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
        }
    }

    void processAudio()
    {
        juce::AudioBuffer<float> buffer (channelCount, blockFrames);
        juce::MidiBuffer midi;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            const auto nextPosition = seek.exchange (-1);
            if (nextPosition >= 0) clock.position = nextPosition;
            clock.playing = play.load();
            for (int c = 0; c < channelCount; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
            pre->processBlock (buffer, midi);
            for (int c = 0; c < channelCount; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    const auto expected = signal[static_cast<std::size_t> (clock.position + f) % signal.size()];
                    const auto actual = buffer.getSample (c, f);
                    if (std::memcmp (&actual, &expected, sizeof (float)) != 0) preTransparent.store (false);
                    buffer.setSample (c, f, expected * -0.5f);
                }
            post->processBlock (buffer, midi);
            const auto begin = nativeStart.load(), end = nativeEnd.load();
            for (int f = 0; f < blockFrames; ++f)
            {
                const auto position = clock.position + f;
                const bool inRange = end > begin && clock.playing && position >= begin && position < end;
                for (int c = 0; c < channelCount; ++c)
                {
                    const auto expected = signal[static_cast<std::size_t> (position) % signal.size()] * -0.5f;
                    const auto error = std::abs (buffer.getSample (c, f) - expected);
                    if (inRange && position >= begin + 240 && position < end - 240)
                    {
                        const auto output = buffer.getSample (c, f);
                        maximumCopyError.store (std::max (maximumCopyError.load(),
                            std::abs (std::abs (output) - std::abs (expected))));
                        auto& correlation = passNumber.load() == 1 ? correlationOne : correlationTwo;
                        correlation.store (correlation.load() + output * -expected);
                    }
                    else if (! inRange && error != 0.0f) postTransparent.store (false);
                }
                if (inRange) ++auditionSamples;
            }
            if (clock.playing) clock.position += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    static constexpr int blockFrames = 2048;
    Clock clock;
    std::vector<float> signal;
    const int channelCount;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false }, preTransparent { true }, postTransparent { true };
    std::atomic<std::int64_t> seek { -1 }, nativeStart { 0 }, nativeEnd { 0 };
    std::atomic<std::uint64_t> auditionSamples { 0 };
    std::atomic<float> maximumCopyError { 0 };
    std::atomic<int> passNumber { 0 };
    std::atomic<double> correlationOne { 0 }, correlationTwo { 0 };
    std::chrono::steady_clock::time_point started;
    int stage = 0, reportedStage = -1, waitingUi = 0;
    bool reopened = false;
};
}

int main (int argc, char** argv)
{
    require (argc == 2 || (argc == 3 && std::string (argv[2]) == "--track-mono"),
             "usage: product test S-1.wav [--track-mono]");
    const bool trackMono = argc == 3;
    auto signal = readFixture (argv[1]);
    if (trackMono) std::fill (signal.begin() + 48000, signal.end(), 0.0f);
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    ProductContract contract (std::move (signal), trackMono);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
