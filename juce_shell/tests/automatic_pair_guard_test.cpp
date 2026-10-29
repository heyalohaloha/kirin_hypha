#include "../src/PluginProcessor.h"
#include "TestProcessorStorage.h"
#include "ReferenceValidationStorageSandbox.h"
#include "EditorProductChecks.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace hypha::tests
{
struct PlayingField { using Type = std::atomic<bool> KirinHyphaProcessorBase::*; friend Type fixturePlaying (PlayingField); };
template <class Tag, typename Tag::Type Member> struct PlayingAccess
{ friend typename Tag::Type fixturePlaying (Tag) { return Member; } };
template struct PlayingAccess<PlayingField, &KirinHyphaProcessorBase::lastPlaying>;
struct LiveField { using Type = hypha::live_compare::ProcessorState KirinHyphaProcessorBase::*; friend Type fixtureLive (LiveField); };
template <class Tag, typename Tag::Type Member> struct LiveAccess
{ friend typename Tag::Type fixtureLive (Tag) { return Member; } };
template struct LiveAccess<LiveField, &KirinHyphaProcessorBase::liveCompare>;
struct BlindField { using Type = hypha::local_blind::LocalBlindProductSession KirinHyphaProcessorBase::*; friend Type fixtureBlind (BlindField); };
template <class Tag, typename Tag::Type Member> struct BlindAccess
{ friend typename Tag::Type fixtureBlind (Tag) { return Member; } };
template struct BlindAccess<BlindField, &KirinHyphaProcessorBase::localBlindProductSession>;
struct MenuField { using Type = void (KirinHyphaEditor::*) (int, const juce::Array<KirinHyphaProcessorBase::PreCandidate>&); friend Type fixtureMenu (MenuField); };
template <class Tag, typename Tag::Type Member> struct MenuAccess
{ friend typename Tag::Type fixtureMenu (Tag) { return Member; } };
template struct MenuAccess<MenuField, &KirinHyphaEditor::handleCandidateMenu>;

}
namespace
{
using Processor = KirinHyphaProcessorBase;
using Clock = std::chrono::steady_clock;

class GuardTest : private juce::Timer
{
public:
    explicit GuardTest (std::string modeIn) : mode (std::move (modeIn))
    {
        first = make (Processor::Role::Pre);
        if (mode == "multiple" || mode == "manual" || mode == "restore") second = make (Processor::Role::Pre);
        startTimer (50);
    }
    ~GuardTest() override
    {
        stopTimer();
        editor.reset();
        for (auto* p : { first.get(), second.get(), post.get(), incumbent.get() })
            if (p != nullptr) p->releaseResources();
    }
    bool passed = false;
private:
    static std::unique_ptr<Processor> make (Processor::Role role, const juce::MemoryBlock* saved = nullptr,
                                            bool playing = false)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        auto p = std::make_unique<Processor> (role);
        if (saved) p->setStateInformation (saved->getData(), static_cast<int> (saved->getSize()));
        if (playing) (p.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (true);
        hypha::tests::prepareFixtureProcessor (*p, 48000, 512);
        return p;
    }
    void openEditor (Processor& processor)
    {
        editor.reset (processor.createEditor());
        editor->addToDesktop (0);
        editor->setVisible (true);
    }
    void finish (bool ok, const char* reason)
    {
        passed = ok; std::cout << (ok ? "PASS: " : "FAIL: ") << mode << " " << reason << std::endl;
        stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop();
    }
    void timerCallback() override
    {
        const auto now = Clock::now();
        if (now - started > std::chrono::seconds (30)) return finish (false, "fixture timeout");
        if (! hypha::tests::fixtureStorageReady (*first)
            || (second && ! hypha::tests::fixtureStorageReady (*second))) return;
        if (! post && stage == 0)
        {
            if (now - started < std::chrono::seconds (3)) return;
            if (mode == "owned")
            {
                incumbent = make (Processor::Role::Post);
                openEditor (*incumbent);
                stage = 1; return;
            }
            post = make (Processor::Role::Post, nullptr, mode == "playing" || mode == "listen" || mode == "stale");
            if (mode == "blind" && ! (post.get()->*fixtureBlind (hypha::tests::BlindField {})).beginCapture (
                    1, 1, hypha::local_blind::GainMatchPolicy::alignedActiveBlocksV1))
                return finish (false, "could not enter blind capture");
            if (mode == "disconnect")
            {
                post->clearPairCandidate();
                post->getStateInformation (saved);
                const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
                if (! xml || ! xml->getBoolAttribute ("pair_explicitly_disconnected", false))
                    return finish (false, "disconnect intent not saved");
                post.reset(); post = make (Processor::Role::Post, &saved);
            }
            if (mode != "manual" && mode != "restore" && mode != "stale" && mode != "late") openEditor (*post);
            stage = 2;
        }
        if (stage == 1)
        {
            if (! hypha::tests::fixtureStorageReady (*incumbent)) return;
            if (incumbent->pairedPreInstanceId() != first->instanceId()) return;
            bool published = false;
            for (const auto& claim : incumbent->enumeratePostPairClaims())
                if (claim.hasPairedPreInstanceId && claim.pairedPreInstanceId == first->instanceId()) published = true;
            if (! published) return;
            editor.reset();
            post = make (Processor::Role::Post); openEditor (*post); stage = 2;
        }
        if (! post || ! hypha::tests::fixtureStorageReady (*post)) return;
        if (stage == 2 && mode == "menu_disconnect")
        {
            if (post->pairedPreInstanceId() != first->instanceId()) return;
            if (! observed) { observed = true; observedAt = now; return; }
            if (now - observedAt < std::chrono::seconds (4)) return;
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            if (! view) return finish (false, "editor unavailable");
            (view->*fixtureMenu (hypha::tests::MenuField {})) (11, {});
            post->getStateInformation (saved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            if (! xml || ! xml->getBoolAttribute ("pair_explicitly_disconnected", false))
                return finish (false, "menu unpair was not saved");
            editor.reset(); post->releaseResources(); post.reset();
            post = make (Processor::Role::Post, &saved); openEditor (*post);
            observedAt = now; stage = 7; return;
        }
        if (stage == 7 && mode == "menu_disconnect")
        {
            if (post->pairedPreInstanceId().isNotEmpty()) return finish (false, "menu unpair reconnected");
            if (now - observedAt < std::chrono::seconds (4)) return;
            return finish (true, "menu unpair survives reopening");
        }
        if (stage == 2 && mode == "late")
        {
            if (saved.getSize() == 0)
            {
                post->getStateInformation (saved);
                const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
                if (! xml || xml->getStringAttribute ("instance_id").isEmpty())
                    return finish (false, "saved instance identity missing");
                xml->setAttribute ("pair_explicitly_disconnected", true);
                xml->setAttribute ("pair_pre_name", "");
                xml->setAttribute ("paired_pre_instance_id", "");
                xml->setAttribute ("paired_pre_project_hash", "");
                juce::AudioProcessor::copyXmlToBinary (*xml, saved);
                openEditor (*post);
                return;
            }
            if (post->pairedPreInstanceId() != first->instanceId()) return;
            post->setStateInformation (saved.getData(), (int) saved.getSize());
            observedAt = now; stage = 6; return;
        }
        if (stage == 6 && mode == "late")
        {
            if (now - observedAt < std::chrono::seconds (4)) return;
            juce::MemoryBlock restored; post->getStateInformation (restored);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (restored.getData(), (int) restored.getSize());
            return finish (xml && xml->getBoolAttribute ("pair_explicitly_disconnected", false)
                           && post->pairedPreInstanceId().isEmpty(), "late disconnected state wins");
        }
        if (stage == 2 && mode == "stale")
        {
            if (! preview) preview = post->createPairPreview();
            if (now - requestedAt > std::chrono::milliseconds (1100)
                && hypha::pair_preview::request (preview)) requestedAt = now;
            KirinPairPreviewValue value {};
            if (! preview || ! kirin_hypha_pair_preview_poll (preview.get(), &value)
                || ! value.complete || ! value.has_single) return;
            lastPreview = value;
            second = make (Processor::Role::Pre); stage = 4; return;
        }
        if (stage == 4 && mode == "stale")
        {
            if (! second || ! hypha::tests::fixtureStorageReady (*second)
                || post->enumeratePreCandidates().size() < 2) return;
            KirinPairPreviewValue current {};
            if (! kirin_hypha_pair_preview_poll (preview.get(), &current) || ! current.complete
                || ! current.has_single || current.generation != lastPreview.generation)
                return finish (false, "stale fixture expired before candidate recount");
            (post.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (false);
            if (! post->automaticPairNeedsService())
                return finish (false, "stale fixture has another admission blocker");
            if (post->tryAutomaticPair (preview.get(), lastPreview))
                return finish (false, "stale single candidate accepted");
            observedAt = now; stage = 5; return;
        }
        if (stage == 5 && mode == "stale")
        {
            if (now - observedAt < std::chrono::seconds (4)) return;
            return finish (post->pairedPreInstanceId().isEmpty(), "stale preview discarded after playback");
        }
        if (stage == 2 && mode == "record")
        {
            if (post->pairedPreInstanceId() != first->instanceId()) return;
            if (! post->keepPair()) return;
            stage = 3;
        }
        if (stage == 2 && (mode == "manual" || mode == "restore"))
        {
            if (post->enumeratePreCandidates().size() < 2) return;
            if (! post->setPairCandidate (first->instanceId(), {})) return;
            if (mode == "restore")
            {
                post->getStateInformation (saved); post.reset();
                post = make (Processor::Role::Post, &saved);
                openEditor (*post);
                stage = 3; return;
            }
            else openEditor (*post);
            stage = 3;
        }
        if (stage == 2 && mode != "manual" && mode != "restore" && mode != "record" && mode != "menu_disconnect") stage = 3;
        if (stage == 3)
        {
            if (! observed)
            {
                if (post->enumeratePreCandidates().size() < (second ? 2 : 1)) return;
                if ((mode == "manual" || mode == "restore" || mode == "record") && post->pairedPreInstanceId() != first->instanceId()) return;
                if (mode == "record" && ! post->isRecording() && post->keepPhase() == KIRIN_KEEP_PHASE_IDLE) return;
                if (mode != "manual" && mode != "restore" && mode != "record")
                {
                    if (! preview) preview = post->createPairPreview();
                    if (now - requestedAt > std::chrono::milliseconds (1100)
                        && hypha::pair_preview::request (preview)) requestedAt = now;
                    KirinPairPreviewValue value {};
                    if (! preview || ! kirin_hypha_pair_preview_poll (preview.get(), &value) || ! value.complete) return;
                    const bool expectSingle = mode == "playing" || mode == "disconnect" || mode == "listen" || mode == "blind";
                    if ((value.has_single != 0) != expectSingle) return;
                }
                observed = true; observedAt = now;
            }
            if (mode == "listen")
            {
                auto& live = post.get()->*fixtureLive (hypha::tests::LiveField {});
                if (! live.authority.arm (live.authority.ticket())) return;
                live.sessionActive.store (true);
                auto& playing = post.get()->*fixturePlaying (hypha::tests::PlayingField {});
                playing.store (false);
                if (! hypha::output_owner::has (post->outputStates(), hypha::output_owner::State::liveSession))
                    return finish (false, "LISTEN state unavailable");
                KirinPairPreviewValue current {};
                if (preview && kirin_hypha_pair_preview_poll (preview.get(), &current)
                    && post->tryAutomaticPair (preview.get(), current))
                    return finish (false, "paired during LISTEN");
                playing.store (true);
            }
            if (now - observedAt < std::chrono::seconds (4)) return;
            const auto id = post->pairedPreInstanceId();
            const auto states = post->outputStates();
            if (mode == "blind" && ! hypha::output_owner::has (states, hypha::output_owner::State::localBlind))
                return finish (false, "BLIND state did not persist");
            if (mode == "manual" || mode == "restore" || mode == "record")
            { if (id != first->instanceId()) return finish (false, "selected exact pair changed"); }
            else if (id.isNotEmpty()) return finish (false, "unexpected automatic pair");
            if ((mode != "record" && post->isRecording()) || post->getLatencySamples() != 0
                || (mode != "listen" && post->liveCompareStatus().active))
                return finish (false, "pairing started output or record");
            if (mode == "record") post->stopPair();
            finish (true, "guard preserved");
        }
    }
    std::string mode;
    std::unique_ptr<Processor> first, second, post, incumbent;
    juce::MemoryBlock saved;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    KirinPairPreviewValue lastPreview {};
    hypha::pair_preview::Ticket preview;
    Clock::time_point started = Clock::now(), observedAt, requestedAt;
    int stage = 0;
    bool observed = false;
};
}
int main (int argc, char** argv)
{
    if (argc != 2) return 2;
    ReferenceValidationStorageSandbox sandbox;
    if (std::string (argv[1]) == "record")
    {
        const auto identity = sandbox.directory().getChildFile ("home/AppData/Roaming/Kirin OS/identity.json");
        if (! identity.isAChildOf (sandbox.directory()) || identity.getParentDirectory().createDirectory().failed()
            || ! identity.appendText (R"({"schema_version":"1.0","installation_id":"test-install","hardware_id":"test-hardware","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"test-signature","license":"os","created_at":"2026-01-01T00:00:00Z","last_verified_at":"2026-01-01T00:00:00Z"})")) return 2;
    }
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI init;
    hypha::tests::editor_product::ScopedChainTimingStorage chainTimingStorage (sandbox.directory());
    GuardTest test (argv[1]);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return test.passed ? 0 : 1;
}
