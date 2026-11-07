#pragma once
#include "ShortcutTestAccess.h"
#include "TestProcessorStorage.h"
#include "ReferenceSelectionReopenTest.h"
#include <memory>
#include <stdexcept>

// The window as a host opens it, and keys as the window receives them: through the peer, to whatever
// holds the focus, up the parents. Nothing here calls the editor's key handler or an action by hand.
namespace hypha::tests
{
struct ShortcutKeyRig
{
    using Processor = KirinHyphaProcessorBase;
    static Processor::Role hostedRole()
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        return KIRIN_HYPHA_PRE_DISPLAY ? Processor::Role::Pre : Processor::Role::Post;
    }
    Processor processor { hostedRole() };
    std::unique_ptr<KirinHyphaEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;

    ShortcutKeyRig()
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        if (! processor.setBusesLayout (layout)) throw std::runtime_error ("stereo fixture layout rejected");
        prepareFixtureProcessor (processor, 48000, 256);
        const auto ready = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (! fixtureStorageReady (processor) && juce::Time::getMillisecondCounterHiRes() < ready)
            if (! afterQueuedMessages ([] {})) throw std::runtime_error ("processor startup callback did not run");
        editor.reset (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        if (editor == nullptr) throw std::runtime_error ("Editor could not be created");
        window = std::make_unique<juce::DocumentWindow> ("Keys", juce::Colours::black, juce::DocumentWindow::minimiseButton);
        window->setContentNonOwned (editor.get(), true);
        window->setVisible (true);
        if (! editor->isShowing() || editor->getPeer() == nullptr) throw std::runtime_error ("Editor peer did not become visible");
    }

    ~ShortcutKeyRig()
    {
        window->clearContentComponent();
        window.reset();
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }

    hypha::observatory::View& view() { return editor.get()->*shortcutMember (ShortcutView {}); }
    KeysPanel& keys() { return editor.get()->*shortcutMember (ShortcutKeysPanel {}); }
    juce::String toast() { return editor.get()->*shortcutMember (ShortcutToastText {}); }
    void clearToast() { (editor.get()->*shortcutMember (ShortcutToastText {})).clear(); }

    // What the window hears of any click in it, on any child (it listens to them all). A click sent
    // through the peer lands on whatever window is on top of the screen, and a test window may not be.
    void click (juce::Point<int> at)
    {
        const auto now = juce::Time::getCurrentTime();
        editor->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(),
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
            juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
            juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
            editor.get(), editor.get(), now, at.toFloat(), now, 1, false));
    }

    // A press still held: a second call is the same key repeating, until `up()`.
    bool hold (int code, int modifiers = 0, juce::juce_wchar text = 0)
    {
        return editor->getPeer()->handleKeyPress (juce::KeyPress (code, juce::ModifierKeys (modifiers), text));
    }
    void up() { editor->getPeer()->handleKeyUpOrDown (false); }
    bool press (int code, int modifiers = 0, juce::juce_wchar text = 0)
    {
        const auto used = hold (code, modifiers, text);
        up();
        return used;
    }
    bool pressShift (int code) { return press (code, juce::ModifierKeys::shiftModifier); }
};
}
