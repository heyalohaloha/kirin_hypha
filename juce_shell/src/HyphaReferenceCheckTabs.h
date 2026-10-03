#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// H12: C の画面の上段の Check のタブ（方向設計 §4）。順番は CHECK セットのまま。右端に「2 / 5」で
// 今のタブの位置だけを示す。タブを押すとその Check を選ぶ（曲はできるだけ同じ曲のまま）。色で採点しない。
namespace hypha::reference_ui
{
class CheckTabs final : public juce::Component
{
public:
    struct Tab
    {
        juce::String id, label;
    };

    CheckTabs();
    std::function<void (const juce::String&)> onChoose;
    void setTabs (std::vector<Tab>, const juce::String& selectedId, presentation::Context);
    const std::vector<Tab>& tabs() const noexcept { return items; }
    const juce::String& selected() const noexcept { return selectedId; }
    // 試験・アクセシビリティ用：タブ index の枠（右端の位置表示を除く）。
    juce::Rectangle<int> tabBounds (size_t index) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<juce::Rectangle<int>> layoutTabs() const;
    std::vector<Tab> items;
    juce::String selectedId;
    presentation::Context context = presentation::defaultContext();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CheckTabs)
};
}
