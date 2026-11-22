#pragma once

#include "HyphaBlindScreen.h"
#include "HyphaBlindTrialLayout.h"

namespace hypha::blind_ui
{
inline void ScreenComponent::layoutTrial (bool choosingRounds, bool fixedProgress)
{
    status.setFont (labelFont (context, shown.votingComplete ? typography::TextRole::compactExplanation
        : typography::TextRole::body, typography::Composition::information));
    question.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    outcome.setFont (labelFont (context, fixedProgress ? typography::TextRole::listenAction : typography::TextRole::body, typography::Composition::information));
    const auto guidanceRole = shown.cause == live_compare_ui::preProcessingCause
        ? typography::TextRole::listenChoice : typography::TextRole::body;
    for (auto* label : { &cause, &recovery })
        label->setFont (labelFont (context, guidanceRole, typography::Composition::information));
    for (auto* label : { &detail, &notice })
        label->setFont (labelFont (context, typography::TextRole::compactExplanation, typography::Composition::information));
    status.setText (actionNotice.isNotEmpty() && !shown.votingComplete ? actionNotice : shown.instruction, juce::dontSendNotification);
    detail.setText (shown.endExplanation.isNotEmpty() ? shown.endExplanation : shown.detail, juce::dontSendNotification);

    auto area = getLocalBounds().reduced (juce::jlimit (6, 24, getWidth() / 40), 0);
    const auto measure = [&] (const juce::Label& label, const juce::String& text)
    {
        if (text.isEmpty()) return 0;
        const auto border = label.getBorderSize();
        const auto width = juce::jmax (1, area.getWidth() - border.getLeftAndRight());
        const auto measured = text_style::shownWrappedHeight (text, label.getFont(), width, look.policyFor (label));
        return juce::roundToInt (std::ceil (measured)) + border.getTopAndBottom() + 2;
    };
    const auto place = [] (juce::Label& label, juce::Rectangle<int> bounds)
    {
        label.setVisible (!bounds.isEmpty() && label.getText().isNotEmpty());
        label.setBounds (label.isVisible() ? bounds : juce::Rectangle<int>());
    };
    const auto textWidth = [] (const juce::Label& label, auto policy)
    {
        return label.isVisible() ? juce::roundToInt (std::ceil (text_style::shownWidth (label.getFont(), label.getText(), policy)))
            + label.getBorderSize().getLeftAndRight() + 2 : 0;
    };
    constexpr int gap = 4, badgeWidth = 48;
    const auto asked = textWidth (question, shown.guessQuestionFixed ? text_style::LabelPolicy::fixed
                                                                   : text_style::LabelPolicy::localized);
    const auto told = textWidth (outcome, text_style::LabelPolicy::localized);
    const auto answerInset = shown.guessQuestionFixed ? 4 : 8;
    const auto preferredAction = juce::jlimit (28, 40, 20 + getHeight() / 22);
    const auto outcomeHeight = !fixedProgress && told > 0
        && asked + told + 2 * (preferredAction + answerInset) + 3 * gap > area.getWidth()
        ? measure (outcome, outcome.getText()) : 0;
    const auto endHeight = measure (detail, detail.getText());
    const auto sourceHeight = shown.sourcesShown ? trial_layout::sourceMinimum : 0;
    const auto approvalHeight = shown.approveShown ? trial_layout::minimumActionHeight : 0;
    const auto titleHeight = measure (title, title.getText());
    const auto statusHeight = shown.trialNotice.contains (status.getText()) ? 0 : measure (status, status.getText());
    const auto requiredStatus = shown.votingComplete ? statusHeight : 0;
    const auto causeHeight = shown.guidanceShown ? measure (cause, cause.getText()) : 0;
    const auto recoveryHeight = shown.guidanceShown ? measure (recovery, recovery.getText()) : 0;
    const auto candidates = notice_stack::ordered (shown.trialNotice, shown.guidanceShown ? cause.getText() : juce::String(),
                                                   shown.guidanceShown ? recovery.getText() : juce::String(), extraTrialNotice());

    juce::StringArray allInline;
    bool hasGuidance = false;
    for (const auto& item : candidates)
        if (item.guidance) hasGuidance = true; else allInline.add (item.text);
    const auto allInlineText = allInline.joinIntoString ("\n");
    const auto allNoticeHeight = measure (notice, allInlineText) + (hasGuidance ? causeHeight + recoveryHeight : 0);

    constexpr auto minimumAction = trial_layout::minimumActionHeight;
    const int fixedHeight = sourceHeight + approvalHeight + endHeight + requiredStatus + outcomeHeight;
    const int actions = 1 + static_cast<int> (shown.guessShown);
    const bool combineRounds = choosingRounds && fixedHeight + (actions + 1) * minimumAction
        + allNoticeHeight + titleHeight > getHeight();
    const int rows = actions + static_cast<int> (choosingRounds && !combineRounds);
    int discretionary = juce::jmax (0, getHeight() - fixedHeight - rows * minimumAction);
    const int available = discretionary;
    bool showTitle = discretionary >= allNoticeHeight + titleHeight;
    bool showStatus = requiredStatus > 0 || discretionary >= allNoticeHeight + (showTitle ? titleHeight : 0) + statusHeight;
    discretionary -= (showTitle ? titleHeight : 0) + (showStatus ? statusHeight - requiredStatus : 0);

    juce::String inlineText = allInlineText;
    juce::StringArray hidden;
    bool showGuidance = hasGuidance;
    if (allNoticeHeight > discretionary && !candidates.empty())
    {
        const auto& first = candidates.front();
        const auto firstHeight = first.guidance ? causeHeight + recoveryHeight : measure (notice, first.text);
        showGuidance = first.guidance && firstHeight <= discretionary;
        inlineText = !first.guidance && firstHeight <= discretionary ? first.text : juce::String();
        for (size_t i = firstHeight <= discretionary ? 1 : 0; i < candidates.size(); ++i)
            hidden.add (candidates[i].text);
    }
    if (!showStatus && statusHeight > 0 && !isRoutineTrialHint()) hidden.addIfNotAlreadyThere (status.getText());
    notice.setText (inlineText, juce::dontSendNotification);
    const auto noticeHeight = measure (notice, inlineText);
    discretionary -= noticeHeight + (showGuidance ? causeHeight + recoveryHeight : 0);
    // The rows of the stopped guidance stay free while it is not shown, so neither the height nor the
    // position of the action rows changes when it appears. Longer notices still take what they need.
    const auto reserved = shown.reservedCause.isEmpty() && shown.reservedRecovery.isEmpty() ? 0
        : juce::jmin (available, titleHeight + measure (cause, shown.reservedCause) + measure (recovery, shown.reservedRecovery));
    const auto kept = juce::jmax (0, reserved - (available - discretionary));
    discretionary -= kept;
    const auto actionHeight = trial_layout::actionHeightFor (discretionary, rows, preferredAction);

    // Fixed-height regions are consumed from the top in order. No rectangle is centred
    // into insufficient remaining height or allowed to extend over an adjacent row.
    place (title, area.removeFromTop (showTitle ? titleHeight : 0));
    place (status, area.removeFromTop (showStatus ? statusHeight : 0));
    auto sourceRow = area.removeFromTop (sourceHeight);
    approve.setBounds (area.removeFromTop (approvalHeight));
    place (detail, area.removeFromTop (endHeight));
    if (showGuidance)
    {
        place (cause, area.removeFromTop (causeHeight));
        place (recovery, area.removeFromTop (recoveryHeight));
    }
    else { place (cause, {}); place (recovery, {}); }
    place (notice, area.removeFromTop (noticeHeight));
    if (outcomeHeight > 0) place (outcome, area.removeFromTop (outcomeHeight));
    area.removeFromTop (kept);

    moreNotices.setPresentationContext (context);
    moreNotices.setNotices (hidden);
    if (moreNotices.isVisible() && sourceHeight > 0)
        moreNotices.setBounds (sourceRow.removeFromRight (badgeWidth).withHeight (actionHeight).reduced (3, 0));
    one.setBounds (sourceRow.removeFromLeft (sourceRow.getWidth() / 2).reduced (3, 0));
    two.setBounds (sourceRow.reduced (3, 0));

    const auto placeRounds = [&] (auto row)
    {
        trial_layout::placeRoundChoices (row, roundsLabel, roundsOne, roundsFive, roundsTen);
    };
    if (choosingRounds && !combineRounds) placeRounds (area.removeFromTop (actionHeight));
    if (shown.guessShown)
    {
        auto row = area.removeFromTop (actionHeight);
        const auto answer = actionHeight + answerInset;
        const auto outcomeInline = outcomeHeight > 0 ? 0 : told;
        const auto total = asked + 2 * answer + 2 * gap + (outcomeInline > 0 ? gap + outcomeInline : 0);
        row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), total), row.getHeight());
        const auto textSpace = juce::jmax (0, row.getWidth() - 2 * answer - (outcomeInline > 0 ? 3 : 2) * gap);
        const auto questionWidth = asked + outcomeInline > textSpace ? textSpace * asked / (asked + outcomeInline) : asked;
        question.setBounds (row.removeFromLeft (questionWidth));
        row.removeFromLeft (gap);
        guessOne.setBounds (row.removeFromLeft (answer));
        row.removeFromLeft (gap);
        guessTwo.setBounds (row.removeFromLeft (answer));
        if (outcomeInline > 0) { row.removeFromLeft (gap); outcome.setBounds (row); }
    }

    auto buttons = area.removeFromTop (actionHeight);
    if (moreNotices.isVisible() && sourceHeight == 0)
        moreNotices.setBounds (buttons.removeFromRight (badgeWidth).reduced (3, 0));
    if (combineRounds) placeRounds (buttons.removeFromLeft (buttons.getWidth() * 4 / 7));
    if (shown.saveImageShown && shown.alignOffShown)
    {
        const auto width = buttons.getWidth() / 3;
        alignOff.setBounds (buttons.removeFromLeft (width).reduced (3, 0));
        saveImage.setBounds (buttons.removeFromLeft (width).reduced (3, 0));
        end.setBounds (buttons.reduced (3, 0));
    }
    else
    {
        end.setBounds (buttons.removeFromRight (buttons.getWidth() / 2).reduced (3, 0));
        reveal.setBounds (buttons.reduced (3, 0));
        alignOff.setBounds (reveal.getBounds());
        saveImage.setBounds (reveal.getBounds());
    }
}
}
