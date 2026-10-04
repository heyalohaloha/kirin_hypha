#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryResizeContract.h"
#include "HyphaTheme.h"

// The editor's own bottom-right grip (2026-10-04, on both platforms). Studio on
// Windows and Pro Tools give a plug-in window no frame to drag: Studio's frame corner lies under
// the plug-in's window, so only a grip the plug-in draws can be held there. Dragging it goes
// through the editor's size rule (EditorSizeConstrainer: 3:2, then the magnified steps), and the
// editor's new size reaches the host the same way as a choice from the size menu.
namespace hypha
{
class EditorResizeGrip final : public juce::ResizableCornerComponent
{
public:
    EditorResizeGrip (juce::Component* editor, juce::ComponentBoundsConstrainer* rule)
        : juce::ResizableCornerComponent (editor, rule)
    {
        setAlwaysOnTop (true);
        setComponentID ("editor-resize-grip");
    }

    // The corner the layouts leave free, inside the frame line 1 px from the edge: the nearest
    // content sits 7 px from the corner at 100 %, 9 px at 125 % and 150 % (the size button), 12 px
    // at 200 % and 15 px at 300 %. Two strokes run across the corner where the distances to the
    // right and bottom edges add up to 12 and 16 px (9 and 12 px at 100 %), so each keeps at least
    // 1.4 px from that content and 2.8 px from the rounded frame line (a shorter third stroke merged
    // with that rounded corner on a Windows display at 125 %). The grip takes the drag where those
    // distances add up to about 1.25 x side - 2: the strokes, never the size button. A magnified
    // editor scales the 300 % corner with its layout.
    struct Geometry
    {
        int side = 15;
        float scale = 1.0f;
        float firstStroke = 12.0f, secondStroke = 16.0f;
    };

    static Geometry geometryFor (int editorWidth, int editorHeight) noexcept
    {
        const auto viewport = observatory::displayViewport (editorWidth, editorHeight);
        switch (observatory::densityForWidth (viewport.width))
        {
            case observatory::Density::compact:    return { 12, 1.0f, 9.0f, 12.0f };
            case observatory::Density::inspection: return { juce::roundToInt (16.0f * viewport.scale), viewport.scale, 12.0f, 16.0f };
            case observatory::Density::focused:
            case observatory::Density::standard:
            case observatory::Density::observatory: break;
        }
        return {};
    }

    void place (juce::Rectangle<int> editorBounds)
    {
        geometry = geometryFor (editorBounds.getWidth(), editorBounds.getHeight());
        setBounds (editorBounds.removeFromBottom (geometry.side).removeFromRight (geometry.side));
        toFront (false);
    }

    void paint (juce::Graphics& g) override
    {
        // Non-text geometry rests in COL_MUTED and lifts to the secondary tier under the pointer.
        g.setColour (isMouseOverOrDragging() ? COL_TEXT_SECONDARY : COL_MUTED);
        const auto s = (float) getWidth();
        const auto k = geometry.scale;
        for (const auto sum : { geometry.firstStroke, geometry.secondStroke })
            g.drawLine (s - 3.0f * k, s - (sum - 3.0f) * k, s - (sum - 3.0f) * k, s - 3.0f * k, k);
    }

private:
    Geometry geometry;
};
}
