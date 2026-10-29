#include "../src/PluginProcessor.h"
#include "../src/AutomaticPairNotice.h"
#include "TestProcessorStorage.h"
#include "AutomaticPairOffscreenEditor.h"
#include "ReferenceValidationStorageSandbox.h"
#include "EditorProductChecks.h"
#include <chrono>
#include <cstring>
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
struct PreviewField { using Type = KirinPairPreviewValue KirinHyphaEditor::*; friend Type fixturePreview (PreviewField); };
template <class Tag, typename Tag::Type Member> struct PreviewAccess
{ friend typename Tag::Type fixturePreview (Tag) { return Member; } };
template struct PreviewAccess<PreviewField, &KirinHyphaEditor::pairPreviewShown>;
struct NoticeField { using Type = bool KirinHyphaEditor::*; friend Type fixtureNotice (NoticeField); };
template <class Tag, typename Tag::Type Member> struct NoticeAccess
{ friend typename Tag::Type fixtureNotice (Tag) { return Member; } };
template struct NoticeAccess<NoticeField, &KirinHyphaEditor::automaticPairNotice>;
struct NameField { using Type = hypha::EditableName KirinHyphaEditor::*; friend Type fixtureName (NameField); };
template <class Tag, typename Tag::Type Member> struct NameAccess
{ friend typename Tag::Type fixtureName (Tag) { return Member; } };
template struct NameAccess<NameField, &KirinHyphaEditor::nameField>;
struct ToastField { using Type = juce::String KirinHyphaEditor::*; friend Type fixtureToast (ToastField); };
template <class Tag, typename Tag::Type Member> struct ToastAccess
{ friend typename Tag::Type fixtureToast (Tag) { return Member; } };
template struct ToastAccess<ToastField, &KirinHyphaEditor::toastText>;
struct SelectField { using Type = void (KirinHyphaEditor::*)(); friend Type fixtureSelect (SelectField); };
template <class Tag, typename Tag::Type Member> struct SelectAccess
{ friend typename Tag::Type fixtureSelect (Tag) { return Member; } };
template struct SelectAccess<SelectField, &KirinHyphaEditor::selectPairPreview>;
}
namespace
{
using Processor = KirinHyphaProcessorBase;
using Clock = std::chrono::steady_clock;
struct PlayingClock final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSamples (0);
        return info;
    }
};
class GuardTest : private juce::Timer
{
public:
    explicit GuardTest (std::string modeIn) : mode (std::move (modeIn))
    {
        first = make (Processor::Role::Pre);
        if (mode == "multiple" || mode == "manual" || mode == "restore") second = make (Processor::Role::Pre);
        if (mode == "late") probe = make (Processor::Role::Post);
        startTimer (50);
    }
    ~GuardTest() override
    {
        stopTimer();
        editor.reset();
        for (auto* p : { first.get(), second.get(), post.get(), incumbent.get(), probe.get() })
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
        hypha::tests::prepareFixtureProcessor (*p, 48000, 512);
        if (playing) (p.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (true);
        return p;
    }
    void openEditor (Processor& processor)
    {
        editor.reset (makeAutomaticPairEditor (processor));
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
        if (probe)
        {
            if (! hypha::tests::fixtureStorageReady (*probe)) return;
            const auto id = probe->instanceId(); juce::MemoryBlock state; probe->getStateInformation (state);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            if (id.isEmpty() || ! xml || xml->getStringAttribute ("instance_id") != id)
                return finish (false, "enabled instance identity missing from state");
            if (! probeRestored)
            {
                if (! probe->automaticPairNeedsService()) return;
                probe->setStateInformation (state.getData(), (int) state.getSize()); probeRestored = true; return;
            }
            if (probe->instanceId() != id) return finish (false, "enabled round trip changed identity");
            if (! probe->automaticPairNeedsService()) return;
            xml->setAttribute ("instance_id", "another-instance"); juce::AudioProcessor::copyXmlToBinary (*xml, state);
            probe->setStateInformation (state.getData(), (int) state.getSize());
            if (probe->automaticPairNeedsService()) return finish (false, "foreign state allowed automatic pairing");
            std::cout << "PASS: self state remains eligible; foreign state inhibits pairing" << std::endl;
            probe->releaseResources(); probe.reset();
        }
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
            if (mode == "playing") post->setPlayHead (&playingClock);
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
            if (mode != "manual" && mode != "restore" && mode != "stale" && mode != "late" && mode != "playing" && mode != "listen") openEditor (*post);
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
        if (stage == 2 && mode == "playing" && ! manualChecked)
        {
            if (post->enumeratePreCandidates().isEmpty()) return;
            if (! post->setPairCandidate (first->instanceId(), {}))
                return finish (false, "manual pairing failed during playback");
            const auto cleanup = hypha::output_owner::bit (hypha::output_owner::State::liveRestoring);
            if ((post->outputStates() & ~cleanup) != 0 || post->isRecording()
                || post->keepPhase() != KIRIN_KEEP_PHASE_IDLE || post->liveCompareStatus().active
                || post->liveCompareStatus().postTarget != 1.0f || post->liveCompareStatus().postActual != 1.0f)
                return finish (false, "manual pairing started output or Keep");
            juce::AudioBuffer<float> audio (2, 512), original (2, 512);
            for (int channel = 0; channel < 2; ++channel)
            {
                for (int sample = 0; sample < 512; ++sample)
                    audio.setSample (channel, sample, (sample - 256) * 0.001f);
                original.copyFrom (channel, 0, audio, channel, 0, 512);
            }
            juce::MidiBuffer midi;
            post->processBlock (audio, midi);
            if (! post->isPlaying()) return finish (false, "host playback was not active");
            for (int channel = 0; channel < 2; ++channel)
                if (std::memcmp (audio.getReadPointer (channel), original.getReadPointer (channel),
                                 512 * sizeof (float)) != 0)
                    return finish (false, "manual pairing changed POST audio");
            std::cout << "PASS: manual playback pairing preserves POST audio and output state" << std::endl;
            post->releaseResources(); post.reset();
            post = make (Processor::Role::Post, nullptr, true);
            post->setPlayHead (&playingClock);
            openEditor (*post);
            manualChecked = true;
            return;
        }
        if (stage == 2 && mode == "disconnect")
        {
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            if (! view) return finish (false, "editor unavailable");
            const auto value = view->*fixturePreview (hypha::tests::PreviewField {});
            if (! value.has_single)
            {
                if (now - started > std::chrono::seconds (12))
                    return finish (false, "restored unpaired POST did not show candidate without hover");
                return;
            }
            observedAt = now; stage = 10; return;
        }
        if (stage == 10 && mode == "disconnect")
        {
            if (now - observedAt < std::chrono::seconds (3)) return;
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            auto& name = view->*fixtureName (hypha::tests::NameField {});
            juce::Image image (juce::Image::ARGB, name.getWidth(), name.getHeight(), true);
            juce::Graphics graphics (image); name.paint (graphics);
            if (! name.paintedSelectionGeneration()) return finish (false, "candidate was not painted");
            (view->*fixtureSelect (hypha::tests::SelectField {}))();
            if ((view->*fixtureToast (hypha::tests::ToastField {})) == "PRE changed")
                return finish (false, "expired preview reported PRE changed");
            second = make (Processor::Role::Pre);
            observedAt = now; stage = 9; return;
        }
        if (stage == 9 && mode == "disconnect")
        {
            if (! hypha::tests::fixtureStorageReady (*second) || post->enumeratePreCandidates().size() < 2) return;
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            if (! (view->*fixturePreview (hypha::tests::PreviewField {})).has_single)
                return finish (post->pairedPreInstanceId().isEmpty(), "restored preview updated without pairing");
            if (now - observedAt > std::chrono::seconds (13))
                return finish (false, "restored preview did not update after PRE added");
            return;
        }
        if (stage == 2 && mode == "menu_disconnect")
        {
            if (post->pairedPreInstanceId() != first->instanceId()) return;
            if (! observed) { observed = true; observedAt = now; return; }
            if (now - observedAt < std::chrono::seconds (4)) return;
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            if (! view) return finish (false, "editor unavailable");
            (view->*fixtureNotice (hypha::tests::NoticeField {})) = true;
            (view->*fixtureMenu (hypha::tests::MenuField {})) (11, {});
            if (view->*fixtureNotice (hypha::tests::NoticeField {}))
                return finish (false, "manual unpair left automatic notice");
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
            const auto candidates = post->enumeratePreCandidates();
            if (candidates.size() < 2) return;
            if (mode == "manual") staleMenuCandidates = candidates;
            if (! post->setPairCandidate (first->instanceId(), {})) return;
            if (mode == "restore")
            {
                post->getStateInformation (saved); post.reset();
                post = make (Processor::Role::Post, &saved);
                openEditor (*post);
                stage = 3; return;
            }
            else
            {
                post->setPlayHead (&playingClock);
                openEditor (*post);
                stage = 8; return;
            }
        }
        if (stage == 8 && mode == "manual")
        {
            juce::AudioBuffer<float> audio (2, 512); audio.clear();
            juce::MidiBuffer midi; post->processBlock (audio, midi);
            if (! post->isPlaying() || ! post->heartbeatLive()) return;
            auto* view = dynamic_cast<KirinHyphaEditor*> (editor.get());
            if (! view) return finish (false, "editor unavailable");
            bool notified = false, checked = false;
            (post.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (false);
            hypha::automatic_pair::unpairNotice (
                [&] { checked = ! post->isPlaying(); return checked; },
                [&] { (post.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (true);
                      (view->*fixtureMenu (hypha::tests::MenuField {})) (11, {}); },
                [&] { return post->pairStatus() == KIRIN_PAIR_STATUS_UNPAIRED; },
                [&] { notified = true; });
            if (! checked || notified || post->pairedPreInstanceId() != first->instanceId())
                return finish (false, "playback race reported unpaired or lost pair");
            int other = -1;
            for (int i = 0; i < staleMenuCandidates.size(); ++i)
                if (staleMenuCandidates[i].instanceId == second->instanceId()) other = i;
            if (other < 0) return finish (false, "stale menu lacks alternate PRE");
            (view->*fixtureMenu (hypha::tests::MenuField {})) (100 + other, staleMenuCandidates);
            if (post->pairedPreInstanceId() != first->instanceId())
                return finish (false, "stale menu changed a playing pair");
            (post.get()->*fixturePlaying (hypha::tests::PlayingField {})).store (false);
            (view->*fixtureNotice (hypha::tests::NoticeField {})) = true;
            (view->*fixtureMenu (hypha::tests::MenuField {})) (100 + other, staleMenuCandidates);
            if ((view->*fixtureNotice (hypha::tests::NoticeField {}))
                || post->pairedPreInstanceId() != second->instanceId())
                return finish (false, "manual selection left automatic notice or did not change pair");
            if (! post->setPairCandidate (first->instanceId(), {}))
                return finish (false, "could not restore first pair");
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
                if (mode != "manual" && mode != "restore" && mode != "record"
                    && ! (mode == "playing" && post->pairedPreInstanceId() == first->instanceId()))
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
            if (mode == "manual" || mode == "restore" || mode == "record" || mode == "playing")
            { if (id != first->instanceId()) return finish (false, "selected exact pair changed"); }
            else if (id.isNotEmpty()) return finish (false, "unexpected automatic pair");
            if ((mode != "record" && post->isRecording()) || post->getLatencySamples() != 0
                || (mode != "listen" && post->liveCompareStatus().active))
                return finish (false, "pairing started output or record");
            if (mode == "playing")
            {
                const auto cleanup = hypha::output_owner::bit (hypha::output_owner::State::liveRestoring);
                if ((states & ~cleanup) != 0 || post->keepPhase() != KIRIN_KEEP_PHASE_IDLE
                    || post->liveCompareStatus().postTarget != 1.0f || post->liveCompareStatus().postActual != 1.0f)
                    return finish (false, "pairing started an output or Keep");
                juce::AudioBuffer<float> audio (2, 512), original (2, 512);
                for (int channel = 0; channel < 2; ++channel)
                {
                    for (int sample = 0; sample < 512; ++sample)
                        audio.setSample (channel, sample, (sample - 256) * 0.001f);
                    original.copyFrom (channel, 0, audio, channel, 0, 512);
                }
                juce::MidiBuffer midi;
                post->processBlock (audio, midi);
                if (! post->isPlaying()) return finish (false, "host playback was not active");
                for (int channel = 0; channel < 2; ++channel)
                    if (std::memcmp (audio.getReadPointer (channel), original.getReadPointer (channel),
                                     512 * sizeof (float)) != 0)
                        return finish (false, "pairing changed POST audio");
            }
            if (mode == "record") post->stopPair();
            finish (true, "guard preserved");
        }
    }
    std::string mode;
    PlayingClock playingClock;
    std::unique_ptr<Processor> first, second, post, incumbent, probe;
    juce::MemoryBlock saved;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    KirinPairPreviewValue lastPreview {};
    juce::Array<Processor::PreCandidate> staleMenuCandidates;
    hypha::pair_preview::Ticket preview;
    Clock::time_point started = Clock::now(), observedAt, requestedAt;
    int stage = 0;
    bool observed = false, manualChecked = false, probeRestored = false;
};
}
int main (int argc, char** argv)
{
    if (argc != 2) return 2;
    ReferenceValidationStorageSandbox sandbox;
    if (std::string (argv[1]) == "record")
    {
       #if JUCE_WINDOWS
        const auto identity = sandbox.directory().getChildFile ("home/AppData/Roaming/Kirin OS/identity.json");
       #else
        const auto identity = sandbox.directory().getChildFile ("home/Library/Application Support/Kirin OS/identity.json");
       #endif
        if (! identity.isAChildOf (sandbox.directory()) || identity.getParentDirectory().createDirectory().failed()
            || ! identity.appendText (R"({"schema_version":"1.0","installation_id":"test-install","hardware_id":"test-hardware","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"test-signature","license":"os","created_at":"2026-01-01T00:00:00Z","last_verified_at":"2026-01-01T00:00:00Z"})")) return 2;
    }
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::tests::editor_product::ScopedChainTimingStorage chainTimingStorage (sandbox.directory());
    if (std::string (argv[1]) == "late")
    {
        Processor fresh (Processor::Role::Post);
        juce::MemoryBlock state; fresh.getStateInformation (state);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (! xml || xml->getStringAttribute ("instance_id").isNotEmpty()) return 2;
        fresh.setStateInformation (state.getData(), (int) state.getSize());
        if (fresh.instanceId().isNotEmpty()) return 2;
        std::cout << "PASS: before enable, round-trip identity is empty" << std::endl;
    }
    GuardTest test (argv[1]);
    juce::MessageManager::getInstance()->runDispatchLoop();
    return test.passed ? 0 : 1;
}
