#include "../src/PluginProcessor.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "ValidationStorageSandbox.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Live AAX group product: " << message << '\n'; std::exit (EXIT_FAILURE); }
}
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

// INV-LC9 end to end: real AAX mono processors, the Rust C ABI, pair discovery, the shared live ring
// and the product editor. Pro Tools names each instance's group before the first prepare (JUCE
// patch 0010). The PRE and POST of a mono track are the only instances of their groups: LISTEN
// starts and PRE plays. A PRE that is one channel of a multi-mono set is refused with the reason,
// and once a second channel joins POST's group, the entry is gone.
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

float noise (std::int64_t index)
{
    auto z = static_cast<std::uint64_t> (index) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
}

class GroupContract final : private juce::Timer
{
public:
    GroupContract()
    {
        preTrack = make (Processor::Role::Pre, 11);
        preLeft = make (Processor::Role::Pre, 22);
        preRight = make (Processor::Role::Pre, 22);
        post = make (Processor::Role::Post, 33);
        editor.reset (post->createEditorIfNeeded());
        require (editor != nullptr, "real product editor opens");
        editor->setSize (900, 600);
        editor->setVisible (true);
        started = std::chrono::steady_clock::now();
        audio = std::thread ([this] { processAudio(); });
        startTimer (20);
    }

    ~GroupContract() override
    {
        stopTimer();
        running.store (false);
        if (audio.joinable()) audio.join();
        if (editor != nullptr) post->editorBeingDeleted (editor.get());
        editor.reset();
        for (auto* p : { preTrack.get(), preLeft.get(), preRight.get(), post.get() })
            p->releaseResources();
    }

    bool passed = false;

private:
    std::unique_ptr<Processor> make (Processor::Role role, juce::uint64 group)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
        auto instance = std::make_unique<Processor> (role);
        auto layout = instance->getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::mono());
        layout.outputBuses.set (0, juce::AudioChannelSet::mono());
        require (instance->setBusesLayout (layout), "host negotiates mono");
        instance->setMeterContextPreference (hypha::meter_context::MeterContext::twoMix, false);
        instance->setPlayHead (&clock);
        instance->setNonRealtime (false);
        instance->kirinHostInstanceGroup (group, true); // as the AAX wrapper does before prepare
        instance->prepareToPlay (48000, blockFrames);
        return instance;
    }

    bool click (const char* id)
    {
        auto* button = dynamic_cast<juce::Button*> (find (*editor, id));
        if (button == nullptr || ! button->onClick || ! button->isEnabled() || ! button->isVisible())
            return false;
        button->onClick();
        return true;
    }

    bool visible (const char* id)
    {
        auto* component = find (*editor, id);
        return component != nullptr && component->isVisible();
    }

    juce::String footer() const
    {
        const auto* view = findView (*editor);
        return view != nullptr ? view->feedback() : juce::String();
    }

    // Discovery lists every PRE; the test chooses one by its identity, as the pair menu does.
    bool pairWith (Processor& pre)
    {
        if (pre.instanceId().isEmpty()) return false;
        if (! preview) preview = post->createPairPreview();
        if (std::chrono::steady_clock::now() - requestedAt >= std::chrono::milliseconds (1050)
            && hypha::pair_preview::request (preview))
            requestedAt = std::chrono::steady_clock::now();
        KirinPairPreviewValue value {};
        if (! kirin_hypha_pair_preview_poll (preview.get(), &value) || ! value.complete) return false;
        return post->setPairCandidate (pre.instanceId(), {});
    }

    void timerCallback() override
    {
        require (std::chrono::steady_clock::now() - started < std::chrono::seconds (60), "group round trip timed out");
        switch (stage)
        {
            case 0:
                // The PRE of a mono track, the only instance of its group.
                if (! pairWith (*preTrack)) break;
                ++stage;
                break;
            case 1:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED || post->pairedPreInstanceId() != preTrack->instanceId()) break;
                play.store (true);
                ++stage;
                break;
            case 2:
                require (post->liveCompareSupported() && ! post->aaxMultiMonoMember(), "a mono-track POST offers LISTEN");
                if (! post->isPlaying() || ! post->heartbeatLive() || ! click ("observatory-live-compare")) break;
                require (post->liveCompareStatus().active, "LISTEN starts on a mono track");
                ++stage;
                break;
            case 3:
                if (! post->liveCompareStatus().preSelected && ! click ("observatory-live-pre")) break;
                if (! post->liveCompareStatus().preAudible) break;
                std::cout << "mono track: PRE plays" << std::endl;
                require (click ("observatory-live-end") && ! post->liveCompareStatus().active, "END closes the session");
                ++stage;
                break;
            case 4:
                // One channel of a multi-mono PRE set: its ring says so.
                require (preLeft->aaxMultiMonoMember() && preRight->aaxMultiMonoMember(), "the PRE set shares a group");
                if (! pairWith (*preLeft)) break;
                ++stage;
                break;
            case 5:
                if (post->pairStatus() != KIRIN_PAIR_STATUS_PAIRED || post->pairedPreInstanceId() != preLeft->instanceId()) break;
                if (! click ("observatory-live-compare")) break;
                require (! post->liveCompareStatus().active, "a multi-mono PRE never starts a session");
                require (footer() == "PRE is multi-mono: insert it as stereo", "the refusal says why");
                std::cout << "multi-mono PRE: " << footer() << std::endl;
                // A second channel joins POST's group: POST is now one channel of a multi-mono set.
                postRight = make (Processor::Role::Post, 33);
                ++stage;
                break;
            case 6:
                require (post->aaxMultiMonoMember() && ! post->liveCompareSupported(), "a multi-mono POST offers nothing");
                if (visible ("observatory-live-compare")) break;
                std::cout << "Live AAX group product: PASS (mono track plays PRE, multi-mono PRE refused with the "
                             "reason, multi-mono POST offers no LISTEN)\n";
                passed = true;
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
            default:
                break;
        }
    }

    void processAudio()
    {
        juce::AudioBuffer<float> buffer (1, blockFrames), spare (1, blockFrames);
        juce::MidiBuffer midi;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            clock.playing = play.load();
            for (int f = 0; f < blockFrames; ++f)
                buffer.setSample (0, f, noise (clock.position + f));
            for (auto* channel : { preLeft.get(), preRight.get() })
            {
                spare.makeCopyOf (buffer, true);
                channel->processBlock (spare, midi);
            }
            preTrack->processBlock (buffer, midi);
            post->processBlock (buffer, midi);
            if (clock.playing) clock.position += blockFrames;
            next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
            std::this_thread::sleep_until (next);
        }
    }

    // A host block of 4096 frames (85 ms), as in the other live product tests: the callback-gap rule
    // then tolerates test-machine stalls up to 213 ms.
    static constexpr int blockFrames = 4096;
    Clock clock;
    std::unique_ptr<Processor> preTrack, preLeft, preRight, post, postRight;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> running { true }, play { false };
    std::chrono::steady_clock::time_point started, requestedAt;
    hypha::pair_preview::Ticket preview;
    int stage = 0;
};
}

int main()
{
    ValidationStorageSandbox sandbox;
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    GroupContract contract;
    juce::MessageManager::getInstance()->runDispatchLoop();
    return contract.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
