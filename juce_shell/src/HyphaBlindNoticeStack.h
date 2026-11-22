#pragma once

#include "HyphaTextButton.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaTooltipLookAndFeel.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace hypha::blind_ui::notice_stack
{
struct Note
{
    int priority;
    juce::String text;
    bool guidance = false;
};

inline std::vector<Note> ordered (const juce::String& trial, const juce::String& cause,
                                  const juce::String& recovery, const juce::String& extra)
{
    std::vector<Note> notes;
    if (const auto guidance = cause + (cause.isNotEmpty() && recovery.isNotEmpty() ? "\n" : "") + recovery;
        guidance.isNotEmpty())
        notes.push_back ({ cause == live_compare_ui::preProcessingCause ? 0 : 4, guidance, true });
    juce::StringArray lines;
    lines.addLines (trial);
    for (const auto& line : lines)
    {
        if (line.trim().isEmpty()) continue;
        const auto priority = line == live_compare_ui::unappliedNotice() ? 1
            : line == "Repeated audio: checked per block" ? 2
            : line.contains (live_compare_ui::unmeasuredNotice()) || line == live_compare_ui::notRemeasuredNotice() ? 3 : 4;
        notes.push_back ({ priority, line });
    }
    if (extra.isNotEmpty() && std::none_of (notes.begin(), notes.end(), [&] (const auto& item)
        { return item.text == extra; }))
        notes.push_back ({ 5, extra });
    std::stable_sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.priority < b.priority; });
    return notes;
}

// This is an owned disclosure, not a stock tooltip timed against the mouse.
class More final : public HyphaTextButton
{
public:
    More() : HyphaTextButton ("", true, text_style::LabelPolicy::fixed)
    {
        onClick = [this] { reveal(); };
    }

    void attachDisclosure (juce::Component& surface)
    {
        panel.setComponentID (getComponentID() + "-disclosure");
        surface.addChildComponent (panel);
    }

    void setNotices (const juce::StringArray& raw)
    {
        juce::StringArray localized;
        for (const auto& line : raw) localized.add (text_style::shownText (line));
        const auto next = localized.joinIntoString ("\n");
        if (panel.getName() != next)
        {
            panel.setName (next);
            panel.repaint();
        }
        setButtonText (raw.isEmpty() ? juce::String() : "+" + juce::String (raw.size()));
        setTitle (getButtonText());
        setDescription (next);
        setVisible (!raw.isEmpty());
        if (raw.isEmpty()) dismiss();
        else if (panel.isVisible()) positionDisclosure();
    }

    void dismiss() { panel.setVisible (false); }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        HyphaTextButton::mouseEnter (event);
        reveal();
    }
    void mouseExit (const juce::MouseEvent& event) override
    {
        HyphaTextButton::mouseExit (event);
        if (!hasKeyboardFocus (true)) dismiss();
    }
    void focusGained (FocusChangeType cause) override
    {
        HyphaTextButton::focusGained (cause);
        reveal();
    }
    void focusLost (FocusChangeType cause) override
    {
        HyphaTextButton::focusLost (cause);
        dismiss();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (panel.isShowing() && key.isKeyCode (juce::KeyPress::escapeKey)
            && !key.getModifiers().isAnyModifierKeyDown())
        {
            dismiss();
            return true;
        }
        return HyphaTextButton::keyPressed (key);
    }
    void moved() override { if (panel.isVisible()) positionDisclosure(); }
    void resized() override { if (panel.isVisible()) positionDisclosure(); }

private:
    class DisclosureAccessibility final : public juce::AccessibilityHandler
    {
    public:
        explicit DisclosureAccessibility (More& badge)
            : juce::AccessibilityHandler (badge, juce::AccessibilityRole::button,
                juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press,
                                                        [&badge] { badge.triggerClick(); }))
        {
        }
        juce::String getHelp() const override { return getDescription(); }
    };
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<DisclosureAccessibility> (*this);
    }

    void reveal()
    {
        if (panel.getName().isEmpty() || !isShowing() || panel.getParentComponent() == nullptr) return;
        positionDisclosure();
        panel.setVisible (true);
        panel.toFront (false);
    }
    void positionDisclosure()
    {
        if (const auto* surface = panel.getParentComponent())
            panel.setBounds (panel.look.getTooltipBounds (panel.getName(), getBounds().getCentre(),
                                                          surface->getLocalBounds()));
    }

    struct Disclosure final : juce::Component
    {
        Disclosure()
        {
            setInterceptsMouseClicks (false, false);
            setAccessible (false);  // The badge already exposes the complete text to readers.
        }
        void paint (juce::Graphics& g) override { look.drawTooltip (g, getName(), getWidth(), getHeight()); }
        TooltipLookAndFeel look;
    } panel;
};
}
