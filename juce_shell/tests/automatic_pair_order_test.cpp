#include "../src/PluginProcessor.h"
#include "ReferenceValidationStorageSandbox.h"
#include "EditorProductChecks.h"
#include "TestProcessorStorage.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace
{
using Processor = KirinHyphaProcessorBase;
using Clock = std::chrono::steady_clock;
class OrderTest final : private juce::Timer
{
public:
    explicit OrderTest (bool initiallyVisible) : visible (initiallyVisible)
    {
        post = make (Processor::Role::Post);
        if (visible) open();
        startTimer (50);
    }
    ~OrderTest() override
    {
        stopTimer(); editor.reset();
        if (pre) pre->releaseResources();
        post->releaseResources();
    }
    bool passed = false;
private:
    static std::unique_ptr<Processor> make (Processor::Role role)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        auto processor = std::make_unique<Processor> (role);
        hypha::tests::prepareFixtureProcessor (*processor, 48000, 512);
        return processor;
    }
    void open()
    {
        editor.reset (post->createEditor());
        editor->addToDesktop (0);
        editor->setVisible (true);
    }
    void finish (bool ok, const char* message)
    {
        passed = ok;
        std::cout << (ok ? "PASS: " : "FAIL: ") << message << std::endl;
        stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop();
    }
    void timerCallback() override
    {
        const auto now = Clock::now();
        if (now - started > std::chrono::seconds (35)) return finish (false, "POST-first timeout");
        if (! hypha::tests::fixtureStorageReady (*post)) return;
        if (! pre)
        {
            if (! preview) preview = post->createPairPreview();
            if (now - requested > std::chrono::milliseconds (1100) && hypha::pair_preview::request (preview))
                requested = now;
            KirinPairPreviewValue value {};
            if (! preview || ! kirin_hypha_pair_preview_poll (preview.get(), &value) || ! value.complete || value.has_single) return;
            if (post->pairedPreInstanceId().isNotEmpty()) return finish (false, "paired with no PRE");
            pre = make (Processor::Role::Pre);
            return;
        }
        if (! hypha::tests::fixtureStorageReady (*pre)) return;
        if (! visible && ! editor)
        {
            const auto candidates = post->enumeratePreCandidates();
            if (candidates.size() != 1 || candidates[0].instanceId != pre->instanceId()) return;
            if (post->pairedPreInstanceId().isNotEmpty()) return finish (false, "paired while hidden");
            if (! seenPre) { seenPre = true; seenAt = now; }
            if (now - seenAt < std::chrono::seconds (4)) return;
            open();
        }
        if (post->pairedPreInstanceId() == pre->instanceId())
            finish (true, "POST-first pairing after editor visible");
    }
    bool visible, seenPre = false;
    std::unique_ptr<Processor> post, pre;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    hypha::pair_preview::Ticket preview;
    Clock::time_point started = Clock::now(), requested, seenAt;
};
}
int main (int argc, char** argv)
{
    if (argc != 2) return 2;
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI init;
    hypha::tests::editor_product::ScopedChainTimingStorage chainTimingStorage (sandbox.directory());
    OrderTest test (std::string (argv[1]) == "visible");
    juce::MessageManager::getInstance()->runDispatchLoop();
    return test.passed ? 0 : 1;
}
