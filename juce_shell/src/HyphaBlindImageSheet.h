#pragma once

#include <functional>

#include "HyphaBlindResultImage.h"
#include "HyphaComparisonSurfaceMaterial.h"
#include "HyphaTextButton.h"
#include "HyphaTextLookAndFeel.h"

// SAVE IMAGE: over the BLIND RESULT, the image as it will be saved beside the two names it shows.
// The names start as the ones used last (A and B, or PRE and POST); an empty field uses that default.
namespace hypha::blind_image
{
class Sheet final : public juce::Component
{
public:
    explicit Sheet (const juce::String& idPrefix)
    {
        setComponentID (idPrefix + "-image-sheet");
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        setLookAndFeel (&look);
        int index = 0;
        for (auto* label : { &heading, &labelA, &labelB, &reason })
        {
            const char* ids[] { "heading", "name-a-label", "name-b-label", "reason" };
            label->setComponentID (idPrefix + "-image-" + ids[index++]);
            label->setMinimumHorizontalScale (1.0f);
            label->setColour (juce::Label::textColourId, COL_NORMAL);
            addAndMakeVisible (*label);
        }
        heading.setText ("SAVE IMAGE", juce::dontSendNotification);
        reason.setJustificationType (juce::Justification::topLeft);
        index = 0;
        for (auto* field : { &fieldA, &fieldB })
        {
            field->setComponentID (idPrefix + (index++ == 0 ? "-image-name-a" : "-image-name-b"));
            field->setMultiLine (false);
            field->setReturnKeyStartsNewLine (false);
            field->setInputRestrictions (nameLimit);
            field->setColour (juce::TextEditor::backgroundColourId, kFieldFill);
            field->setColour (juce::TextEditor::textColourId, COL_FLORA);
            field->setColour (juce::TextEditor::outlineColourId, COL_MUTED);
            field->setColour (juce::TextEditor::focusedOutlineColourId, COL_FLORA);
            field->setColour (juce::CaretComponent::caretColourId, COL_FLORA);
            field->onTextChange = [this] { refreshPreview(); };
            field->onReturnKey = [this] { if (onSave) onSave(); };
            field->onEscapeKey = [this] { if (onBack) onBack(); };
            addAndMakeVisible (*field);
        }
        for (auto [button, id, title] : { std::tuple { &save, "save", "Choose where to save the image" },
                                          std::tuple { &back, "back", "Back to the result" } })
        {
            button->setComponentID (idPrefix + "-image-" + id);
            button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            button->setWantsKeyboardFocus (true);
            button->setTitle (title);
            button->setDescription (title);
            button->setTooltip (title);
            addAndMakeVisible (*button);
        }
        save.onClick = [this] { if (onSave) onSave(); };
        back.onClick = [this] { if (onBack) onBack(); };
    }
    ~Sheet() override { setLookAndFeel (nullptr); }

    std::function<void()> onSave, onBack;

    // Shows `result` with its names in the fields; the reason line starts empty.
    void open (const Facts& result)
    {
        frozen = result;
        for (auto [field, label, b] : { std::tuple { &fieldA, &labelA, false }, std::tuple { &fieldB, &labelB, true } })
        {
            const auto fallback = defaultName (frozen.candidate, b);
            label->setText (fallback, juce::dontSendNotification);
            field->setText (b ? frozen.nameB : frozen.nameA, juce::dontSendNotification);
            field->setTextToShowWhenEmpty (fallback, COL_TEXT_SECONDARY);
            const auto help = "Name of " + fallback + " in the image, up to 16 characters";
            field->setTitle (help);
            field->setDescription (help);
            field->setTooltip (help);
        }
        setReason ({});
        refreshPreview();
    }

    // The frozen result with the names as they are typed now.
    Facts facts() const
    {
        auto result = frozen;
        result.nameA = cleanName (fieldA.getText(), defaultName (result.candidate, false));
        result.nameB = cleanName (fieldB.getText(), defaultName (result.candidate, true));
        return result;
    }

    void setReason (const juce::String& text)
    {
        reason.setText (text, juce::dontSendNotification);
        reason.setVisible (text.isNotEmpty());
    }
    juce::String reasonShown() const { return reason.getText(); }
    juce::TextEditor& nameField (bool b) noexcept { return b ? fieldB : fieldA; }
    juce::Rectangle<int> previewBounds() const noexcept { return previewArea; }

    void paint (juce::Graphics& g) override
    {
        comparison_surface::paintScreen (g, *this);
        if (previewArea.isEmpty() || ! preview.isValid()) return;
        surface_material::paintPanel (g, previewArea.toFloat().expanded (3.0f), 0.96f, 5.0f, true);
        g.drawImage (preview, previewArea.toFloat());
    }

    void resized() override
    {
        const auto context = presentation::forEditor (getWidth(), getHeight());
        const bool compact = getWidth() < 450;
        for (auto* button : { &save, &back }) button->setPresentationContext (context);
        heading.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
        for (auto* label : { &labelA, &labelB })
            label->setFont (labelFont (context, typography::TextRole::action, typography::Composition::information));
        reason.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
        const auto fieldFont = nativeTextFont (context, typography::TextRole::selector);
        for (auto* field : { &fieldA, &fieldB })
        {
            field->setFont (nativeTextFont (context, typography::TextRole::selector));
            field->applyFontToAllText (fieldFont);
        }
        auto area = getLocalBounds().reduced (compact ? 18 : 30);
        const auto side = juce::jmin (area.getHeight(), juce::roundToInt ((float) area.getWidth() * 0.45f));
        previewArea = area.removeFromLeft (side).withSizeKeepingCentre (side, side);
        area.removeFromLeft (compact ? 12 : 24);
        const auto row = juce::jlimit (24, 52, juce::roundToInt ((float) getHeight() * 0.11f));
        const auto gap = compact ? 6 : 10;
        heading.setBounds (area.removeFromTop (row));
        area.removeFromTop (gap);
        const auto labelWidth = juce::roundToInt (std::ceil (juce::jmax (text_style::shownWidth (labelA.getFont(), "POST"),
                                                                        text_style::shownWidth (labelA.getFont(), labelA.getText()))))
                              + labelA.getBorderSize().getLeftAndRight() + 4;
        for (auto [label, field] : { std::pair { &labelA, &fieldA }, std::pair { &labelB, &fieldB } })
        {
            auto line = area.removeFromTop (row);
            label->setBounds (line.removeFromLeft (labelWidth));
            field->setBounds (line);
            field->setIndents (6, juce::jmax (0, (row - juce::roundToInt (fieldFont.getHeight())) / 2));
            area.removeFromTop (gap);
        }
        auto actions = area.removeFromBottom (row);
        back.setBounds (actions.removeFromRight (actions.getWidth() / 2).reduced (3, 0));
        save.setBounds (actions.reduced (3, 0));
        reason.setBounds (area.reduced (0, gap / 2));
        refreshPreview();
    }

private:
    void refreshPreview()
    {
        if (previewArea.isEmpty()) return;
        // Drawn at twice its place so it stays sharp on a high-density display.
        preview = render (facts(), previewArea.getWidth() * 2);
        repaint();
    }

    TextLookAndFeel look;
    Facts frozen;
    juce::Label heading, labelA, labelB, reason;
    juce::TextEditor fieldA, fieldB;
    HyphaTextButton save { "SAVE" }, back { "BACK" };
    juce::Rectangle<int> previewArea;
    juce::Image preview;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sheet)
};
}
