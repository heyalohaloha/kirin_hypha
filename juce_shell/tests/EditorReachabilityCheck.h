#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// 2026-10-18: every control the editor shows (button, selector, text field) lies inside the
// editor, a press at its centre reaches it, and the keyboard reaches it (Tab walks its focus
// container to it), at each size, display scale and language. A host's display scale
// (setScaleFactor) scales the whole editor; the controls keep their places in it.
namespace hypha::tests::editor_reachability
{
inline bool shownWithin (const juce::Component& component, const juce::Component& editor)
{
    for (auto* item = &component; item != nullptr && item != &editor; item = item->getParentComponent())
        if (! item->isVisible() || item->getWidth() <= 0 || item->getHeight() <= 0) return false;
    return component.isEnabled();
}

// Tab walks the control's keyboard focus container (the editor, or a full-screen view inside it)
// to the control itself or to a part of it that takes the keys (a selector's box).
inline bool keyboardReaches (juce::Component& control)
{
    auto* container = control.findKeyboardFocusContainer();
    if (container == nullptr) return false;
    const auto traverser = container->createKeyboardFocusTraverser();
    if (traverser == nullptr) return false;
    for (auto* reached : traverser->getAllComponents (container))
        if (reached == &control || control.isParentOf (reached))
            return true;
    return false;
}

// Calls `fail` with a description for each shown control that is cut by the edge of `editor` (the
// editor, or the host window holding it at a display scale) or that a press at its centre does not
// reach. Returns the number of controls checked.
inline int check (juce::Component& editor, const std::function<void (const juce::String&)>& fail)
{
    int checked = 0;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& component)
    {
        for (auto* child : component.getChildren())
        {
            visit (*child);
            const bool control = dynamic_cast<juce::Button*> (child) != nullptr
                || dynamic_cast<juce::ComboBox*> (child) != nullptr
                || dynamic_cast<juce::TextEditor*> (child) != nullptr;
            if (! control || ! shownWithin (*child, editor)) continue;
            ++checked;
            const auto bounds = editor.getLocalArea (child, child->getLocalBounds());
            const auto name = child->getComponentID().isNotEmpty() ? child->getComponentID() : child->getTitle();
            if (! editor.getLocalBounds().contains (bounds))
                fail ("cut by the editor's edge: " + name + " " + bounds.toString());
            auto* hit = editor.getComponentAt (bounds.getCentre());
            if (hit != child && ! child->isParentOf (hit))
                fail ("a press at its centre does not reach it: " + name + " " + bounds.toString()
                      + (hit != nullptr ? " (reaches " + hit->getComponentID() + " " + hit->getTitle() + ")" : juce::String()));
            if (! keyboardReaches (*child))
                fail ("the keyboard does not reach it: " + name + " " + bounds.toString());
        }
    };
    visit (editor);
    return checked;
}
}
