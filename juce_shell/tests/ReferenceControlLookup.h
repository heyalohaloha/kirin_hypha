#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 2026-10-04：REF の状態の行（承認・VERSION BLIND のボタン）は StatusStrip の子になった（足元の段がある大きさでは
// エディターが足元へ移す）。試験はボタンを REF の子の中まで探し、位置と押せるかどうかは REF の座標で確かめる。
namespace hypha::tests
{
inline juce::Component* findReferenceControl (juce::Component& root, const juce::String& id)
{
    if (auto* direct = root.findChildWithID (id)) return direct;
    for (auto* child : root.getChildren())
        if (auto* found = findReferenceControl (*child, id)) return found;
    return nullptr;
}

inline juce::Rectangle<int> boundsWithin (juce::Component& root, juce::Component& control)
{
    return root.getLocalArea (&control, control.getLocalBounds());
}
}
