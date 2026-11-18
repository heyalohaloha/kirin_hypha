#include "HyphaBlindScreen.h"
#include "HyphaLanguage.h"
#include "HyphaPreProcessingNotice.h"
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
                         screen.guessAnswer, screen.guessQuestion, screen.guessOutcome, screen.saveImageShown, screen.detailsShown, screen.detailsOpen, screen.detailsText, screen.frequencyDetails, screen.preferenceResultText, screen.preferenceResultNote, screen.roundsShown, screen.roundsEnabled, screen.roundsValue, screen.roundsProgress);
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
    roundsLabel.setComponentID (idPrefix + "-rounds-label");
    roundsLabel.setColour (juce::Label::textColourId, COL_NORMAL);
    roundsLabel.setMinimumHorizontalScale (1.0f);
    roundsLabel.setJustificationType (juce::Justification::centred);
    addChildComponent (roundsLabel);
    int roundIndex = 0;
    for (auto* button : { &roundsOne, &roundsFive, &roundsTen })
    {
        const int values[] { 1, 5, 10 }; const int value = values[roundIndex++];
        button->setComponentID (idPrefix + "-rounds-" + juce::String (value));
        button->onClick = [this, value] { if (shown.roundsShown && shown.roundsEnabled && onRounds) onRounds (value); };
        addChildComponent (button);
    }
    detailsToggle.setComponentID (idPrefix + "-details");
    detailsToggle.onClick = [this] { if (shown.detailsShown && onDetails) onDetails(); };
    addChildComponent (detailsToggle);
    detailsBody.setComponentID (idPrefix + "-details-body");
    detailsBody.setMultiLine (true); detailsBody.setReadOnly (true);
    detailsBody.setScrollbarsShown (true);
    detailsBody.setColour (juce::TextEditor::textColourId, COL_NORMAL);
    detailsViewport.setComponentID (idPrefix + "-details-viewport");
    detailsViewport.setViewedComponent (&detailsContent, false);
    detailsViewport.setScrollBarsShown (true, false);
    detailsContent.addAndMakeVisible (detailsBody);
    frequencyRow.setComponentID (idPrefix + "-frequency-details");
    detailsContent.addChildComponent (frequencyRow);
    addChildComponent (detailsViewport);
    preferenceResultBody.setComponentID (idPrefix + "-preference-result");
    preferenceResultBody.setMultiLine (true); preferenceResultBody.setReadOnly (true);
    preferenceResultBody.setScrollbarsShown (true);
    preferenceResultBody.setColour (juce::TextEditor::textColourId, COL_NORMAL);
    addChildComponent (preferenceResultBody);
    preferenceResultBody.setJustification (juce::Justification::centred);
    preferenceNoteBody.setComponentID (idPrefix + "-preference-note");
    preferenceNoteBody.setMultiLine (true); preferenceNoteBody.setReadOnly (true);
    preferenceNoteBody.setScrollbarsShown (true);
    preferenceNoteBody.setColour (juce::TextEditor::textColourId, COL_NORMAL);
    addChildComponent (preferenceNoteBody);
    for (auto* body : { &detailsBody, &preferenceResultBody, &preferenceNoteBody })
    {
        body->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        body->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        body->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    }
    apply();
}

