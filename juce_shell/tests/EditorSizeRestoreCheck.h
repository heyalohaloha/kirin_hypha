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
struct EnsureReferenceRoom
{
    using Type = bool (KirinHyphaEditor::*)();
    friend Type privateMember (EnsureReferenceRoom);
};
template struct PrivateAccess<EnsureReferenceRoom, &KirinHyphaEditor::ensureReferenceRoom>;
struct EditorDomain
{
    using Type = observatory::Domain KirinHyphaEditor::*;
    friend Type privateMember (EditorDomain);
};
template struct PrivateAccess<EditorDomain, &KirinHyphaEditor::observatoryDomain>;
struct ReferencePane
{
    using Type = reference_ui::Component KirinHyphaEditor::*;
    friend Type privateMember (ReferencePane);
};
template struct PrivateAccess<ReferencePane, &KirinHyphaEditor::referenceView>;
struct EditorToast
{
    using Type = juce::String KirinHyphaEditor::*;
    friend Type privateMember (EditorToast);
};
template struct PrivateAccess<EditorToast, &KirinHyphaEditor::toastText>;

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

// The REF page widens a small editor to 900 x 600 for C, V and VERSION BLIND. That size is the
// page's, not the user's: it is not saved, leaving the page returns to the size before, and a
// larger editor is never shrunk to 900 x 600. VERSION BLIND starts on the same press that widens.
inline void verifyReferenceRoomSize()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (Processor::Role::Post);
    hypha::tests::prepareFixtureProcessor (processor, 48'000, 960);
    std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
    require (editor != nullptr && editor->getWidth() == 600, "a new editor opens at 200% before REF");
    auto& domain = editor.get()->*privateMember (EditorDomain {});
    domain = observatory::Domain::reference;
    require ((editor.get()->*privateMember (EnsureReferenceRoom {})) ()
                 && editor->getWidth() == 900 && editor->getHeight() == 600, "C / V widen a small editor to 900 x 600");
    require (savesEditorSize (processor, 600, 400), "the size the REF page widened to is not saved as the user's");
    (editor.get()->*privateMember (LayoutLocalBlind {})) ();
    require (editor->getWidth() == 900, "the widened size stays while the REF page is shown");
    domain = observatory::Domain::level;
    (editor.get()->*privateMember (LayoutLocalBlind {})) ();
    require (editor->getWidth() == 600 && editor->getHeight() == 400, "leaving the REF page returns to the size before");
    require (savesEditorSize (processor, 600, 400), "and the user's size stays saved");

    editor->setSize (1350, 900);
    require (savesEditorSize (processor, 1350, 900), "a user enlargement is saved");
    domain = observatory::Domain::reference;
    require ((editor.get()->*privateMember (EnsureReferenceRoom {})) () && editor->getWidth() == 1350,
             "a larger editor is not shrunk to 900 x 600");

    editor->setSize (600, 400);
    auto& pane = editor.get()->*privateMember (ReferencePane {});
    require (static_cast<bool> (pane.onStartBlind), "VERSION BLIND has a start action");
    pane.onStartBlind();
    const auto toast = editor.get()->*privateMember (EditorToast {});
    require (! toast.contains ("Press VERSION BLIND to start"), "VERSION BLIND starts on the press that widens");
    require (editor->getWidth() == 600 && editor->getHeight() == 400 && savesEditorSize (processor, 600, 400),
             "a VERSION BLIND that could not start returns to the size before, unsaved");
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    processor.releaseResources();
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
    verifyReferenceRoomSize();
}
}
