#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Live PIN product: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#include "LocalBlindSignalFixture.h"
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// INV-LC15 end to end: the real PRE and POST processors, the Rust C ABI, pair discovery, the shared
// live ring, the product editor and PRE / POST Blind. POST hears PRE at -6 dB. LISTEN starts a live
// session; after more than four seconds of proven playback PIN fixes the last four seconds and
// Blind prepares them without its own capture: an exact four-second pair with Gain Match -6.02 dB.
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

const hypha::observatory::View* findView (juce::Component& parent)
{
    if (auto* view = dynamic_cast<const hypha::observatory::View*> (&parent)) return view;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* view = findView (*parent.getChildComponent (i))) return view;
    return nullptr;
}

struct Clock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        if (playing)
        {
            info.setTimeInSamples (position);
            info.setTimeInSeconds (static_cast<double> (position) / 48000.0);
        }
        return info;
    }
    std::int64_t position = 0;
    bool playing = false;
};

class PinContract final : private juce::Timer
{
public:
    explicit PinContract (std::vector<float> signalIn) : signal (std::move (signalIn))
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto instance = std::make_unique<Processor> (role);
            auto layout = instance->getBusesLayout();
            layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
            require (instance->setBusesLayout (layout), "host negotiates stereo");
            instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
            instance->setPlayHead (&clock);
            instance->setNonRealtime (false);
            instance->prepareToPlay (48000, blockFrames);
            (role == Processor::Role::Pre ? pre : post) = std::move (instance);
        }
        editor.reset (post->createEditorIfNeeded());
        require (editor != nullptr, "real product editor opens");
        editor->setSize (900, 600);
        editor->setVisible (true);
        started = std::chrono::steady_clock::now();
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }

    ~PinContract() override
    {
        stopTimer();
        running.store (false);
        if (audio.joinable()) audio.join();
        if (editor != nullptr) post->editorBeingDeleted (editor.get());
        editor.reset();
        pre->releaseResources();
        post->releaseResources();
    }

    bool passed = false;

private:
    juce::String footer() const
    {
        const auto* view = findView (*editor);
        return view != nullptr ? view->feedback() : juce::String();
    }

    bool click (const char* id)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        if (button == nullptr || ! button->onClick || ! button->isEnabled() || ! button->isVisible())
            return false;
        button->onClick();
        return true;
    }

    void timerCallback() override
    {
        require (std::chrono::steady_clock::now() - started < std::chrono::seconds (40), "PIN round trip timed out");
        switch (stage)
        {
            case 0:
            {
                if (pre->instanceId().isEmpty()) break;
                if (! preview)
                    preview = post->createPairPreview();
                if (! demanded)
                {
                    if (! hypha::pair_preview::request (preview)) break;
                    demanded = true;
                    requestedAt = std::chrono::steady_clock::now();
                }
                KirinPairPreviewValue value {};
                if (! kirin_hypha_pair_preview_poll (preview.get(), &value)) break;
                if (! value.complete || ! value.has_single)
                {
                    if (std::chrono::steady_clock::now() - requestedAt >= std::chrono::milliseconds (1050)
                        && hypha::pair_preview::request (preview))
                        requestedAt = std::chrono::steady_clock::now();
                    break;
                }
                require (post->setPairCandidate (pre->instanceId(), {}), "the discovered PRE is selected");
                preview.reset();
                ++stage;
                break;
            }
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED) break;
                play.store (true);
                ++stage;
                break;
            case 2:
                if (! post->isPlaying() || ! post->heartbeatLive() || ! click ("observatory-live-compare")) break;
                require (post->liveCompareStatus().active, "LISTEN starts a live session");
                listenedAt = std::chrono::steady_clock::now();
                ++stage;
                break;
            case 3:
                if (std::chrono::steady_clock::now() - listenedAt < std::chrono::milliseconds (4600)
                    || post->liveCompareStatus().verdict != hypha::live_compare::Verdict::accepted)
                    break;
                if (! click ("observatory-live-pin")) break;
                if (post->liveCompareStatus().active)
                {
                    // A scheduling stall of the test machine longer than the callback-gap rule
                    // allows starts a new PRE run, and PIN rightly refuses a window across it
                    // (INV-LC15). Say why and try again once four seconds of one run have passed.
                    std::cout << "PIN refused, retrying: " << footer() << std::endl;
                    require (++pinAttempts < 6, "PIN ends the live session");
                    listenedAt = std::chrono::steady_clock::now();
                    break;
                }
                ++stage;
                break;
            case 4:
            {
                const auto view = post->localBlindProductView();
                require (view.phase != Phase::failed, "Blind prepares the pinned pair");
                if (view.phase != Phase::ready) break;
                std::cout << "pinned frames=" << view.frames << " channels=" << view.channels
                          << " start=" << view.start << " gain_db=" << view.fixedPreGainDb << std::endl;
                require (view.frames == 192000 && view.channels == 2, "an exact four-second stereo pair");
                require (std::abs (view.fixedPreGainDb + 6.0206) < 0.002, "Gain Match measures -6.02 dB");
                require (view.start >= 0 && view.start + view.frames <= clock.position + blockFrames * 2,
                         "the pinned range is audio that already played");
                auto* screen = find (*editor, "local-blind-screen");
                require (screen != nullptr && screen->isVisible(), "Blind opens on the pinned pair");
                std::cout << "Live PIN product: PASS (real C ABI, pair discovery, live ring, PIN to Blind ready)\n";
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            }
            default:
                break;
        }
    }

    void processAudio()
    {
        juce::AudioBuffer<float> buffer (2, blockFrames);
        juce::MidiBuffer midi;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            clock.playing = play.load();
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, signal[static_cast<std::size_t> (clock.position + f) % signal.size()]);
            pre->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, buffer.getSample (c, f) * -0.5f);
            post->processBlock (buffer, midi);
            if (clock.playing) clock.position += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    // A host block of 8192 frames (171 ms): the callback-gap rule then tolerates test-machine stalls
    // up to 427 ms, as a DAW's real-time thread never needs. PIN needs four seconds of one run; with
    // 4096 frames a loaded macOS runner refused six windows in a row (2026-09-28).
    static constexpr int blockFrames = 8192;
    Clock clock;
    std::vector<float> signal;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false };
    std::chrono::steady_clock::time_point started, listenedAt, requestedAt;
    hypha::pair_preview::Ticket preview;
    int stage = 0, pinAttempts = 0;
    bool demanded = false;
};
}

int main (int argc, char** argv)
{
    require (argc == 2, "usage: live PIN product test S-1.wav");
    auto signal = readFixture (argv[1]);
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    PinContract contract (std::move (signal));
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
