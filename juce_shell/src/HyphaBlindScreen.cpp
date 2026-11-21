#include "HyphaBlindScreen.h"
#include "HyphaLanguage.h"
#include "HyphaPreProcessingNotice.h"
#include "HyphaComparisonSurfaceMaterial.h"
#include "HyphaUnavailableReason.h"

#include <array>
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
                         screen.guessAnswer, screen.guessQuestion, screen.guessOutcome, screen.saveImageShown, screen.matchedGainText, screen.preferenceResultText, screen.preferenceResultNote, screen.roundsShown, screen.roundsEnabled, screen.roundsValue, screen.roundsProgress);
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
    matchedGainBody.setComponentID(idPrefix + "-matched-gain");
    matchedGainBody.setMultiLine(true); matchedGainBody.setReadOnly(true);
    matchedGainBody.setJustification(juce::Justification::centredTop);
    matchedGainBody.setScrollbarsShown(false);
    matchedGainBody.setColour(juce::TextEditor::textColourId,COL_NORMAL);
    addChildComponent(matchedGainBody);
    preferenceResultBody.setComponentID (idPrefix + "-preference-result");
    preferenceResultBody.setMultiLine (true); preferenceResultBody.setReadOnly (true);
    preferenceResultBody.setScrollbarsShown (false);
    preferenceResultBody.setColour (juce::TextEditor::textColourId, COL_NORMAL);
    addChildComponent (preferenceResultBody);
    preferenceResultBody.setJustification (juce::Justification::centredTop);
    preferenceNoteBody.setComponentID (idPrefix + "-preference-note");
    preferenceNoteBody.setMultiLine (true); preferenceNoteBody.setReadOnly (true);
    preferenceNoteBody.setScrollbarsShown (false);
    preferenceNoteBody.setColour (juce::TextEditor::textColourId, COL_NORMAL);
    addChildComponent (preferenceNoteBody);
    for (auto* body : { &matchedGainBody, &preferenceResultBody, &preferenceNoteBody })
    {
        body->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        body->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        body->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        body->setBorder ({});
        body->setIndents (3, 0);
        body->setScrollToShowCursor (false);
    }
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
    const bool preferenceResult = shown.preferenceResultText.isNotEmpty() || shown.matchedGainText.isNotEmpty();
    preferenceResultBody.setVisible (shown.preferenceResultText.isNotEmpty());
    const auto resultText = shown.preferenceResultText;
    auto noteText = shown.preferenceResultText.isNotEmpty() ? shown.preferenceResultNote : text_style::shownText(shown.instruction) + "\n" + text_style::shownText(shown.detail);
    if (preferenceResult && shown.guidanceShown)
    {
        // A guidance label can still be valid after REVEAL. Give its text a measured row
        // in the result notes instead of painting the old LIVE BLIND labels at stale bounds.
        juce::StringArray notes;
        for (const auto& entry : { text_style::shownText (shown.cause), text_style::shownText (shown.recovery), noteText })
            if (entry.trim().isNotEmpty()) notes.addIfNotAlreadyThere (entry);
        noteText = notes.joinIntoString ("\n");
    }
    preferenceNoteBody.setVisible (preferenceResult && noteText.trim().isNotEmpty());
    if (preferenceNoteBody.getText() != noteText) preferenceNoteBody.setText(noteText,false);
    if (preferenceResultBody.getText() != resultText) preferenceResultBody.setText (resultText, false);
    matchedGainBody.setVisible(shown.matchedGainText.isNotEmpty());
    if (matchedGainBody.getText()!=shown.matchedGainText) matchedGainBody.setText(shown.matchedGainText,false);
    saveImage.setVisible (shown.saveImageShown);
    saveImage.setTitle ("Save the result as an image");
    saveImage.setDescription (saveImage.getTitle());
    saveImage.setTooltip (saveImage.getTitle());
    unavailable::set (end, shown.endEnabled, "Returning to normal level");
    end.setTitle (shown.endTitle);
    end.setDescription (shown.endTitle + " / " + shown.detail);
    end.setTooltip (end.getDescription());
    for (auto* always : std::initializer_list<juce::Component*> { &title, &detail, &end }) always->setVisible (true);
    if (preferenceResult)
        for (auto* label : { &status, &detail, &cause, &recovery }) label->setVisible (false);
    if (covered)
        for (juce::Component* own : std::initializer_list<juce::Component*> { &title, &status, &detail, &cause, &recovery, &question,
                                     &outcome, &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage, &matchedGainBody, &preferenceResultBody, &preferenceNoteBody })
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
    const bool showRounds = !covered && shown.roundsShown
        && shown.preferenceResultText.isEmpty() && getWidth() >= 450 && getHeight() >= 300;
    // This short standard viewport shows ROUNDS while the smaller compact sizes do not.
    // Keep the normal layout at 600 x 400 and above; the saved explanation on END is the fallback.
    const bool shortStandard = getWidth() >= 450 && getWidth() < 600
        && getHeight() >= 300 && getHeight() < 400;
    const bool shortLiveLayout = shortStandard && shown.sourcesShown && (showRounds || shown.saveImageShown);
    constexpr int sourceMinHeight = 60;
    const bool showDetail = !covered && !compactPreference
        && shown.preferenceResultText.isEmpty() && shown.matchedGainText.isEmpty();
    detail.setVisible (showDetail);
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

    for (auto* body : { &matchedGainBody, &preferenceResultBody, &preferenceNoteBody })
    {
        body->setFont (displayTextFont (body->getText(), context,
                                       body == &preferenceResultBody ? typography::TextRole::primaryValue : typography::TextRole::listenChoice, typography::Composition::information));
        body->applyFontToAllText (body->getFont());
    }
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    for (auto* button : { &one, &two, &reveal, &end, &approve, &alignOff, &guessOne, &guessTwo, &saveImage })
        button->setPresentationContext (context);
    if (shown.preferenceResultText.isNotEmpty() || shown.matchedGainText.isNotEmpty())
    {
        // Try the normal two-row actions first, using the actual wrapped text
        // at its final width. A narrow width alone does not select dense mode.
        auto normal = getLocalBounds().reduced (24);
        normal.removeFromTop (40); normal.removeFromBottom (80);
        const std::array<juce::TextEditor*, 3> bodies { &preferenceResultBody, &matchedGainBody, &preferenceNoteBody };
        const std::array<juce::Font, 3> normalFonts { bodies[0]->getFont(), bodies[1]->getFont(), bodies[2]->getFont() };
        std::array<int, 3> heights {};
        auto measure = [&] (juce::Rectangle<int> bounds, float scale, int padding)
        {
            int total = 0;
            bool fitsWidth = true;
            for (size_t i = 0; i < bodies.size(); ++i)
            {
                auto& body = *bodies[i];
                heights[i] = 0;
                if (! body.isVisible() || body.getText().trim().isEmpty()) continue;
                body.setScrollbarsShown (false);
                body.setBounds (bounds.getX(), bounds.getY(), bounds.getWidth(), juce::jmax (1, bounds.getHeight()));
                const auto height = normalFonts[i].getHeight();
                const auto font = normalFonts[i].withHeight (juce::jmax (juce::jmin (12.0f, height), height * scale));
                body.setFont (font);
                body.applyFontToAllText (font);
                heights[i] = body.getTextHeight() + padding;
                total += heights[i];
                fitsWidth = fitsWidth && body.getTextWidth() <= bounds.getWidth();
            }
            return std::pair { total, fitsWidth };
        };
        const auto [neededHeight, normalWidthFits] = measure (normal, 1.0f, 8);
        const bool denseResult = neededHeight > normal.getHeight() || ! normalWidthFits;
        auto panel = getLocalBounds().reduced (denseResult ? 4 : 24);
        if (denseResult) panel.removeFromTop (20); // same 24px top inset as normal
        title.setBounds (panel.removeFromTop (denseResult ? 18 : 40));
        auto actions = panel.removeFromBottom (denseResult ? 24 : 40);
        if (denseResult)
        {
            // Keep all four actions available without spending two rows on them.
            const int cell = actions.getWidth() / 4;
            one.setBounds (actions.removeFromLeft (cell).reduced (2, 0));
            two.setBounds (actions.removeFromLeft (cell).reduced (2, 0));
            saveImage.setBounds (actions.removeFromLeft (cell).reduced (2, 0));
            end.setBounds (actions.reduced (2, 0));
        }
        else
        {
            saveImage.setBounds (actions.removeFromLeft (actions.getWidth()/2).reduced (3, 0));
            end.setBounds (actions.reduced (3, 0));
            auto sources = panel.removeFromBottom (40);
            one.setBounds (sources.removeFromLeft (sources.getWidth() / 2).reduced (3, 0));
            two.setBounds (sources.reduced (3, 0));
        }
        // An actual line count at the final width reserves separate strips for the result,
        // compensation and notes. Never centre a block taller than its remaining space.
        auto measured = measure (panel, 1.0f, denseResult ? 4 : 8);
        auto fits = [&] { return measured.first <= panel.getHeight() && measured.second; };
        for (const auto scale : { 0.95f, 0.90f, 0.85f, 0.80f, 0.70f, 0.60f })
        {
            if (fits()) break;
            measured = measure (panel, scale, 3);
        }
        if (! fits())
        {
            // A very long name or note can exhaust even a dense panel. Keep every body
            // bounded and scrollable rather than letting it paint over an adjacent row.
            auto remaining = static_cast<int> (std::count_if (heights.begin(), heights.end(), [] (int h) { return h > 0; }));
            for (size_t i = 0; i < bodies.size(); ++i)
            {
                auto& body = *bodies[i];
                if (heights[i] == 0) { body.setBounds ({}); continue; }
                const auto height = juce::jmin (heights[i], panel.getHeight() / juce::jmax (1, remaining));
                body.setBounds (panel.removeFromTop (height));
                body.setScrollbarsShown (true);
                --remaining;
            }
            return;
        }
        preferenceNoteBody.setBounds (panel.removeFromBottom (heights[2]));
        const auto groupHeight = heights[0] + heights[1];
        auto centred = panel.withSizeKeepingCentre (panel.getWidth(), groupHeight);
        preferenceResultBody.setBounds (centred.removeFromTop (heights[0]));
        matchedGainBody.setBounds (centred);
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
    auto area = getLocalBounds().reduced (compact || shortLiveLayout ? 12 : 24);
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
    title.setBounds (area.removeFromTop (shown.guidanceShown ? needed (title)
                                                            : (compact || shortLiveLayout ? 24 : 40)));
    // A RESULT with two facts takes the lines it needs; a text that fits keeps the fixed height
    // (needed() adds 2 px of slack, so a tight single line does not move the sources).
    const auto fixedStatus = compact || shortLiveLayout ? 30 : 48;
    const auto statusHeight = shown.guidanceShown || needed (status) - 2 > fixedStatus ? needed (status) : fixedStatus;
    status.setBounds ((!shown.guidanceShown || status.isVisible()) ? area.removeFromTop (statusHeight) : juce::Rectangle<int>());
    const int actionHeight = compact ? 28 : shortLiveLayout ? 32 : 40;
    const int voteHeight = compact ? 26 : shortLiveLayout ? 32 : 40;
    const int verticalInset = compact || shortLiveLayout ? 5 : 12;
    const int detailHeight = shortLiveLayout ? juce::jmax (34, needed (detail)) : compact ? 34 : 46;
    const auto causeHeight = needed (cause);
    const auto recoveryHeight = needed (recovery);
    if (shortLiveLayout)
    {
        // First budget the source cards, interactive rows and visible recovery. END already
        // exposes the explanation to hover and accessibility if the extra detail row will not fit.
        const int requiredHeight = sourceMinHeight + actionHeight + (showRounds ? 32 : 0)
            + (shown.guessShown ? voteHeight : 0) + 2 * verticalInset
            + (shown.guidanceShown ? causeHeight + recoveryHeight : 0);
        detail.setVisible (showDetail && area.getHeight() >= requiredHeight + detailHeight);
    }
    detail.setBounds (detail.isVisible() ? area.removeFromBottom (detailHeight) : juce::Rectangle<int>());
    auto actions = area.removeFromBottom (actionHeight);
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
        auto row = area.removeFromBottom (voteHeight)
                       .withTrimmedBottom (compact || shortLiveLayout ? 3 : 6);
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
    area.reduce (0, verticalInset);
    // A live trial retains source controls during long timing loss. Reserve guidance above
    // them instead of reusing the same rectangle as an ended-trial screen.
    const auto guidanceHeight = shortLiveLayout
        ? juce::jmin (causeHeight + recoveryHeight, juce::jmax (0, area.getHeight() - sourceMinHeight))
        : causeHeight + recoveryHeight;
    auto guidance = shown.guidanceShown && (shown.sourcesShown || shown.approveShown)
        ? area.removeFromTop (guidanceHeight)
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
