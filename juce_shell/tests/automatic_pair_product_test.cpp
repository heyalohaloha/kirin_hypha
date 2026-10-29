#include "../src/PluginProcessor.h"
#include "../src/HyphaFeedbackStrip.h"
#include "TestProcessorStorage.h"
#include "ReferenceValidationStorageSandbox.h"
#include "EditorProductChecks.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <cstring>

namespace
{
class AutomaticPairTest : private juce::Timer
{
public:
    AutomaticPairTest()
    {
        for (auto role : { KirinHyphaProcessorBase::Role::Pre, KirinHyphaProcessorBase::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            auto processor = std::make_unique<KirinHyphaProcessorBase> (role);
            hypha::tests::prepareFixtureProcessor (*processor, 48000, 512);
            (role == KirinHyphaProcessorBase::Role::Pre ? pre : post) = std::move (processor);
        }
        startTimer (50);
    }
    ~AutomaticPairTest() override { stopTimer(); editor.reset(); pre->releaseResources(); post->releaseResources(); }
    bool passed = false;
private:
    void finish (const char* reason)
    {
        std::cout << reason << std::endl;
        stopTimer();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }
    void timerCallback() override
    {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds (35)) return finish ("FAIL: fixture did not produce a single PRE");
        if (! hypha::tests::fixtureStorageReady (*pre) || ! hypha::tests::fixtureStorageReady (*post)) return;
        if (! seen && ! preview) preview = post->createPairPreview();
        if (! seen && (! demanded || std::chrono::steady_clock::now() - requested > std::chrono::milliseconds (1100))
            && hypha::pair_preview::request (preview))
        { demanded = true; requested = std::chrono::steady_clock::now(); }
        KirinPairPreviewValue value {};
        if (! seen && preview && kirin_hypha_pair_preview_poll (preview.get(), &value)
            && value.complete && value.has_single)
        {
            seen = true;
            found = std::chrono::steady_clock::now();
            std::cout << "fixture: exactly one available PRE; editor closed" << std::endl;
        }
        if (! seen) return;
        preview.reset();
        if (! editor)
        {
            if (post->pairedPreInstanceId() == pre->instanceId())
                return finish ("FAIL: paired while editor is closed");
            if (std::chrono::steady_clock::now() - found < std::chrono::seconds (4)) return;
            editor.reset (post->createEditor());
            editor->addToDesktop (0);
            editor->setVisible (true);
            found = std::chrono::steady_clock::now();
            return;
        }
        if (stage == 1)
        {
            if (post->pairedPreInstanceId().isNotEmpty()) return;
            juce::MemoryBlock saved;
            post->getStateInformation (saved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            if (! xml || ! xml->getBoolAttribute ("pair_explicitly_disconnected", false))
                return finish ("FAIL: unpair intent not saved");
            editor.reset();
            post->releaseResources(); post.reset();
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
            post = std::make_unique<KirinHyphaProcessorBase> (KirinHyphaProcessorBase::Role::Post);
            post->setStateInformation (saved.getData(), (int) saved.getSize());
            hypha::tests::prepareFixtureProcessor (*post, 48000, 512);
            editor.reset (post->createEditor()); editor->addToDesktop (0); editor->setVisible (true);
            found = std::chrono::steady_clock::now(); stage = 2;
            return;
        }
        if (stage == 2)
        {
            if (post->pairedPreInstanceId().isNotEmpty()) return finish ("FAIL: unpaired POST reconnected");
            if (std::chrono::steady_clock::now() - found < std::chrono::seconds (4)) return;
            passed = true;
            return finish ("PASS: editor-only pairing, notice action, persistent unpair");
        }
        if (post->pairedPreInstanceId() == pre->instanceId())
        {
            auto findFeedback = [] (auto&& self, juce::Component& parent) -> hypha::FeedbackStrip*
            {
                if (auto* strip = dynamic_cast<hypha::FeedbackStrip*> (&parent)) return strip;
                for (auto* child : parent.getChildren())
                    if (auto* result = self (self, *child)) return result;
                return nullptr;
            };
            const auto* strip = findFeedback (findFeedback, *editor);
            if (! strip || ! strip->isVisible() || ! strip->text().startsWith ("Paired with PRE  "))
                return finish ("FAIL: automatic pairing notice missing");
            const auto outputStates = post->outputStates();
            std::cout << "pair output states: " << outputStates << ", record: " << post->isRecording()
                      << ", latency: " << post->getLatencySamples()
                      << ", live: " << post->liveCompareStatus().active << std::endl;
            // The ordinary exact-selection path revokes old authority and briefly reports
            // cleanup pending; it does not start or own an audible comparison.
            const auto cleanup = hypha::output_owner::bit (hypha::output_owner::State::liveRestoring);
            if (post->isRecording() || post->getLatencySamples() != 0 || post->liveCompareStatus().active
                || (outputStates & ~cleanup) != 0)
                return finish ("FAIL: automatic pair started record or listening");
            juce::AudioBuffer<float> audio (2, 512), original (2, 512);
            for (int channel = 0; channel < 2; ++channel)
            {
                for (int sample = 0; sample < 512; ++sample)
                    audio.setSample (channel, sample, (sample - 256) * 0.001f);
                original.copyFrom (channel, 0, audio, channel, 0, 512);
            }
            juce::MidiBuffer midi;
            post->processBlock (audio, midi);
            for (int channel = 0; channel < 2; ++channel)
                if (std::memcmp (audio.getReadPointer (channel), original.getReadPointer (channel),
                                 512 * sizeof (float)) != 0)
                    return finish ("FAIL: automatic pair changed A audio");
            const bool noticeTranslated = hypha::i18n::translate (strip->text(), hypha::i18n::Language::japanese) != strip->text();
            const bool actionTranslated = hypha::i18n::translate ("Unpair", hypha::i18n::Language::japanese) != "Unpair";
            std::cout << "notice translation: " << noticeTranslated << ", action translation: " << actionTranslated << std::endl;
            if (! noticeTranslated || ! actionTranslated) return finish ("FAIL: pairing notice translation missing");
            auto* button = dynamic_cast<juce::TextButton*> (strip->findChildWithID ("feedback-unpair"));
            if (! button || ! button->isVisible() || button->getWidth() < 40 || button->getHeight() < 12)
                return finish ("FAIL: unpair button missing or too small");
            button->triggerClick(); stage = 1; found = std::chrono::steady_clock::now();
            return;
        }
        if (std::chrono::steady_clock::now() - found > std::chrono::seconds (12))
            return finish ("FAIL: no automatic pair with visible editor");
    }
    std::unique_ptr<KirinHyphaProcessorBase> pre, post;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    hypha::pair_preview::Ticket preview;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), found, requested;
    bool demanded = false, seen = false;
    int stage = 0;
};
}

int main()
{
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI init;
    hypha::tests::editor_product::ScopedChainTimingStorage chainTimingStorage (sandbox.directory());
    AutomaticPairTest test;
    juce::MessageManager::getInstance()->runDispatchLoop();
    return test.passed ? 0 : 1;
}
