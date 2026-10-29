#pragma once
// A new editor opens at 200%. A saved size still wins, whether the host restores the state before it
// opens the editor or after it (the editor first, then a state on another page), and a state that
// holds no size opens at the same 200% default.
#include "EditorProductChecks.h"
#include "TestProcessorStorage.h"

namespace hypha::tests::editor_product
{
struct RefreshObservatory
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (RefreshObservatory);
};
template struct PrivateAccess<RefreshObservatory, &KirinHyphaEditor::refreshObservatory>;

struct LocalBlindOpen
{
    using Type = bool KirinHyphaEditor::*;
    friend Type privateMember (LocalBlindOpen);
};
template struct PrivateAccess<LocalBlindOpen, &KirinHyphaEditor::localBlindOpen>;
struct LayoutLocalBlind
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (LayoutLocalBlind);
};
template struct PrivateAccess<LayoutLocalBlind, &KirinHyphaEditor::layoutLocalBlindProduct>;
struct ReturnFromLocalBlind
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (ReturnFromLocalBlind);
};
template struct PrivateAccess<ReturnFromLocalBlind, &KirinHyphaEditor::returnFromLocalBlindSize>;

inline juce::MemoryBlock editorSizeState (int version, observatory::Domain domain, int preset,
                                          int width, int height)
{
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("display_state_version", version);
    if (version >= 2)
    {
        xml.setAttribute ("observatory_domain", (int) observatory::stateValue (domain));
        xml.setAttribute ("observatory_size", preset);
    }
    if (version >= 3)
    {
        xml.setAttribute ("observatory_width", width);
        xml.setAttribute ("observatory_height", height);
    }
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary (xml, bytes);
    return bytes;
}

inline bool savesEditorSize (Processor& processor, int width, int height)
{
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    return xml != nullptr && xml->getIntAttribute ("observatory_width") == width
        && xml->getIntAttribute ("observatory_height") == height;
}

inline void verifyEditorSizeRestore()
{
    for (const auto role : { Processor::Role::Post, Processor::Role::Pre })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (role);
        hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr && editor->getWidth() == 600 && editor->getHeight() == 400,
                 "a new editor opens at 200%");
        require (savesEditorSize (processor, 600, 400), "a new instance saves 200%");

        // The host opens the editor first, then restores a saved 100% on another page.
        auto* view = component<observatory::View> (*editor);
        require (view != nullptr, "the editor shows its Observatory");
        const auto otherPage = view->domain() == observatory::Domain::time ? observatory::Domain::level
                                                                           : observatory::Domain::time;
        const auto bytes = editorSizeState (5, otherPage, 0, 300, 200);
        processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        (editor.get()->*privateMember (RefreshObservatory {})) ();
        require (editor->getWidth() == 300 && editor->getHeight() == 200,
                 "a saved 100% wins over the open 200% editor");
        require (savesEditorSize (processor, 300, 200), "and is saved again as 100%");
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }

    struct Restored { int version, preset, width, height, openWidth, openHeight; const char* what; };
    for (const auto& state : { Restored { 1, 0, 0, 0, 600, 400, "a state with no size opens at 200%" },
                               Restored { 2, 0, 0, 0, 300, 200, "a preset-only state keeps its 100%" },
                               Restored { 5, 3, 654, 436, 654, 436, "a free size comes back as saved" } })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (Processor::Role::Post);
        const auto bytes = editorSizeState (state.version, observatory::Domain::level, state.preset,
                                            state.width, state.height);
        processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr && editor->getWidth() == state.openWidth
                     && editor->getHeight() == state.openHeight, state.what);
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }

    // Local Blind holds the editor at 900 x 600 without saving it. Closing it returns to the size
    // before, unless the host restored a state meanwhile: the restored size wins, even 300%.
    struct BlindRestore { int width, height, closedWidth, closedHeight; const char* what; };
    for (const auto& blind : { BlindRestore { 0, 0, 600, 400, "Blind returns to the size before it" },
                               BlindRestore { 300, 200, 300, 200, "a 100% restored during Blind wins" },
                               BlindRestore { 900, 600, 900, 600, "a 300% restored during Blind wins" } })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (Processor::Role::Post);
        hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr && editor->getWidth() == 600, "a new editor opens at 200% before Blind");
        auto& blindOpen = editor.get()->*privateMember (LocalBlindOpen {});
        blindOpen = true;
        (editor.get()->*privateMember (LayoutLocalBlind {})) ();
        require (editor->getWidth() == 900 && editor->getHeight() == 600, "Blind holds the editor at 900 x 600");
        require (savesEditorSize (processor, 600, 400), "the size Blind holds is not saved as the user's");
        if (blind.width != 0)
        {
            const auto bytes = editorSizeState (5, observatory::Domain::level, 0, blind.width, blind.height);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
        }
        blindOpen = false; // the order closing Blind takes: return, relayout, refresh
        (editor.get()->*privateMember (ReturnFromLocalBlind {})) ();
        editor->resized();
        (editor.get()->*privateMember (RefreshObservatory {})) ();
        require (editor->getWidth() == blind.closedWidth && editor->getHeight() == blind.closedHeight, blind.what);
        require (savesEditorSize (processor, blind.closedWidth, blind.closedHeight), blind.what);
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        processor.releaseResources();
    }
}
}
