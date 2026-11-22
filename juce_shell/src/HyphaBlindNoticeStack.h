#pragma once

#include "HyphaTextButton.h"
#include "HyphaLiveCompareRecoveryText.h"

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

// The badge exposes the hidden *full* text, not the count alone. The native tooltip
// opens on pointer entry, keyboard focus, or activation without spending a layout row.
class More final : public HyphaTextButton
{
public:
    More() : HyphaTextButton ("", true, text_style::LabelPolicy::fixed)
    {
        onClick = [this] { reveal(); };
    }

    void setNotices (const juce::StringArray& raw)
    {
        juce::StringArray localized;
        for (const auto& line : raw) localized.add (text_style::shownText (line));
        const auto next = localized.joinIntoString ("\n");
        const bool changed = next != fullText;
        fullText = next;
        setButtonText (raw.isEmpty() ? juce::String() : "+" + juce::String (raw.size()));
        setTitle (getButtonText());
        setDescription (fullText);
        setTooltip (fullText);
        setVisible (raw.size() > 0);
        if (!isVisible()) hide();
        else if (changed && tip) reveal();
    }

    void mouseEnter (const juce::MouseEvent& event) override
    {
        HyphaTextButton::mouseEnter (event);
        reveal();
    }
    void mouseExit (const juce::MouseEvent& event) override
    {
        HyphaTextButton::mouseExit (event);
        if (!hasKeyboardFocus (true)) hide();
    }
    void focusGained (FocusChangeType cause) override
    {
        HyphaTextButton::focusGained (cause);
        reveal();
    }
    void focusLost (FocusChangeType cause) override
    {
        HyphaTextButton::focusLost (cause);
        if (!isMouseOver()) hide();
    }

private:
    void reveal()
    {
        if (fullText.isEmpty() || !isShowing() || getPeer() == nullptr) return;
        if (!tip) tip = std::make_unique<juce::TooltipWindow> (nullptr, -1);
        tip->displayTip (getScreenBounds().getBottomLeft(), fullText);
    }
    void hide()
    {
        if (tip) tip->hideTip();
        tip.reset();
    }

    juce::String fullText;
    std::unique_ptr<juce::TooltipWindow> tip;
};
}
