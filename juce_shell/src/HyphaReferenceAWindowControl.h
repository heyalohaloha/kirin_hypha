#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "reference_audition/ReferenceVisualPreferences.h"

// B・C の図の凡例の A の場所に重ねる、A の窓を選ぶボタン。文字は凡例が描き（A LAST 4 S）、ここは矢印と指したときの
// 枠だけを描く。リストは Reference の選択欄と同じ材料。役ごとの選択は B・C の画面が持ち、ここは今の選択を見せるだけ。
namespace hypha::reference_ui
{
class AWindowControl final : public juce::Button
{
public:
    using Choice = reference_audition::AWindowChoice;
    using Target = reference_audition::AWindowTarget;
    static constexpr int arrowWidth = 12;  // 凡例が矢印のために空ける幅
    static constexpr int resetItem = 100;

    AWindowControl() : juce::Button ("A window")
    {
        setTitle (titleFor (choice));
        setTooltip ("Choose how much of A the line shows. The sound and MATCH do not change.");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (Target, Choice)> onChoose;
    std::function<void (Target)> onReset;

    void setChoice (Choice next)
    {
        if (choice == next) return;
        choice = next;
        setTitle (titleFor (choice));
    }
    Choice chosen() const noexcept { return choice; }
    void setTarget (Target next) noexcept { target = next; }
    Target currentTarget() const noexcept { return target; }

    juce::PopupMenu makeMenu() const
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("A WINDOW");
        for (const auto option : reference_audition::aWindowOptions)
            menu.addItem (itemFor (option), reference_audition::aWindowLabel (option), true, option == choice);
        menu.addSeparator();
        menu.addItem (resetItem, "RESET A WINDOW");
        return menu;
    }

    static int itemFor (Choice option) noexcept { return static_cast<int> (option) + 1; }

    // The menu's answer, bound to what the menu was opened for. 0 is a menu closed without a choice.
    void choose (Target opened, int item)
    {
        if (item == resetItem) { if (onReset) onReset (opened); return; }
        for (const auto option : reference_audition::aWindowOptions)
            if (item == itemFor (option) && onChoose) onChoose (opened, option);
    }

    void clicked() override
    {
        auto menu = makeMenu();
        menu.setLookAndFeel (&getLookAndFeel());
        const juce::Component::SafePointer<AWindowControl> safe (this);
        const auto opened = target;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                                .withItemThatMustBeVisible (itemFor (choice)),
                            [safe, opened] (int item) { if (safe != nullptr) safe->choose (opened, item); });
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        if (highlighted || down)
        {
            g.setColour (COL_FLORA_BR.withAlpha (down ? 0.16f : 0.08f));
            g.fillRoundedRectangle (area, 3.0f);
            g.setColour (COL_FLORA_BR.withAlpha (0.42f));
            g.drawRoundedRectangle (area, 3.0f, 1.0f);
        }
        g.setColour (COL_FLORA_BR.withAlpha (highlighted || down ? 0.96f : 0.74f));
        surface_material::strokeMenuArrow (g, { area.getRight() - 6.0f, area.getCentreY() + 1.75f }, 3.0f, 3.5f, 1.1f);
    }

private:
    static juce::String titleFor (Choice option) { return juce::String ("A window: ") + reference_audition::aWindowLabel (option); }

    Choice choice = Choice::seconds10;
    Target target;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AWindowControl)
};
}
