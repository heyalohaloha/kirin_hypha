#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaComparisonSurfaceMaterial.h"
#include "HyphaSelectMenu.h"
#include "HyphaTextStyle.h"

namespace hypha
{
// JUCE's own labels, text buttons, combo boxes and popup menus draw their text inside the
// LookAndFeel, past text_style. This one shows that text in the current language too (INV-S40).
// English goes through JUCE's drawing unchanged; Japanese labels and buttons are drawn in the
// native font at the height the component chose, wrapped instead of compressed. Menus are built in
// English and each item is measured and drawn translated, in the menu font, which a LookAndFeel
// that shows Japanese sets to the native one. The components keep their English text.
class TextLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // A transparent menu background lets JUCE give popups a transparent window where the platform
    // allows it, so the Kirin Select window keeps its rounded corners (HyphaSelectMenu.h).
    TextLookAndFeel() { setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack); }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        select_menu::paintWindow (g, { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) },
                                  select_menu::roundedWindow (*this));
    }

    int getPopupMenuBorderSize() override { return select_menu::border; }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable*,
                            const juce::Colour* textColour) override
    {
        select_menu::Row row;
        row.separator = isSeparator;
        row.active = isActive;
        row.pointed = isHighlighted;
        row.chosen = isTicked;
        row.submenu = hasSubMenu;
        row.text = text; // text_style::drawText shows it in the current language
        row.shortcut = shortcutKeyText;
        row.colour = textColour;
        select_menu::paintRow (g, area, row, getPopupMenuFont());
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override
    {
        select_menu::paintHeader (g, area, text_style::shownText (sectionName));
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight, int& idealWidth,
                                    int& idealHeight) override
    {
        select_menu::idealRowSize (text_style::shownText (text), isSeparator, standardMenuItemHeight,
                                   getPopupMenuFont(), idealWidth, idealHeight);
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.isBeingEdited()
            || ! requiresJapaneseGlyphs (text_style::shownText (label.getText())))
        {
            juce::LookAndFeel_V4::drawLabel (g, label);
            return;
        }
        g.fillAll (label.findColour (juce::Label::backgroundColourId));
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto height = getLabelFont (label).getHeight();
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (nativeTextFontLike (getLabelFont (label)));
        const auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        text_style::drawLines (g, label.getText(), area, label.getJustificationType(),
                               juce::jmax (1, (int) ((float) area.getHeight() / height)));
        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
        g.drawRect (label.getLocalBounds());
    }

    int getTextButtonWidthToFitText (juce::TextButton& button, int buttonHeight) override
    {
        const auto shown = text_style::shownText (button.getButtonText());
        if (! requiresJapaneseGlyphs (shown))
            return juce::LookAndFeel_V4::getTextButtonWidthToFitText (button, buttonHeight);
        return nativeTextFontLike (getTextButtonFont (button, buttonHeight)).getStringWidth (shown)
             + buttonHeight;
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool highlighted, bool down) override
    {
        if (! requiresJapaneseGlyphs (text_style::shownText (button.getButtonText())))
        {
            juce::LookAndFeel_V4::drawButtonText (g, button, highlighted, down);
            return;
        }
        // The geometry of LookAndFeel_V2::drawButtonText, with two lines at most as there.
        const auto height = getTextButtonFont (button, button.getHeight()).getHeight();
        g.setFont (nativeTextFontLike (getTextButtonFont (button, button.getHeight())));
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        const auto yIndent = juce::jmin (4, button.proportionOfHeight (0.3f));
        const auto cornerSize = juce::jmin (button.getHeight(), button.getWidth()) / 2;
        const auto indent = juce::roundToInt (height * 0.6f);
        const auto leftIndent = juce::jmin (indent, 2 + cornerSize / (button.isConnectedOnLeft() ? 4 : 2));
        const auto rightIndent = juce::jmin (indent, 2 + cornerSize / (button.isConnectedOnRight() ? 4 : 2));
        const auto textWidth = button.getWidth() - leftIndent - rightIndent;
        if (textWidth > 0)
            text_style::drawLines (g, button.getButtonText(),
                                   { leftIndent, yIndent, textWidth, button.getHeight() - yIndent * 2 },
                                   juce::Justification::centred, 2);
    }

    // A combo box's "nothing selected" text ("Choose Version") is drawn here, not by its label:
    // English as JUCE draws it, Japanese with LookAndFeel_V2's geometry and colour.
    void drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box,
                                              juce::Label& label) override
    {
        if (! requiresJapaneseGlyphs (text_style::shownText (box.getTextWhenNothingSelected())))
        {
            juce::LookAndFeel_V4::drawComboBoxTextWhenNothingSelected (g, box, label);
            return;
        }
        g.setColour (findColour (juce::ComboBox::textColourId).withMultipliedAlpha (0.5f));
        g.setFont (nativeTextFontLike (label.getLookAndFeel().getLabelFont (label)));
        text_style::drawText (g, box.getTextWhenNothingSelected(),
                              getLabelBorderSize (label).subtractedFrom (label.getBounds()),
                              label.getJustificationType());
    }
};
}
