#include "HyphaBlindScreen.h"
#include "HyphaLanguage.h"
#include "HyphaSurfaceMaterial.h"

#include <tuple>

namespace hypha::blind_ui
{
bool Screen::operator== (const Screen& other) const noexcept
{
    const auto key = [] (const Screen& screen)
    {
        return std::tie (screen.title, screen.instruction, screen.detail, screen.guidanceShown, screen.cause, screen.recovery,
                         screen.sourcesShown, screen.sourceOneEnabled, screen.sourceTwoEnabled, screen.audible,
                         screen.sourceOne, screen.sourceTwo, screen.revealShown, screen.revealEnabled, screen.revealTitle,
                         screen.approveShown, screen.approveText, screen.approveTitle, screen.approveDescription,
                         screen.endEnabled, screen.endTitle);
    };
    return key (*this) == key (other);
}

ScreenComponent::ScreenComponent (const juce::String& idPrefix)
{
    setComponentID (idPrefix + "-screen");
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setLookAndFeel (&look);
    int index = 0;
    for (auto* label : { &title, &status, &detail, &cause, &recovery })
    {
        label->setComponentID (idPrefix + "-text-" + juce::String (++index));
        label->setJustificationType (juce::Justification::centred);
        label->setMinimumHorizontalScale (1.0f);
        label->setColour (juce::Label::textColourId, COL_NORMAL);
        addAndMakeVisible (*label);
    }
    index = 0;
    for (auto* button : { &one, &two, &reveal, &end, &approve })
    {
        const char* ids[] { "source-1", "source-2", "reveal", "end", "approve" };
        button->setComponentID (idPrefix + "-" + juce::String (ids[index++]));
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button->setWantsKeyboardFocus (true);
        addAndMakeVisible (*button);
    }
    one.onClick = [this] { if (onSelect) onSelect (1); };
    two.onClick = [this] { if (onSelect) onSelect (2); };
    reveal.onClick = [this] { if (onReveal) onReveal(); };
    end.onClick = [this] { if (onEnd) onEnd(); };
    approve.onClick = [this] { if (onApprove) onApprove(); };
    apply();
}

ScreenComponent::~ScreenComponent() { setLookAndFeel (nullptr); }

void ScreenComponent::setScreen (const Screen& next)
{
    const bool languageChanged = languageRevision != i18n::revision();
    if (! languageChanged && next == shown) return;
    languageRevision = i18n::revision();
    shown = next;
    apply();
    if (languageChanged) resized();
}

void ScreenComponent::apply()
{
    title.setText (shown.title, juce::dontSendNotification);
    status.setText (shown.instruction, juce::dontSendNotification);
    detail.setText (shown.detail, juce::dontSendNotification);
    cause.setText (shown.cause, juce::dontSendNotification);
    recovery.setText (shown.recovery, juce::dontSendNotification);
    cause.setVisible (shown.guidanceShown);
    recovery.setVisible (shown.guidanceShown);
    one.setButtonText (shown.sourceOne);
    two.setButtonText (shown.sourceTwo);
    one.setToggleState (shown.sourcesShown && shown.audible == 1, juce::dontSendNotification);
    two.setToggleState (shown.sourcesShown && shown.audible == 2, juce::dontSendNotification);
    for (auto [button, enabled] : { std::pair { &one, shown.sourceOneEnabled }, std::pair { &two, shown.sourceTwoEnabled } })
    {
        button->setVisible (shown.sourcesShown);
        button->setEnabled (shown.sourcesShown && enabled);
        button->setTitle (button->getButtonText());
        button->setDescription (button->getButtonText());
        button->setTooltip (button->getButtonText());
    }
    reveal.setVisible (shown.revealShown);
    reveal.setEnabled (shown.revealShown && shown.revealEnabled);
    reveal.setTitle (shown.revealTitle);
    reveal.setDescription (shown.revealTitle);
    reveal.setTooltip (shown.revealTitle);
    approve.setVisible (shown.approveShown);
    approve.setButtonText (shown.approveText);
    approve.setTitle (shown.approveTitle);
    approve.setDescription (shown.approveDescription);
    end.setEnabled (shown.endEnabled);
    end.setTitle (shown.endTitle);
    end.setDescription (shown.endTitle + " / " + shown.detail);
    end.setTooltip (end.getDescription());
    repaint();
}

void ScreenComponent::resized()
{
    context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    for (auto* button : { &one, &two, &reveal, &end, &approve }) button->setPresentationContext (context);
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    status.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    detail.setFont (labelFont (context, typography::TextRole::status, typography::Composition::information));
    cause.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    recovery.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    auto area = getLocalBounds().reduced (compact ? 12 : 24);
    title.setBounds (area.removeFromTop (compact ? 24 : 40));
    status.setBounds (area.removeFromTop (compact ? 30 : 48));
    detail.setBounds (area.removeFromBottom (compact ? 34 : 46));
    auto actions = area.removeFromBottom (compact ? 28 : 40);
    end.setBounds (actions.removeFromRight (actions.getWidth() / 2).reduced (3, 0));
    reveal.setBounds (actions.reduced (3, 0));
    area.reduce (0, compact ? 5 : 12);
    approve.setBounds (area);
    auto guidance = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 90));
    cause.setBounds (guidance.removeFromTop (guidance.getHeight() / 2));
    recovery.setBounds (guidance);
    one.setBounds (area.removeFromLeft (area.getWidth() / 2).reduced (3, 0));
    two.setBounds (area.reduced (3, 0));
}

void ScreenComponent::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    surface_material::paintInstrumentFrame (g, getLocalBounds().toFloat(), false);
}
}
