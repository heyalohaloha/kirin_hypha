#include "../src/HyphaBlindScreen.h"
#include "ShortcutTestAccess.h"
#include "TestProcessorStorage.h"
#include "ReferenceSelectionReopenTest.h"
#include "../src/HyphaLanguage.h"
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace hypha::tests
{
namespace
{
struct MeterClock final : juce::AudioPlayHead
{
    int64_t position = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setIsPlaying (true); info.setTimeInSamples (position); return info;
    }
};

KirinMeterSession seedMeter (KirinHyphaProcessorBase& processor, MeterClock& clock)
{
    juce::AudioBuffer<float> block (2, 256); juce::MidiBuffer midi;
    auto due = std::chrono::steady_clock::now();
    const auto deadline = due + std::chrono::seconds (8);
    KirinMeterSession meter {};
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 256; ++frame)
                block.setSample (channel, frame, static_cast<float> (1.25 * std::sin ((clock.position + frame) * 0.13)));
        processor.processBlock (block, midi); clock.position += 256;
        due += std::chrono::nanoseconds (256LL * 1'000'000'000 / 48000);
        if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
        if (processor.pollMeterSession (meter) && meter.observed_frames > 0
            && meter.clip_events[0] > 0 && meter.channel_clip_latched[0]
            && meter.sample_peak_hold_dbfs[0] > 0.0) return meter;
    }
    throw std::runtime_error ("fixture did not produce meter values and holds: frames="
        + std::to_string (meter.observed_frames) + ", clips=" + std::to_string (meter.clip_events[0])
        + ", latched=" + std::to_string (meter.channel_clip_latched[0])
        + ", peak=" + std::to_string (meter.sample_peak_hold_dbfs[0]));
}

void requireMeterPreserved (KirinHyphaProcessorBase& processor, const KirinMeterSession& before)
{
    KirinMeterSession now {};
    if (! processor.pollMeterSession (now) || now.generation != before.generation
        || now.measurement_epoch != before.measurement_epoch || now.active_frames < before.active_frames
        || now.clip_events[0] < before.clip_events[0] || ! now.channel_clip_latched[0]
        || now.sample_peak_hold_dbfs[0] < before.sample_peak_hold_dbfs[0])
        throw std::runtime_error ("RESET unexpectedly changed the processor meter");
}
}