ScreenComponent::~ScreenComponent() { detailsViewport.setViewedComponent (nullptr, false); setLookAndFeel (nullptr); }

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
    const bool preferenceResult = shown.preferenceResultText.isNotEmpty();
    preferenceResultBody.setVisible (preferenceResult && !shown.detailsOpen);
    const auto& resultText = shown.preferenceResultText;
    preferenceNoteBody.setVisible (preferenceResult && !shown.detailsOpen);
    if (preferenceNoteBody.getText() != shown.preferenceResultNote) preferenceNoteBody.setText (shown.preferenceResultNote, false);
    if (preferenceResultBody.getText() != resultText) preferenceResultBody.setText (resultText, false);
    detailsToggle.setVisible (shown.detailsShown);
    detailsToggle.setButtonText (shown.detailsOpen ? "Back" : "Details");
    detailsViewport.setVisible (shown.detailsShown && shown.detailsOpen);
    detailsBody.setVisible (shown.detailsShown && shown.detailsOpen);
    frequencyRow.setResult (shown.frequencyDetails);
    frequencyRow.setVisible (shown.detailsShown && shown.detailsOpen && shown.frequencyDetails != nullptr);
    if (detailsBody.getText() != shown.detailsText) detailsBody.setText (shown.detailsText, false);
    saveImage.setVisible (shown.saveImageShown);
    saveImage.setTitle ("Save the result as an image");
    saveImage.setDescription (saveImage.getTitle());
    saveImage.setTooltip (saveImage.getTitle());
    unavailable::set (end, shown.endEnabled, "Returning to normal level");
    end.setTitle (shown.endTitle);
    end.setDescription (shown.endTitle + " / " + shown.detail);
    end.setTooltip (end.getDescription());
    for (auto* always : std::initializer_list<juce::Component*> { &title, &detail, &end }) always->setVisible (true);
    if (preferenceResult) { status.setVisible (false); detail.setVisible (false); }
    if (shown.detailsShown && shown.detailsOpen)
        for (juce::Component* own : std::initializer_list<juce::Component*> { &status, &detail, &cause, &recovery,
                &question, &outcome, &one, &two, &reveal, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
            own->setVisible (false);
    if (covered)
        for (juce::Component* own : std::initializer_list<juce::Component*> { &title, &status, &detail, &cause, &recovery, &question,
                                     &outcome, &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage, &detailsToggle, &detailsViewport, &detailsBody, &preferenceResultBody, &preferenceNoteBody })
            own->setVisible (false);
    repaint();
}

void ScreenComponent::setCovered (bool next)
{
    if (covered == next) return;
    covered = next;
    apply();
    resized();
}

void ScreenComponent::resized()
{
    context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    // The active preference screen uses this row for explicit round progress.
    // The same exit explanation remains on END's accessible description/tooltip.
    const bool compactPreference = compact && shown.roundsShown && shown.guessShown;
    detail.setVisible (!covered && !compactPreference && !shown.detailsOpen && shown.preferenceResultText.isEmpty());
    const bool showRounds = !covered && shown.roundsShown && !shown.detailsOpen
        && shown.preferenceResultText.isEmpty() && getWidth() >= 450 && getHeight() >= 300;
    roundsLabel.setVisible (showRounds);
    outcome.setVisible (!covered && shown.guessShown && shown.guessOutcome.isNotEmpty() && !showRounds);
    roundsLabel.setText (shown.roundsEnabled ? i18n::tr ("ROUNDS") : shown.roundsProgress, juce::dontSendNotification);
    roundsLabel.setFont (labelFont (context, typography::TextRole::listenAction, typography::Composition::information));
    int roundIndex = 0;
    for (auto* button : { &roundsOne, &roundsFive, &roundsTen })
    {
        const int values[] { 1, 5, 10 }; const int value = values[roundIndex++];
        button->setPresentationContext (context);
        button->setButtonText (i18n::tr (juce::String (value) + juce::String::fromUTF8 (u8"×")));
        button->setVisible (showRounds && shown.roundsEnabled);
        unavailable::set (*button, shown.roundsEnabled, "Rounds cannot change after the first answer");
        button->setToggleState (shown.roundsValue == value, juce::dontSendNotification);
    }

    detailsToggle.setPresentationContext (context);
    for (auto* body : { &detailsBody, &preferenceResultBody, &preferenceNoteBody })
    {
        body->setFont (displayTextFont (body->getText(), context,
                                       body == &preferenceResultBody ? typography::TextRole::primaryValue : typography::TextRole::listenChoice, typography::Composition::information));
        body->applyFontToAllText (body->getFont());
    }
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    if (shown.detailsShown && shown.detailsOpen)
    {
        auto panel = getLocalBounds().reduced (compact ? 12 : 24);
        title.setBounds (panel.removeFromTop (compact ? 24 : 40));
        auto actions = panel.removeFromBottom (compact ? 28 : 40);
        detailsToggle.setBounds (actions.removeFromLeft (actions.getWidth() / 2).reduced (3, 0));
        end.setBounds (actions.reduced (3, 0));
        detailsViewport.setBounds (panel.reduced (0, 5));
        const int width = juce::jmax (1, detailsViewport.getWidth() - detailsViewport.getScrollBarThickness());
        detailsBody.setScrollbarsShown (false);
        detailsBody.setBounds (0, 0, width, 100);
        const int textHeight = juce::jmax (40, detailsBody.getTextHeight() + 16);
        detailsBody.setSize (width, textHeight);
        const int graphHeight = juce::jmax (108, juce::jmin (180, detailsViewport.getHeight()));
        frequencyRow.setBounds (0, textHeight + 12, width, graphHeight);
        detailsContent.setSize (width, textHeight + (shown.frequencyDetails ? graphHeight + 12 : 0));
        return;
    }
    for (auto* button : { &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
        button->setPresentationContext (context);
    if (shown.preferenceResultText.isNotEmpty())
    {
        auto panel = getLocalBounds().reduced (compact ? 12 : 24);
        title.setBounds (panel.removeFromTop (compact ? 24 : 40));
        auto actions = panel.removeFromBottom (compact ? 28 : 40);
        const auto third = actions.getWidth() / 3;
        detailsToggle.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        saveImage.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        end.setBounds (actions.reduced (3, 0));
        auto sources = panel.removeFromBottom (compact ? 28 : 40);
        one.setBounds (sources.removeFromLeft (sources.getWidth() / 2).reduced (3, 0));
        two.setBounds (sources.reduced (3, 0));
        const auto noteHeight = juce::jmin (panel.getHeight() / 3, compact ? 52 : 72);
        preferenceNoteBody.setBounds (panel.removeFromBottom (noteHeight).reduced (0, 3));
        const auto textHeight = text_style::shownWrappedHeight (shown.preferenceResultText,
            preferenceResultBody.getFont(), juce::jmax (1, panel.getWidth() - 12)) + 12.0f;
        preferenceResultBody.setBounds (panel.withSizeKeepingCentre (panel.getWidth(),
            juce::jmin (panel.getHeight(), juce::roundToInt (std::ceil (textHeight)))));
        return;
    }
    for (auto* label : { &question, &outcome })
        label->setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    status.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    detail.setFont (labelFont (context, typography::TextRole::status, typography::Composition::information));
    const auto guidanceRole = shown.cause == live_compare_ui::preProcessingCause
        ? typography::TextRole::listenChoice : typography::TextRole::body;
    cause.setFont (labelFont (context, guidanceRole, typography::Composition::information));
    recovery.setFont (labelFont (context, guidanceRole, typography::Composition::information));
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
    detail.setBounds (detail.isVisible() ? area.removeFromBottom (compact ? 34 : 46) : juce::Rectangle<int>());
    auto actions = area.removeFromBottom (compact ? 28 : 40);
    if (shown.detailsShown)
    {
        const auto third = actions.getWidth() / 3;
        detailsToggle.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        saveImage.setBounds (actions.removeFromLeft (third).reduced (3, 0));
        end.setBounds (actions.reduced (3, 0));
    }
    else if (shown.saveImageShown && shown.alignOffShown)
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
    if (showRounds)
    {
        auto row = area.removeFromBottom (32).reduced (0, 2);
        row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), 360), row.getHeight());
        roundsLabel.setBounds (shown.roundsEnabled ? row.removeFromLeft (120) : row);
        roundsOne.setBounds (row.removeFromLeft (80).reduced (2, 0));
        roundsFive.setBounds (row.removeFromLeft (80).reduced (2, 0));
        roundsTen.setBounds (row.reduced (2, 0));
    }
    if (shown.guessShown)
    {
        if (compact && shown.roundsShown && outcome.isVisible())
        {
            outcome.setJustificationType (juce::Justification::centred);
            outcome.setBounds (area.removeFromBottom (24));
        }
        else outcome.setJustificationType (juce::Justification::centredLeft);
        // The question, its two answers and, after REVEAL, the outcome: one group centred above REVEAL.
        auto row = area.removeFromBottom (compact ? 26 : 40).withTrimmedBottom (compact ? 3 : 6);
        const auto gap = compact ? 6 : 10, answer = row.getHeight() + 8;
        const auto textWidth = [] (const juce::Label& label)
        {
            return label.isVisible() ? juce::roundToInt (std::ceil (text_style::shownWidth (label.getFont(), label.getText())))
                + label.getBorderSize().getLeftAndRight() + 2 : 0;
        };
        const auto asked = textWidth (question), told = compact && shown.roundsShown ? 0 : textWidth (outcome);
        const auto total = asked + gap + answer * 2 + 4 + (told > 0 ? gap + told : 0);
        row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), total), row.getHeight());
        question.setBounds (row.removeFromLeft (juce::jmin (asked, row.getWidth() - gap - answer * 2 - 4)));
        row.removeFromLeft (gap);
        guessOne.setBounds (row.removeFromLeft (answer));
        row.removeFromLeft (4);
        guessTwo.setBounds (row.removeFromLeft (answer));
        row.removeFromLeft (gap);
        if (!(compact && shown.roundsShown)) outcome.setBounds (row);
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
