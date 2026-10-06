#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// C の画面の上段の Check のタブ。順番は CHECK セットのまま。タブを押すとその Check を選ぶ
// （曲はできるだけ同じ曲のまま）。色で採点しない。1 段に入りきらなければ、高さが 2 段ぶんあれば 2 段に分ける
// （2026-10-05。300% で 8 項目の名前が「音色…」「セク…」と切れていた）。
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
    // その幅で名前を切らずに並べるのに要る段の数（1 か 2）。
    int rowsFor (int width) const;
    const std::vector<Tab>& tabs() const noexcept { return items; }
    const juce::String& selected() const noexcept { return selectedId; }
    // 試験・アクセシビリティ用：タブ index の枠（右端の位置表示を除く）。
    juce::Rectangle<int> tabBounds (size_t index) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<juce::Rectangle<int>> layoutTabs() const;
    std::vector<int> naturalWidths() const;
    std::vector<Tab> items;
    juce::String selectedId;
    presentation::Context context = presentation::defaultContext();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CheckTabs)
};
}