void verifyShortcutEditorActions()
{
    using Processor = KirinHyphaProcessorBase;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (KIRIN_HYPHA_PRE_DISPLAY ? Processor::Role::Pre : Processor::Role::Post);
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    if (! processor.setBusesLayout (layout)) throw std::runtime_error ("stereo fixture layout rejected");
    MeterClock clock; processor.setPlayHead (&clock);
    prepareFixtureProcessor (processor, 48000, 256);
    const auto readyDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! fixtureStorageReady (processor) && juce::Time::getMillisecondCounterHiRes() < readyDeadline)
        if (! afterQueuedMessages ([] {})) throw std::runtime_error ("processor startup callback did not run");
    if (! fixtureStorageReady (processor)) throw std::runtime_error ("processor fixture did not become ready");
    const auto measured = seedMeter (processor, clock);
    auto editor = std::unique_ptr<KirinHyphaEditor> (
        dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
    if (editor == nullptr) throw std::runtime_error ("Editor could not be created");
    juce::DocumentWindow window ("Test", juce::Colours::black, juce::DocumentWindow::minimiseButton);
    window.setContentNonOwned (editor.get(), true);
    window.setVisible (true);
    if (! editor->isShowing()) throw std::runtime_error ("Editor peer did not become visible");
    auto& name = editor.get()->*shortcutMember (ShortcutName {});
    if (name.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) || name.isEditing())
        throw std::runtime_error ("Space was consumed by the Editor name field");
    auto& view = editor.get()->*shortcutMember (ShortcutView {});
    if (! view.onDomainChange || ! view.onReset) throw std::runtime_error ("Actions are not wired");
    view.onDomainChange (observatory::Domain::time);
    if (view.domain() != observatory::Domain::time) throw std::runtime_error ("TIME action failed");
    view.onDomainChange (observatory::Domain::level);
    if (view.domain() != observatory::Domain::level) throw std::runtime_error ("LEVEL action failed");
    // Without waiting: a test process that is not the front app loses an open menu to the menu's next timer.
    auto chooseSecondDomainInMenu = []
    {
        auto* menu = juce::ModalComponentManager::getInstance()->getModalComponent (0);
        if (menu == nullptr) throw std::runtime_error ("Domain menu did not open");
        menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        menu->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    };
    // The closed menu's choice is handed over at once, before the Editor's timer runs. A fixture
    // BLIND (only the flag) would be closed by that timer, as no BLIND is really running.
    auto deliverMenuChoiceNow = []
    {
        // ModalComponentManager hands a closed menu's result over through its private AsyncUpdater.
        ((juce::AsyncUpdater*) juce::ModalComponentManager::getInstance())->handleUpdateNowIfNeeded();
        if (juce::ModalComponentManager::getInstance()->getNumModalComponents() != 0)
            throw std::runtime_error ("Domain menu stayed open");
    };
    auto chooseTimeFromMenu = [&]
    {
        if (! view.onDomainMenu) throw std::runtime_error ("Domain menu is not wired");
        view.onDomainMenu();
        chooseSecondDomainInMenu();
        deliverMenuChoiceNow();
    };
    view.setHostRecording (true);
    if (! view.hybridVuShownByRecording()) throw std::runtime_error ("Recording VU fixture failed");
    chooseTimeFromMenu();
    if (view.domain() != observatory::Domain::time || view.hybridVuShownByRecording())
        throw std::runtime_error ("Domain menu did not dismiss recording VU");
    view.setHostRecording (false);
    view.onDomainChange (observatory::Domain::level);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    auto& localBlind = editor.get()->*shortcutMember (ShortcutLocalBlindView {});
    auto& localOpen = editor.get()->*shortcutMember (ShortcutLocalBlindOpen {});
    localOpen = true;
    localBlind.setVisible (true);
    const auto requireLocalNotice = [&] (const juce::String& expected)
    {
        auto* result = dynamic_cast<juce::Label*> (localBlind.findChildWithID ("local-blind-result"));
        if (result == nullptr || ! result->isShowing() || result->getText() != expected)
            throw std::runtime_error ("Local Blind refusal was not visible");
    };
    (editor.get()->*shortcutMember (ShortcutBlindSource {})) (1);
    requireLocalNotice ("SOURCE COULD NOT BE SELECTED");
    (editor.get()->*shortcutMember (ShortcutBlindReveal {}))();
    requireLocalNotice ("CHOOSE AN ANSWER AFTER BOTH PASSES");
    view.onDomainChange (observatory::Domain::time);
    requireLocalNotice ("End Blind Compare first");
    if (view.domain() != observatory::Domain::level)
        throw std::runtime_error ("Local Blind allowed a domain change");
    localBlind.clearActionNotice();
    view.onReset();
    requireLocalNotice ("End Blind Compare first");
    localBlind.clearActionNotice();
    view.onClearPeakClipHolds();
    requireLocalNotice ("End Blind Compare first");
    requireMeterPreserved (processor, measured);
    localBlind.setVisible (false);
    localOpen = false;
    view.onDomainMenu();
    auto& blindOpen = editor.get()->*shortcutMember (ShortcutLiveBlindOpen {});
    auto& liveBlind = editor.get()->*shortcutMember (ShortcutLiveBlindView {});
    blindOpen = true;
    chooseSecondDomainInMenu();
    deliverMenuChoiceNow();
    const auto refusal = liveBlind.actionNoticeShown();
    blindOpen = false;
    if (view.domain() != observatory::Domain::level)
        throw std::runtime_error ("Domain menu changed view after BLIND began");
    if (refusal != "End Blind Compare first")
        throw std::runtime_error ("Domain menu choice after BLIND began was not refused on the BLIND screen");
   #endif
    if (i18n::translate ("Reset Meter Session?", i18n::Language::japanese)
        != juce::String::fromUTF8 (u8"Meter Sessionをリセットしますか"))
        throw std::runtime_error ("RESET Japanese lookup failed");
    if (i18n::translate ("Press Return to choose a PRE", i18n::Language::japanese)
        != juce::String::fromUTF8 (u8"Returnを押してPREを選びます"))
        throw std::runtime_error ("PAIR Japanese lookup failed");
    auto& watchPresent = editor.get()->*shortcutMember (ShortcutWatchPresent {});
    watchPresent = true;
    view.onReset();
    auto& dialog = editor.get()->*shortcutMember (ShortcutResetDialog {});
    if (dialog == nullptr || dialog->getNumButtons() != 2)
        throw std::runtime_error ("RESET did not open confirmation");
    auto* first = dialog.get();
    view.onReset();
    if (dialog.get() != first) throw std::runtime_error ("RESET opened a duplicate confirmation");
    if (! watchPresent) throw std::runtime_error ("RESET cleared display before confirmation");
    requireMeterPreserved (processor, measured);
    if (! dialog->getButton (0)->hasKeyboardFocus (false))
        throw std::runtime_error ("RESET did not initially focus CANCEL");
    if (dialog->getPeer() == nullptr || ! dialog->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey)))
        throw std::runtime_error ("Return did not reach the focused CANCEL button");
    const auto returnDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (dialog != nullptr && juce::Time::getMillisecondCounterHiRes() < returnDeadline)
        if (! afterQueuedMessages ([] {})) throw std::runtime_error ("Return callback did not run");
    if (dialog != nullptr || ! watchPresent) throw std::runtime_error ("Return did not cancel RESET");
    requireMeterPreserved (processor, measured);
    view.onReset();
    dialog->exitModalState (0);
    if (! afterQueuedMessages ([] {}) || dialog != nullptr || ! watchPresent)
        throw std::runtime_error ("CANCEL did not preserve display");
    requireMeterPreserved (processor, measured);
    view.onReset();
    dialog->exitModalState (1);
    editor->setVisible (false);
    if (dialog != nullptr) throw std::runtime_error ("hidden Editor kept RESET open");
    Processor other (KIRIN_HYPHA_PRE_DISPLAY ? Processor::Role::Pre : Processor::Role::Post);
    prepareFixtureProcessor (other, 48000, 256);
    auto otherEditor = std::unique_ptr<KirinHyphaEditor> (
        dynamic_cast<KirinHyphaEditor*> (other.createEditorIfNeeded()));
    if (otherEditor == nullptr) throw std::runtime_error ("second Editor could not be created");
    otherEditor->setVisible (true);
    if (otherEditor->isCurrentlyBlockedByAnotherModalComponent())
        throw std::runtime_error ("hidden RESET blocks another Editor");
    editor->setVisible (true);
    view.onReset();
    if (! afterQueuedMessages ([] {}) || dialog == nullptr || ! watchPresent)
        throw std::runtime_error ("old RESET callback affected the reopened confirmation");
    requireMeterPreserved (processor, measured);
    dialog->exitModalState (0);
    if (! afterQueuedMessages ([] {}) || dialog != nullptr)
        throw std::runtime_error ("reopened RESET did not cancel");
    view.onReset();
    dialog->exitModalState (1);
    window.setVisible (false);
    (editor.get()->*shortcutMember (ShortcutTimer {}))();
    if (dialog != nullptr || otherEditor->isCurrentlyBlockedByAnotherModalComponent())
        throw std::runtime_error ("hidden parent kept RESET modal");
    if (! afterQueuedMessages ([] {}) || ! watchPresent) throw std::runtime_error ("hidden parent confirmed RESET");
    requireMeterPreserved (processor, measured);
    window.setVisible (true);
    view.onReset();
    window.setMinimised (true);
    const auto minimiseDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! window.isMinimised() && juce::Time::getMillisecondCounterHiRes() < minimiseDeadline)
        if (! afterQueuedMessages ([] {})) throw std::runtime_error ("minimise callback did not run");
    if (! window.isMinimised()) throw std::runtime_error ("test window did not minimise");
    (editor.get()->*shortcutMember (ShortcutTimer {}))();
    if (dialog != nullptr || otherEditor->isCurrentlyBlockedByAnotherModalComponent())
        throw std::runtime_error ("minimised parent kept RESET modal");
    if (! afterQueuedMessages ([] {}) || ! watchPresent) throw std::runtime_error ("minimised parent confirmed RESET");
    requireMeterPreserved (processor, measured);
    window.setMinimised (false);
    const auto restoreDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! editor->isShowing() && juce::Time::getMillisecondCounterHiRes() < restoreDeadline)
        if (! afterQueuedMessages ([] {})) throw std::runtime_error ("restore callback did not run");
    if (! editor->isShowing()) throw std::runtime_error ("test window did not restore");
    other.editorBeingDeleted (otherEditor.get()); otherEditor.reset(); other.releaseResources();
    view.onReset();
    dialog->exitModalState (1);
    if (! afterQueuedMessages ([] {}) || dialog != nullptr)
        throw std::runtime_error ("confirmed RESET did not close confirmation");
    KirinMeterSession reset {};
    if (! processor.pollMeterSession (reset) || reset.generation == measured.generation
        || reset.active_frames != 0 || reset.clip_events[0] != 0 || reset.channel_clip_latched[0]
        || std::isfinite (reset.sample_peak_hold_dbfs[0]))
        throw std::runtime_error ("confirmed RESET did not clear processor meter and holds");
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    auto& reference = editor.get()->*shortcutMember (ShortcutReference {});
    bool versionRequested = false;
    reference.onStartBlind = [&] { versionRequested = true; };
    view.onDomainChange (observatory::Domain::reference);
    if (view.domain() != observatory::Domain::reference || ! view.onLocalBlind)
        throw std::runtime_error ("Reference or PRE/POST BLIND action unavailable");
    reference_ui::State versionChosen;
    versionChosen.separateComparisons = versionChosen.libraryReceived = versionChosen.aAvailable = true;
    versionChosen.versionReady = true;
    versionChosen.osAccess = os_access::State::ready;
    versionChosen.comparisonSlot = 1;
    versionChosen.versionId = "v1";
    versionChosen.blindPhase = reference_ui::BlindPhase::available;
    reference.setState (versionChosen);
    (editor.get()->*shortcutMember (ShortcutBlindStart {}))();
    if (! versionRequested) throw std::runtime_error ("Reference BLIND did not request Version Blind");
   #endif
    const auto measuredAgain = seedMeter (processor, clock);
    view.onReset();
    if (dialog == nullptr) throw std::runtime_error ("RESET did not reopen");
    dialog->exitModalState (1);
    window.clearContentComponent();
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    if (! afterQueuedMessages ([] {})) throw std::runtime_error ("destroyed Editor callback did not finish");
    requireMeterPreserved (processor, measuredAgain);
    processor.releaseResources();
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void verifyShortcutBlindScreenActions()
{
    blind_ui::ScreenComponent component ("shortcut-blind");
    int source = 0, reveals = 0, ends = 0;
    component.onSelect = [&] (int selected) { source = selected; };
    component.onReveal = [&] { ++reveals; };
    component.onEnd = [&] { ++ends; };
    auto require = [] (bool condition)
    {
        if (! condition) throw std::runtime_error ("Blind action availability");
    };
    blind_ui::Screen screen;
    screen.endEnabled = false;
    component.setScreen (screen);
    require (! component.requestSource (1) && ! component.requestReveal()
             && ! component.requestEnd() && source == 0 && reveals == 0 && ends == 0);
    screen.sourcesShown = true;
    screen.sourceOneEnabled = true;
    screen.revealShown = true;
    screen.endEnabled = true;
    component.setScreen (screen);
    require (! component.requestSource (0) && ! component.requestSource (2)
             && ! component.requestSource (3) && ! component.requestReveal());
    require (component.requestSource (1) && source == 1 && component.requestEnd() && ends == 1);
    screen.sourceTwoEnabled = true;
    screen.revealEnabled = true;
    component.setScreen (screen);
    require (component.requestSource (2) && source == 2 && component.requestReveal() && reveals == 1);
}
#endif
}
