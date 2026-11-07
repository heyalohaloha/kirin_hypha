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
class AWindowSecondsEntry final : public juce::Component
{
public:
    explicit AWindowSecondsEntry (int seconds)
    {
        label.setText ("SECONDS  3-600 S", juce::dontSendNotification);
        addAndMakeVisible (label);
        input.setComponentID ("a-window-seconds");
        input.setText (juce::String (seconds));
        input.setSelectAllWhenFocused (true);
        input.setTitle ("SECONDS  3-600 S");
        input.onTextChange = [this] { apply.setEnabled (reference_audition::parseAWindowSeconds (input.getText()) != 0); };
        input.onReturnKey = [this] { submit(); };
        input.onEscapeKey = [this] { dismiss(); };
        addAndMakeVisible (input);
        apply.setButtonText ("SET");
        apply.onClick = [this] { submit(); };
        addAndMakeVisible (apply);
        setSize (224, 76);
    }
    std::function<bool (const juce::String&)> onSubmit;
    void visibilityChanged() override { if (isShowing()) input.grabKeyboardFocus(); }
    bool submit()
    {
        if (reference_audition::parseAWindowSeconds (input.getText()) == 0 || ! onSubmit) return false;
        const bool accepted = onSubmit (input.getText());
        if (accepted) dismiss();
        return accepted;
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        label.setBounds (area.removeFromTop (24));
        apply.setBounds (area.removeFromRight (50));
        area.removeFromRight (8);
        input.setBounds (area);
    }
private:
    void dismiss() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
    juce::Label label;
    juce::TextEditor input;
    juce::TextButton apply;
};

class AWindowControl final : public juce::Button,
                             private juce::ComponentMovementWatcher
{
public:
    using Choice = reference_audition::AWindowChoice;
    using Target = reference_audition::AWindowTarget;
    static constexpr int arrowWidth = 12;  // 凡例が矢印のために空ける幅
    static constexpr int resetItem = 100;
    static constexpr int secondsItem = 101;

    AWindowControl() : juce::Button ("A window"), juce::ComponentMovementWatcher (this)
    {
        setTitle (titleFor (choice));
        setTooltip ("Choose how much of A the line shows. The sound and MATCH do not change.");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    ~AWindowControl() override { closeSecondsEntry(); }

    std::function<void (Target, Choice, int)> onChoose;
    std::function<void (Target)> onReset;

    void setChoice (Choice next, int nextSeconds = 10)
    {
        if (choice == next && seconds == nextSeconds) return;
        choice = next;
        seconds = nextSeconds;
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
        menu.addItem (secondsItem, "SECONDS", true, choice == Choice::custom);
        menu.addSeparator();
        menu.addItem (resetItem, "RESET A WINDOW");
        return menu;
    }

    static int itemFor (Choice option) noexcept { return static_cast<int> (option) + 1; }

    // The menu's answer, bound to what the menu was opened for. 0 is a menu closed without a choice.
    void choose (Target opened, int item)
    {
        if (item == resetItem) { if (onReset) onReset (opened); return; }
        if (item == secondsItem)
        {
            if (! isShowing()) return;
            closeSecondsEntry();
            secondsEntry = &juce::CallOutBox::launchAsynchronously (makeSecondsEntry (opened), getScreenBounds(), nullptr);
            return;
        }
        for (const auto option : reference_audition::aWindowOptions)
            if (item == itemFor (option) && onChoose) onChoose (opened, option, seconds);
    }

    std::unique_ptr<AWindowSecondsEntry> makeSecondsEntry (Target opened)
    {
        auto entry = std::make_unique<AWindowSecondsEntry> (seconds);
        const juce::Component::SafePointer<AWindowControl> safe (this);
        entry->onSubmit = [safe, opened] (const juce::String& text)
        {
            const auto value = reference_audition::parseAWindowSeconds (text);
            if (safe == nullptr || value == 0 || ! safe->onChoose) return false;
            safe->onChoose (opened, Choice::custom, value);
            return true;
        };
        return entry;
    }

    void clicked() override
    {
        auto menu = makeMenu();
        menu.setLookAndFeel (&getLookAndFeel());
        const juce::Component::SafePointer<AWindowControl> safe (this);
        const auto opened = target;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                                .withItemThatMustBeVisible (choice == Choice::custom ? secondsItem : itemFor (choice)),
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
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { closeSecondsEntry(); }
    void componentVisibilityChanged() override { if (! isShowing()) closeSecondsEntry(); }
    void closeSecondsEntry()
    {
        if (secondsEntry == nullptr) return;
        // End this modal immediately; JUCE releases its async content on the message loop.
        secondsEntry->exitModalState (0);
        secondsEntry->setVisible (false);
        secondsEntry = nullptr;
    }
    juce::String titleFor (Choice option) const { return juce::String ("A window: ")
        + (option == Choice::custom ? juce::String (seconds) + " S" : juce::String (reference_audition::aWindowLabel (option))); }

    Choice choice = Choice::seconds10;
    int seconds = 10;
    Target target;
    juce::Component::SafePointer<juce::CallOutBox> secondsEntry;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AWindowControl)
};
}
