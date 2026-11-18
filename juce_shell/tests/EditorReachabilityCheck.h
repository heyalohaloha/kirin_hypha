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

// Opt-in for the BLIND details fixture only. The whole read-only editor scrolls inside
// a viewport; test both ends and hit/keyboard reachability instead of demanding that
// off-screen content lie in the host. All other controls retain the original checks.
inline bool checkBlindDetailsScroll (juce::Component& child, juce::Component& editor,
                                     const std::function<void (const juce::String&)>& fail)
{
    if (child.getComponentID() != "live-blind-details-body") return false;
    juce::Viewport* viewport = nullptr;
    for (auto* parent = child.getParentComponent(); parent && parent != &editor; parent = parent->getParentComponent())
        if (parent->getComponentID() == "live-blind-details-viewport")
            viewport = dynamic_cast<juce::Viewport*> (parent);
    if (!viewport) { fail ("BLIND details has no scroll viewport"); return true; }
    auto* content = viewport->getViewedComponent();
    if (!content || child.getParentComponent() != content || !content->getLocalBounds().contains(child.getBounds()))
    { fail ("BLIND details is not contained in scroll content"); return true; }
    const auto viewportBounds = editor.getLocalArea(viewport, viewport->getLocalBounds());
    if (!editor.getLocalBounds().contains(viewportBounds) || viewportBounds.isEmpty())
        fail ("BLIND details viewport is outside editor");
    const auto original = viewport->getViewPosition();
    for (const bool bottom : { false, true })
    {
        viewport->setViewPosition(0, bottom ? child.getBottom() - viewport->getViewHeight() : child.getY());
        const auto visible = viewport->getViewArea();
        const auto edge = bottom ? child.getBounds().withTrimmedTop(child.getHeight()-1)
                                 : child.getBounds().withHeight(1);
        if (!visible.contains(edge)) fail ("BLIND details text endpoint is unreachable by scrolling");
        const auto portion = visible.getIntersection(child.getBounds());
        if (portion.isEmpty()) { fail ("BLIND details visible text is empty"); continue; }
        const auto point = editor.getLocalPoint(content, portion.getCentre());
        auto* hit = editor.getComponentAt(point);
        if (hit != &child && !child.isParentOf(hit)) fail ("BLIND details visible text cannot be reached by a press");
    }
    if (!keyboardReaches(child)) fail ("BLIND details is not keyboard reachable");
    if (auto* graph = content->findChildWithID("live-blind-frequency-details"); graph && graph->isVisible())
    {
        if (!content->getLocalBounds().contains(graph->getBounds())) fail ("BLIND frequency row escapes scroll content");
        viewport->setViewPosition(0, graph->getY());
        if (!viewport->getViewArea().contains(graph->getBounds()))
            fail ("BLIND frequency heading, pointer and plot cannot be visible together");
        const auto bounds = editor.getLocalArea(graph, graph->getLocalBounds());
        if (!editor.getLocalBounds().contains(bounds)) fail ("BLIND frequency row escapes editor when scrolled into view");
    }
    viewport->setViewPosition(original);
    return true;
}

// Calls `fail` with a description for each shown control that is cut by the edge of `editor` (the
// editor, or the host window holding it at a display scale) or that a press at its centre does not
// reach. Returns the number of controls checked.
inline int check (juce::Component& editor, const std::function<void (const juce::String&)>& fail, bool blindDetailsScroll = false)
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
            if (blindDetailsScroll && checkBlindDetailsScroll(*child, editor, fail)) continue;
            const auto bounds = editor.getLocalArea (child, child->getLocalBounds());
            const auto name = child->getComponentID().isNotEmpty() ? child->getComponentID() : child->getTitle();
            if (! editor.getLocalBounds().contains (bounds))
                fail ("cut by the editor's edge: " + name + " " + bounds.toString());
            auto* hit = editor.getComponentAt (bounds.getCentre());
            if (hit != child && ! child->isParentOf (hit))
                fail ("a press at its centre does not reach it: " + name + " " + bounds.toString()
                      + (hit != nullptr ? " (reaches " + hit->getComponentID() + " " + hit->getTitle() + " "
                                              + editor.getLocalArea (hit, hit->getLocalBounds()).toString() + " in "
                                              + (hit->getParentComponent() != nullptr
                                                     ? hit->getParentComponent()->getComponentID() + " "
                                                       + editor.getLocalArea (hit->getParentComponent(),
                                                             hit->getParentComponent()->getLocalBounds()).toString()
                                                     : juce::String()) + ")"
                                            : juce::String()));
            if (! keyboardReaches (*child))
                fail ("the keyboard does not reach it: " + name + " " + bounds.toString());
        }
    };
    visit (editor);
    return checked;
}

// A full-window screen (or any surface) at each display scale inside a host window: the host's scale
// transforms the surface, presses land in the window's pixels. Restores the surface afterwards.
inline constexpr float displayScales[] { 1.0f, 1.25f, 1.5f, 2.0f };

inline int checkAtDisplayScales (juce::Component& surface, const std::function<void (float, const juce::String&)>& fail, bool blindDetailsScroll = false)
{
    auto* parent = surface.getParentComponent();
    const auto transform = surface.getTransform();
    if (parent != nullptr) parent->removeChildComponent (&surface);
    juce::Component window;
    window.setVisible (true);
    window.addAndMakeVisible (surface);
    int checked = 0;
    for (const auto scale : displayScales)
    {
        surface.setTransform (juce::AffineTransform::scale (scale));
        const auto scaled = surface.getBoundsInParent();
        window.setSize (scaled.getRight(), scaled.getBottom());
        checked += check (window, [&] (const juce::String& problem) { fail (scale, problem); }, blindDetailsScroll);
    }
    surface.setTransform (transform);
    window.removeChildComponent (&surface);
    if (parent != nullptr) parent->addChildComponent (surface);
    return checked;
}
}
