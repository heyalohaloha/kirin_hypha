#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"
#include "FooterNoticeContractChecks.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Live offset product: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// INV-LC7 / LC10 end to end: the real PRE and POST processors, the Rust C ABI, pair discovery, the
// shared live ring and the product editor. Between PRE and POST sits a delay the host does not know
// about, 2000 frames (41.67 ms at 48 kHz), as from a plug-in that under-reports its latency. A few
// seconds after LISTEN the footer reads the offset. The delay then grows to 3000 frames with the
// clocks unchanged: POST is held until playback stops, and the footer reads the new offset. With
// the host's delay compensation off (INV-LC8) PRE waits and the footer says why; back on, PRE returns.
namespace
{
using Processor = KirinHyphaProcessorBase;

juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* child = find (*parent.getChildComponent (i), id)) return child;
    return nullptr;
}

hypha::observatory::View* findView (juce::Component& parent)
{
    if (auto* view = dynamic_cast<hypha::observatory::View*> (&parent)) return view;
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

// Broadband noise, independent per channel, like a mix: the estimate needs one clear peak.
float noise (std::int64_t index, int channel)
{
    auto z = static_cast<std::uint64_t> (index * 2 + channel) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
}

class OffsetContract final : private juce::Timer
{
public:
    OffsetContract()
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

    ~OffsetContract() override
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
    bool click (const char* id)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        if (button == nullptr || ! button->onClick || ! button->isEnabled() || ! button->isVisible())
            return false;
        button->onClick();
        return true;
    }

    juce::String footer() const
    {
        const auto* view = findView (*editor);
        return view != nullptr ? view->feedback() : juce::String();
    }

    void timerCallback() override
    {
        if (std::chrono::steady_clock::now() - started >= std::chrono::seconds (60))
        {
            // Where the round trip stood, for a slow test machine to explain itself.
            const auto status = post->liveCompareStatus();
            std::cerr << "Live offset product state: stage " << stage << ", footer \"" << footer()
                      << "\", pair " << static_cast<int> (post->pairStatus()) << ", active " << status.active
                      << ", matched " << status.matched << ", PRE selected " << status.preSelected
                      << ", PRE audible " << status.preAudible << ", PRE waiting " << status.preWaiting
                      << ", held " << status.contentHeld << ", reason " << static_cast<int> (status.reason) << "/"
                      << static_cast<int> (status.observation) << ", longest callback interval "
                      << longestCallbackMicros.load() / 1000.0 << " ms\n";
            require (false, "offset round trip timed out");
        }
        // A stall of this machine past the callback-gap rule (2.5 blocks, 213 ms) rightly stops PRE:
        // "Audio gap: select PRE again". While the offset settles, select PRE again as a user would.
        // A gap reported without such a stall is a product fault and fails here.
        if (stage == 3)
            if (const auto status = post->liveCompareStatus(); status.active && status.interrupted
                && status.reason == hypha::live_compare::RecoveryReason::callbackGap && ! status.preSelected)
            {
                require (stalls.load() > recoveredStalls, "a callback gap is reported only after a real stall");
                recoveredStalls = stalls.load();
                std::cout << "recovered from a test-machine stall of " << longestCallbackMicros.load() / 1000.0
                          << " ms" << std::endl;
                click ("observatory-live-pre");
                return;
            }
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
                ++stage;
                break;
            case 3:
                // Two agreeing estimates two seconds apart settle the offset (INV-LC7).
                if (footer() != "PRE 41.67 ms early") break;
                {
                    const auto measured = post->measureLiveCompare();
                    if (! measured.ok()) break;
                    const auto plan = hypha::live_compare::planMatch (measured, 0.0);
                    require (post->applyLiveCompareMatch (plan, hypha::live_compare::MatchChoice::basis),
                             "named comparison starts with a valid MATCH before the fault");
                    matchedGain = post->liveCompareStatus().gain;
                }
                std::cout << "settled: " << footer() << std::endl;
                require (! post->liveCompareStatus().contentHeld, "the first settled offset is the baseline");
                delayFrames.store (3000);
                ++stage;
                break;
            case 4:
            {
                // The latency changed with the clocks unchanged: POST is held (INV-LC10).
                heldNoticeSeen = heldNoticeSeen || footer() == "Timing changed: stop/play DAW (POST)";
                if (! post->liveCompareStatus().contentHeld || ! heldNoticeSeen) break;
                if (heldAt == std::chrono::steady_clock::time_point()) heldAt = std::chrono::steady_clock::now();
                if (std::chrono::steady_clock::now() - heldAt < std::chrono::milliseconds (3500)) break;
                require (footer() == "Timing changed: stop/play DAW (POST)", "recovery persists beyond the toast timeout");
                // The exact settled measurement that triggered the hold is retained. Re-reading
                // a moving RT history here can be undetermined and is not the jump evidence.
                require (post->liveCompareStatus().contentJumpLagFrames == -3000,
                         "the 62.50 ms observation remains measured under the recovery notice");
                std::cout << "held: " << footer() << std::endl;
                play.store (false);
                ++stage;
                break;
            }
            case 5:
                // Stopping ends the hold; the session stays for the next run. One audio callback
                // clears the hold before it revokes MATCH, so wait until the stop is fully observed.
                if (post->liveCompareStatus().contentHeld || post->liveCompareStatus().matched) break;
                require (post->liveCompareStatus().contentJumpLagFrames == 0,
                         "the prior run's jump evidence does not leak into a new run");
                require (post->liveCompareStatus().active, "the session survives the stop");
                require (post->liveCompareStatus().matchHeld && ! post->liveCompareStatus().matched
                    && std::abs (post->liveCompareStatus().gain - matchedGain) <= 0.0f,
                    "named comparison retains the gain as HELD across stop/play");
                play.store (true);
                ++stage;
                break;
            case 6:
                // PRE plays again in the new run.
                if (! post->liveCompareStatus().preSelected && ! click ("observatory-live-pre")) break;
                if (! post->liveCompareStatus().preAudible) break;
                // At 600 px recovery stays at LIVE/HOLD, with bounded paint and full details.
                // Safety controls and the actual audio refusal keep their original behavior.
                editor->setSize (600, 400);
                post->kirinHostDelayCompensationStateChanged (false);
                ++stage;
                break;
            case 7:
            {
                // INV-LC8: with the host's delay compensation off, POST sounds, PRE waits and the
                // status line says why; the offset monitor claims no jump.
                const auto status = post->liveCompareStatus();
                if (! status.preWaiting || status.preAudible || footer() != "Compensation off: enable it (POST)") break;
                require (! status.contentHeld, "switching compensation off is not a latency jump");
                auto* view = findView (*editor);
                require (view != nullptr && ! find (*editor, "feedback-strip")->isVisible()
                    && hypha::tests::footer_notice::retainedInFooter (*view, footer()),
                         "actual refusal keeps full details and bounded paint at the fixed footer");
                std::cout << "compensation off: " << footer() << std::endl;
                post->kirinHostDelayCompensationStateChanged (true);
                ++stage;
                break;
            }
            case 8:
            {
                // Back on: the correspondence is proven again and PRE returns by itself.
                const auto status = post->liveCompareStatus();
                if (! status.preAudible || status.preWaiting) break;
                require (footer() != "Compensation off: enable it (POST)", "the reason clears once it is on");
                std::cout << "Live offset product: PASS (real C ABI, pair discovery, live ring, footer warning, hold, "
                             "delay compensation off)\n";
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
        constexpr std::size_t lineFrames = 4096;
        std::vector<float> line (lineFrames * 2, 0.0f);
        std::int64_t written = 0;
        auto next = std::chrono::steady_clock::now();
        auto previousPre = std::chrono::steady_clock::time_point();
        auto previousPost = std::chrono::steady_clock::time_point();
        // Each side's gap is measured where that side's callback starts, before the call, so a stall
        // inside PRE is counted before POST can report it.
        const auto countStall = [this] (std::chrono::steady_clock::time_point& previous) {
            const auto callbackAt = std::chrono::steady_clock::now();
            if (previous != std::chrono::steady_clock::time_point())
            {
                const auto interval = static_cast<std::int64_t> (
                    std::chrono::duration_cast<std::chrono::microseconds> (callbackAt - previous).count());
                longestCallbackMicros.store (std::max (longestCallbackMicros.load(), interval));
                // The product's gap rule: longer than 2.5 times the previous block.
                if (interval * 48000 > static_cast<std::int64_t> (blockFrames) * 2'500'000) stalls.fetch_add (1);
            }
            previous = callbackAt;
        };
        while (running.load())
        {
            clock.playing = play.load();
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    buffer.setSample (c, f, noise (clock.position + f, c));
            countStall (previousPre);
            pre->processBlock (buffer, midi);
            // The unreported delay between PRE and POST.
            const auto delay = delayFrames.load();
            for (int f = 0; f < blockFrames; ++f, ++written)
                for (int c = 0; c < 2; ++c)
                {
                    line[static_cast<std::size_t> (written) % lineFrames * 2 + static_cast<std::size_t> (c)] = buffer.getSample (c, f);
                    const auto from = written - delay;
                    buffer.setSample (c, f, from < 0 ? 0.0f
                        : line[static_cast<std::size_t> (from) % lineFrames * 2 + static_cast<std::size_t> (c)]);
                }
            countStall (previousPost);
            post->processBlock (buffer, midi);
            if (clock.playing) clock.position += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    // A host block of 4096 frames (85 ms): the callback-gap rule then tolerates test-machine stalls
    // up to 213 ms, as a DAW's real-time thread never needs.
    static constexpr int blockFrames = 4096;
    Clock clock;
    std::unique_ptr<Processor> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false };
    std::atomic<std::int64_t> longestCallbackMicros { 0 };
    std::atomic<int> stalls { 0 };
    int recoveredStalls = 0;
    std::atomic<std::int64_t> delayFrames { 2000 };
    std::chrono::steady_clock::time_point started, requestedAt, heldAt;
    hypha::pair_preview::Ticket preview;
    int stage = 0;
    float matchedGain = 1.0f;
    bool demanded = false, heldNoticeSeen = false;
};
}

int main()
{
    ValidationStorageSandbox sandbox;
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    OffsetContract contract;
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
