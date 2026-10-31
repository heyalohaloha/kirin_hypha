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
struct ReferenceOpenedAt
{
    using Type = double KirinHyphaEditor::*;
    friend Type privateMember (ReferenceOpenedAt);
};
template struct PrivateAccess<ReferenceOpenedAt, &KirinHyphaEditor::referenceOpenedAt>;
struct ReferenceOpenedTick  // what each REF refresh of the timer runs first
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type privateMember (ReferenceOpenedTick);
};
template struct PrivateAccess<ReferenceOpenedTick, &KirinHyphaEditor::serviceReferenceOpenedNotice>;
struct ReferenceLaterNotice
{
    using Type = void (KirinHyphaEditor::*) (const juce::String&);
    friend Type privateMember (ReferenceLaterNotice);
};
template struct PrivateAccess<ReferenceLaterNotice, &KirinHyphaEditor::showToast>;
struct ReferenceEditorTimer { using Type = void (KirinHyphaEditor::*)(); friend Type privateMember (ReferenceEditorTimer); };
template struct PrivateAccess<ReferenceEditorTimer, &KirinHyphaEditor::timerCallback>;

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

    // C from a small window: "opened at 300%" waits for a tick 0.2 s later. A host that puts the window
    // back (REAPER's FX chain) gets the fact instead; a window that stayed wide gets "opened at 300%".
    editor->setSize (600, 400);
    auto& pane = editor.get()->*privateMember (ReferencePane {});
    auto& opened = editor.get()->*privateMember (EditorToast {});
    auto& openedAt = editor.get()->*privateMember (ReferenceOpenedAt {});
    require (static_cast<bool> (pane.onOpenLarge), "C has an open-at-300% action");
    opened = {};
    pane.onOpenLarge (2);
    (editor.get()->*privateMember (ReferenceOpenedTick {})) ();
    require (editor->getWidth() == 900 && ! opened.contains ("opened at 300%"),
             "the page-opened notice waits until the host has had time to put the window back");
    editor->setSize (600, 400);  // the host restores its own size
    openedAt -= 0.2;              // 0.2 s later
    (editor.get()->*privateMember (ReferenceOpenedTick {})) ();
    require (opened == "C and V open at 300%. Choose 300% or a floating window.",
             "a window back at its size (the host or a drag) says what C needs, not that C opened or why");
    opened = {};
    pane.onOpenLarge (2);
    openedAt -= 0.2;
    (editor.get()->*privateMember (ReferenceOpenedTick {})) ();
    require (editor->getWidth() == 900 && opened.startsWith ("C opened at 300%"),
             "a window that stayed wide says C opened at 300%");
    // The wait belongs to its press. The user's own size, another role, a later notice, another page
    // or a hidden window drops it without a word; a size the host may not have chosen gets no cause.
    auto& sizeView = *component<observatory::View> (*editor);
    const auto pressC = [&]
    {
        editor->setSize (600, 400); opened = {};
        pane.onOpenLarge (2);
    };
    const auto tickLater = [&] (double seconds)
    {
        openedAt -= seconds;
        (editor.get()->*privateMember (ReferenceOpenedTick {})) ();
    };
    pressC(); sizeView.onSizeChange (observatory::sizePresets[3]); tickLater (0.2);  // 200%, the size before
    require (opened.isEmpty(), "the user's own size after C says neither C opened nor the window did not widen");
    pressC(); editor->setSize (750, 500); tickLater (0.2);
    require (opened.isEmpty(), "a size neither widened nor the size before says no cause");
    pressC(); pane.onOpenLarge (1); tickLater (0.2);
    require (! opened.startsWith ("C "), "another role drops the notice for C");
    pressC(); (editor.get()->*privateMember (ReferenceLaterNotice {})) ("Reference is not ready"); tickLater (0.2);
    require (opened == "Reference is not ready", "a later operation's notice is not overwritten by the wait for C");
    pressC(); domain = observatory::Domain::level; tickLater (0.2); domain = observatory::Domain::reference;
    require (opened.isEmpty(), "leaving the REF page drops the notice for C");
    pressC(); editor->setVisible (false); tickLater (2.0); editor->setVisible (true);
    require (opened.isEmpty(), "a window hidden past the wait drops the notice for C");
    for (const auto& press : { std::function<void()> (pressC), std::function<void()> ([&] { processor.selectReferenceVisualSlot (1);
                                                                                          editor->setSize (600, 400); opened = {}; pane.onOpenLarge (1); }) })
    {
        press(); editor->setVisible (false); (editor.get()->*privateMember (ReferenceEditorTimer {})) ();
        editor->setVisible (true); tickLater (0.2);
        require (opened.isEmpty(), "a window hidden for a moment drops the notice for C and V");
    }
    processor.selectReferenceVisualSlot (2);
    // V widens on the same path (a host at 200% keeps it small too): the same fact, never "V opened".
    const auto pressV = [&]
    {
        editor->setSize (600, 400); opened = {};
        processor.selectReferenceVisualSlot (1);  // the V press shows the V page (an OS license), then widens
        pane.onOpenLarge (1);
    };
    pressV(); editor->setSize (600, 400); tickLater (0.2);
    require (opened == "C and V open at 300%. Choose 300% or a floating window.",
             "a V window back at its size says what V needs, not that V opened at 300%");
    pressV(); tickLater (0.2);
    require (editor->getWidth() == 900 && opened.startsWith ("V opened at 300%"), "a V window that stayed wide says V opened");
    processor.selectReferenceVisualSlot (2);
    editor->setSize (600, 400);
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
