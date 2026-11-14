#include "HyphaBlindScreen.h"
#include "HyphaLanguage.h"
#include "HyphaComparisonSurfaceMaterial.h"
#include "HyphaUnavailableReason.h"

#include <tuple>

namespace hypha::blind_ui
{
bool Screen::operator== (const Screen& other) const noexcept
{
    const auto key = [] (const Screen& screen)
    {
        return std::tie (screen.title, screen.instruction, screen.detail, screen.guidanceShown, screen.cause, screen.recovery,
                         screen.sourcesShown, screen.sourceOneEnabled, screen.sourceTwoEnabled, screen.audible,
                         screen.sourceOne, screen.sourceTwo, screen.sourceReason, screen.revealShown, screen.revealEnabled, screen.revealTitle, screen.alignOffShown,
                         screen.approveShown, screen.approveText, screen.approveTitle, screen.approveDescription,
                         screen.endEnabled, screen.endTitle, screen.guessShown, screen.guessOpen, screen.guessRight,
                         screen.guessAnswer, screen.guessQuestion, screen.guessOutcome, screen.saveImageShown);
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
    question.setComponentID (idPrefix + "-guess-question");
    outcome.setComponentID (idPrefix + "-guess-outcome");
    for (auto* label : { &question, &outcome })
    {
        label->setMinimumHorizontalScale (1.0f);
        label->setBorderSize ({ 0, 4, 0, 4 });  // one line in the row: its height goes to the text
        label->setColour (juce::Label::textColourId, COL_NORMAL);
        addChildComponent (*label);
    }
    question.setJustificationType (juce::Justification::centredRight);
    outcome.setJustificationType (juce::Justification::centredLeft);
    index = 0;
    for (auto* button : { &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
    {
        const char* ids[] { "source-1", "source-2", "reveal", "end", "approve", "align-off", "guess-1", "guess-2", "save-image" };
        button->setComponentID (idPrefix + "-" + juce::String (ids[index++]));
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button->setWantsKeyboardFocus (true);
        addAndMakeVisible (*button);
    }
    one.onClick = [this] { requestSource (1); };
    two.onClick = [this] { requestSource (2); };
    reveal.onClick = [this] { requestReveal(); };
    end.onClick = [this] { requestEnd(); };
    approve.onClick = [this] { if (onApprove) onApprove(); };
    alignOff.onClick = [this] { if (onAlignOff) onAlignOff(); };
    guessOne.onClick = [this] { if (shown.guessOpen && onGuess) onGuess (1); };
    guessTwo.onClick = [this] { if (shown.guessOpen && onGuess) onGuess (2); };
    saveImage.onClick = [this] { if (shown.saveImageShown && onSaveImage) onSaveImage(); };
    apply();
}

ScreenComponent::~ScreenComponent() { setLookAndFeel (nullptr); }

void ScreenComponent::setActionNotice (const juce::String& notice)
{
    actionNotice = notice;
    startTimer (3000);
    apply();
    resized();
}

void ScreenComponent::timerCallback()
{
    stopTimer();
    actionNotice.clear();
    apply();
    resized();
}

bool ScreenComponent::requestSource (int source)
{
    if (! shown.sourcesShown || ! onSelect
        || (source == 1 ? ! shown.sourceOneEnabled : source == 2 ? ! shown.sourceTwoEnabled : true))
        return false;
    onSelect (source);
    return true;
}

bool ScreenComponent::requestReveal()
{
    if (! shown.revealShown || ! shown.revealEnabled || ! onReveal) return false;
    onReveal();
    return true;
}

bool ScreenComponent::requestEnd()
{
    if (! shown.endEnabled || ! onEnd) return false;
    onEnd();
    return true;
}

void ScreenComponent::setScreen (const Screen& next)
{
    const bool languageChanged = languageRevision != i18n::revision();
    if (! languageChanged && next == shown) return;
    languageRevision = i18n::revision();
    shown = next;
    apply();
    resized(); // Guidance rows depend on the current localized sentence lengths.
}

void ScreenComponent::apply()
{
    title.setText (shown.title, juce::dontSendNotification);
    const auto& instruction = actionNotice.isNotEmpty() ? actionNotice : shown.instruction;
    status.setText (instruction, juce::dontSendNotification);
    status.setVisible (instruction.isNotEmpty());
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
        unavailable::set (*button, shown.sourcesShown && enabled, shown.sourceReason);
        button->setTitle (button->getButtonText());
        button->setDescription (button->getButtonText());
        button->setTooltip (button->getButtonText());
    }
    reveal.setVisible (shown.revealShown && !shown.alignOffShown);
    unavailable::set (reveal, shown.revealShown && shown.revealEnabled,
                      shown.guessOpen && shown.guessAnswer == 0 ? "Choose an answer first" : "Hear both sources first");
    reveal.setTitle (shown.revealTitle);
    reveal.setDescription (shown.revealTitle);
    reveal.setTooltip (shown.revealTitle);
    alignOff.setVisible (shown.alignOffShown);
    alignOff.setTitle ("Turn automatic alignment OFF");
    alignOff.setDescription (alignOff.getTitle());
    alignOff.setTooltip (alignOff.getTitle());
    approve.setVisible (shown.approveShown);
    approve.setButtonText (shown.approveText);
    approve.setTitle (shown.approveTitle);
    approve.setDescription (shown.approveDescription);
    question.setText (shown.guessQuestion, juce::dontSendNotification);
    question.setVisible (shown.guessShown);
    outcome.setText (shown.guessOutcome, juce::dontSendNotification);
    outcome.setVisible (shown.guessShown && shown.guessOutcome.isNotEmpty());
    outcome.setColour (juce::Label::textColourId, shown.guessRight ? COL_SPECTRUM_DELTA_BR : COL_NORMAL);
    for (auto [button, answer] : { std::pair { &guessOne, 1 }, std::pair { &guessTwo, 2 } })
    {
        button->setVisible (shown.guessShown);
        button->setToggleState (shown.guessShown && shown.guessAnswer == answer, juce::dontSendNotification);
        unavailable::set (*button, shown.guessShown && shown.guessOpen, "The answer was fixed at REVEAL");
        button->setTitle (answer == 1 ? "Answer SOURCE 1" : "Answer SOURCE 2");
        button->setDescription (button->getTitle());
        button->setTooltip (button->getTitle());
    }
    saveImage.setVisible (shown.saveImageShown);
    saveImage.setTitle ("Save the result as an image");
    saveImage.setDescription (saveImage.getTitle());
    saveImage.setTooltip (saveImage.getTitle());
    unavailable::set (end, shown.endEnabled, "Returning to normal level");
    end.setTitle (shown.endTitle);
    end.setDescription (shown.endTitle + " / " + shown.detail);
    end.setTooltip (end.getDescription());
    for (auto* always : std::initializer_list<juce::Component*> { &title, &detail, &end }) always->setVisible (true);
    if (covered)
        for (juce::Component* own : std::initializer_list<juce::Component*> { &title, &status, &detail, &cause, &recovery, &question,
                                     &outcome, &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
            own->setVisible (false);
    repaint();
}

void ScreenComponent::setCovered (bool next)
{
    if (covered == next) return;
    covered = next;
    apply();
}

void ScreenComponent::resized()
{
    context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    for (auto* button : { &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
        button->setPresentationContext (context);
    for (auto* label : { &question, &outcome })
        label->setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    status.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    detail.setFont (labelFont (context, typography::TextRole::status, typography::Composition::information));
    cause.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    recovery.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    auto area = getLocalBounds().reduced (compact ? 12 : 24);
    const auto needed = [&] (const juce::Label& label)
    {
        const auto border = label.getBorderSize();
        const auto width = juce::jmax (1, area.getWidth() - border.getLeftAndRight());
        juce::AttributedString text;
        text.append (text_style::shownText (label.getText()), label.getFont(), juce::Colours::white);
        juce::TextLayout layout;
        layout.createLayout (text, static_cast<float> (width));
        return juce::roundToInt (std::ceil (std::max (layout.getHeight(),
            text_style::shownWrappedHeight (label.getText(), label.getFont(), width)))) + border.getTopAndBottom() + 2;
    };
    // Waiting/interruption screens give the cause and next step their actual localized height.
    // Normal source buttons keep their established layout; no font is compressed or shrunk.
    title.setBounds (area.removeFromTop (shown.guidanceShown ? needed (title) : compact ? 24 : 40));
    // A RESULT with two facts takes the lines it needs; a text that fits keeps the fixed height
    // (needed() adds 2 px of slack, so a tight single line does not move the sources).
    const auto fixedStatus = compact ? 30 : 48;
    const auto statusHeight = shown.guidanceShown || needed (status) - 2 > fixedStatus ? needed (status) : fixedStatus;
    status.setBounds ((!shown.guidanceShown || status.isVisible()) ? area.removeFromTop (statusHeight) : juce::Rectangle<int>());
    detail.setBounds (area.removeFromBottom (compact ? 34 : 46));
    auto actions = area.removeFromBottom (compact ? 28 : 40);
    if (shown.saveImageShown && shown.alignOffShown)
    {
        const auto third = actions.getWidth() / 3;
        alignOff.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        saveImage.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        end.setBounds (actions.reduced (3, 0));
    }
    else
    {
        end.setBounds (actions.removeFromRight (actions.getWidth() / 2).reduced (3, 0));
        reveal.setBounds (actions.reduced (3, 0));
        alignOff.setBounds (reveal.getBounds());
        saveImage.setBounds (reveal.getBounds());
    }
    if (shown.guessShown)
    {
        // The question, its two answers and, after REVEAL, the outcome: one group centred above REVEAL.
        auto row = area.removeFromBottom (compact ? 26 : 40).withTrimmedBottom (compact ? 3 : 6);
        const auto gap = compact ? 6 : 10, answer = row.getHeight() + 8;
        const auto textWidth = [] (const juce::Label& label)
        {
            return label.isVisible() ? juce::roundToInt (std::ceil (text_style::shownWidth (label.getFont(), label.getText())))
                + label.getBorderSize().getLeftAndRight() + 2 : 0;
        };
        const auto asked = textWidth (question), told = textWidth (outcome);
        const auto total = asked + gap + answer * 2 + 4 + (told > 0 ? gap + told : 0);
        row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), total), row.getHeight());
        question.setBounds (row.removeFromLeft (juce::jmin (asked, row.getWidth() - gap - answer * 2 - 4)));
        row.removeFromLeft (gap);
        guessOne.setBounds (row.removeFromLeft (answer));
        row.removeFromLeft (4);
        guessTwo.setBounds (row.removeFromLeft (answer));
        row.removeFromLeft (gap);
        outcome.setBounds (row);
    }
    area.reduce (0, compact ? 5 : 12);
    const auto causeHeight = needed (cause);
    const auto recoveryHeight = needed (recovery);
    // A live trial retains source controls during long timing loss. Reserve guidance above
    // them instead of reusing the same rectangle as an ended-trial screen.
    auto guidance = shown.guidanceShown && (shown.sourcesShown || shown.approveShown)
        ? area.removeFromTop (causeHeight + recoveryHeight)
        : area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), causeHeight + recoveryHeight));
    cause.setBounds (guidance.removeFromTop (causeHeight));
    recovery.setBounds (guidance);
    approve.setBounds (area);
    one.setBounds (area.removeFromLeft (area.getWidth() / 2).reduced (3, 0));
    two.setBounds (area.reduced (3, 0));
}

void ScreenComponent::paint (juce::Graphics& g)
{
    comparison_surface::paintScreen (g, *this);
}
}
