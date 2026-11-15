#pragma once

#include "HyphaTextStyle.h"
#include "HyphaLanguage.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace hypha::live_compare_ui
{
// A choice inside the editor, without a modal window: an outside press is delivered to its target.
class MatchChoicePopup final : public juce::Component, private juce::ComponentListener
{
    class ChoiceButton final : public juce::Button
    {
    public:
        ChoiceButton() : juce::Button (juce::String {}) { setWantsKeyboardFocus (true); }
        void setText (juce::String next, presentation::Context nextContext)
        {
            text = std::move (next); context = nextContext;
            setButtonText (text);
            setTitle (text_style::shownText (text));
            setDescription (text_style::shownText (text)); setTooltip (text);
        }
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            g.fillAll (highlighted || down ? kFieldFill : BG);
            g.setColour (COL_TEXT_SECONDARY);
            g.drawRect (getLocalBounds(), 1);
            g.setColour (COL_NORMAL);
            g.setFont (labelFont (context, typography::TextRole::listenChoice, typography::Composition::information));
            text_style::drawLines (g, text, getLocalBounds().reduced (8, 4), juce::Justification::centredLeft, 4);
        }
        juce::String text;
        presentation::Context context;
    };

public:
    MatchChoicePopup()
    {
        setComponentID ("listen-match-choice");
        addAndMakeVisible (first); addAndMakeVisible (second);
        first.setComponentID ("listen-match-lower"); second.setComponentID ("listen-match-limit");
        first.onClick = [this] { choose (1); };
        second.onClick = [this] { choose (2); };
        setVisible (false);
    }
    ~MatchChoicePopup() override { close(); }

    void open (juce::Component& parent, juce::Rectangle<int> anchor,
               juce::String heading, juce::String lower, juce::String limit,
               presentation::Context shownContext, std::function<void (int)> action)
    {
        close();
        context = shownContext;
        owner = &parent;
        callback = std::move (action);
        title = std::move (heading);
        font = labelFont (context, typography::TextRole::listenChoice, typography::Composition::information);
        first.setText (std::move (lower), context);
        second.setText (std::move (limit), context);
        const int width = juce::jmax (1, juce::jmin (520, parent.getWidth() - 12));
        const auto heightOf = [&] (const juce::String& text, int inset)
        { return static_cast<int> (std::ceil (text_style::shownWrappedHeight (text, font, juce::jmax (1, width - inset)))); };
        const int titleHeight = heightOf (title, 16) + 8;
        const int firstHeight = juce::jmax (30, heightOf (first.text, 28) + 8);
        const int secondHeight = juce::jmax (30, heightOf (second.text, 28) + 8);
        const int height = titleHeight + firstHeight + secondHeight + 12;
        const int x = juce::jlimit (0, juce::jmax (0, parent.getWidth() - width), anchor.getCentreX() - width / 2);
        const int y = anchor.getY() >= height + 4 ? anchor.getY() - height - 4
            : juce::jlimit (0, juce::jmax (0, parent.getHeight() - height), anchor.getBottom() + 4);
        setBounds (x, y, width, height);
        headingBounds = { 8, 4, width - 16, titleHeight - 8 };
        first.setBounds (6, titleHeight, width - 12, firstHeight);
        second.setBounds (6, titleHeight + firstHeight + 4, width - 12, secondHeight);
        setTitle (text_style::shownText (title));
        parent.addAndMakeVisible (*this);
        parent.addMouseListener (this, true);
        parent.addComponentListener (this);
        toFront (false);
    }

    void close()
    {
        setVisible (false);
        if (owner != nullptr)
        {
            owner->removeMouseListener (this);
            owner->removeComponentListener (this);
            owner = nullptr;
        }
        callback = {};
    }

    void dismissIfOutside (const juce::Component* source)
    {
        if (source != this && (source == nullptr || ! isParentOf (source))) close();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (BG);
        g.setColour (COL_TEXT_SECONDARY);
        g.drawRect (getLocalBounds(), 1);
        g.setColour (COL_NORMAL);
        g.setFont (labelFont (context, typography::TextRole::listenChoice, typography::Composition::information));
        text_style::drawLines (g, title, headingBounds, juce::Justification::centredLeft, 4);
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! key.isKeyCode (juce::KeyPress::escapeKey)) return false;
        close(); return true;
    }
    const juce::Font& bodyFont() const noexcept { return font; }
    juce::Rectangle<int> titleBounds() const noexcept { return headingBounds; }
    juce::Button& option (int index) noexcept { return index == 1 ? first : second; }

private:
    void choose (int index)
    {
        auto action = std::move (callback);
        close();
        if (action) action (index);
    }
    void mouseDown (const juce::MouseEvent& event) override { dismissIfOutside (event.originalComponent); }
    void componentMovedOrResized (juce::Component&, bool, bool) override { close(); }
    juce::Component::SafePointer<juce::Component> owner;
    std::function<void (int)> callback;
    juce::String title;
    presentation::Context context;
    juce::Font font;
    juce::Rectangle<int> headingBounds;
    ChoiceButton first, second;
};
}
