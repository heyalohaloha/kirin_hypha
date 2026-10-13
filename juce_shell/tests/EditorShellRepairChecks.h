#pragma once
#include <thread>
#include <atomic>
#include "TestProcessorStorage.h"
struct SetIsolation
{
    using Type = void (KirinHyphaEditor::*) (bool);
    friend Type testMember (SetIsolation);
};
template struct TestAccess<SetIsolation, &KirinHyphaEditor::setLocalBlindIsolation>;
struct AddUpdates
{
    using Type = void (KirinHyphaEditor::*) (juce::PopupMenu&) const;
    friend Type testMember (AddUpdates);
};
template struct TestAccess<AddUpdates, &KirinHyphaEditor::addUpdateCheckMenu>;

void verifyEditorShellRepairs()
{
    for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        Processor processor (role);
        juce::MemoryBlock saved; processor.getStateInformation (saved);
        std::thread host ([&] {
            processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            hypha::tests::prepareFixtureProcessor (processor, 48000, 256);
        });
        host.join(); // No message dispatch: timer setup must be posted, never synchronous here.
        require (! LiveTimingFixtureAccess::serviceTimerRunning (processor)
            && LiveTimingFixtureAccess::serviceUpdatePending (processor), "host-thread lifecycle posts timer setup to the message thread");
        std::unique_ptr<KirinHyphaEditor> editor (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
        require (editor != nullptr, "PRE/POST editor after host-thread restore");
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        juce::PopupMenu updates;
        (editor.get()->*testMember (AddUpdates {})) (updates);
        if (hypha::update::productionPublicKey().isEmpty())
            require (updates.getNumItems() == 0, "keyless PRE/POST builds hide every update-check menu item");
        auto* view = component<hypha::observatory::View> (*editor);
        require (view != nullptr, "real measurement root exists");
        struct AccessibleChild { juce::Component* component; bool accessible, handler; };
        std::vector<AccessibleChild> before;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& root) {
            before.push_back ({ &root, root.isAccessible(), root.getAccessibilityHandler() != nullptr });
            for (auto* child : root.getChildren()) collect (*child);
        };
        collect (*view);
        require (before.size() > 10, "test includes grandchildren, not only the measurement root");
        for (const char* screen : { "live-blind-screen", "local-blind-screen", "version-blind-screen" })
        {
            if (role == Processor::Role::Pre) continue; // PRE has no comparison output or Blind surface.
            auto* blind = find (*editor, screen);
            require (blind != nullptr, "all three Blind surfaces share the isolation path");
            blind->setVisible (true);
            (editor.get()->*testMember (SetIsolation {})) (true);
            for (const auto& item : before)
                require (! item.component->isAccessible(), "Blind conceals every underlying descendant");
            blind->setVisible (false);
            (editor.get()->*testMember (SetIsolation {})) (false);
            for (const auto& item : before)
            {
                require (item.component->isAccessible() == item.accessible,
                         "closing each Blind restores every descendant's original AX state");
                if (item.handler) require (item.component->getAccessibilityHandler() != nullptr,
                                          "closing each Blind restores the actual child AX handlers");
            }
        }
        // Simultaneous host restore and explicit source actions used to invert controller/handle locks.
        if (role == Processor::Role::Post)
        {
            std::atomic<bool> begin { false };
            std::thread restore ([&] {
                while (! begin.load()) std::this_thread::yield();
                for (int i = 0; i < 50; ++i)
                    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            });
            begin = true;
            for (int i = 0; i < 50; ++i)
            { processor.selectReferenceB(); processor.selectReferenceC(); processor.selectReferenceA(); }
            restore.join();
            require (! processor.referenceAuditionSnapshot().bSelected, "restore and source presses leave fixture A selected");
        }
        processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
    }
}
