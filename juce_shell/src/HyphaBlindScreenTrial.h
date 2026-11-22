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
    constexpr auto minimumAction = trial_layout::minimumActionHeight;
    const int baseHeight = sourceHeight + approvalHeight + endHeight + outcomeHeight;
    const int actions = 1 + static_cast<int> (shown.guessShown);

    // One state of the trial: its instruction, guidance and notices, what they need and what is shown.
    struct Stack
    {
        std::vector<notice_stack::Note> notes;
        juce::String inlineText;
        juce::StringArray hidden;
        int statusHeight = 0, required = 0, causeHeight = 0, recoveryHeight = 0, noticeHeight = 0, all = 0, used = 0;
        bool title = false, status = false, guidance = false;
    };
    const auto describe = [&] (const juce::String& instruction, const juce::String& why, const juce::String& how)
    {
        Stack s;
        s.statusHeight = shown.trialNotice.contains (instruction) ? 0 : measure (status, instruction);
        s.required = shown.votingComplete ? s.statusHeight : 0;
        s.notes = notice_stack::ordered (shown.trialNotice, why, how, extraTrialNotice());
        juce::StringArray lines;
        for (const auto& item : s.notes)
            if (item.guidance) s.guidance = true; else lines.add (item.text);
        if (s.guidance) { s.causeHeight = measure (cause, why); s.recoveryHeight = measure (recovery, how); }
        s.inlineText = lines.joinIntoString ("\n");
        s.all = measure (notice, s.inlineText) + s.causeHeight + s.recoveryHeight;
        return s;
    };
    const auto fit = [&] (Stack& s, int rows)
    {
        int room = juce::jmax (0, getHeight() - baseHeight - s.required - rows * minimumAction);
        s.title = room >= s.all + titleHeight;
        s.status = s.required > 0 || room >= s.all + (s.title ? titleHeight : 0) + s.statusHeight;
        const int heading = (s.title ? titleHeight : 0) + (s.status ? s.statusHeight - s.required : 0);
        room -= heading;
        if (s.all > room && !s.notes.empty())
        {
            const auto& first = s.notes.front();
            const auto firstHeight = first.guidance ? s.causeHeight + s.recoveryHeight : measure (notice, first.text);
            s.guidance = first.guidance && firstHeight <= room;
            s.inlineText = !first.guidance && firstHeight <= room ? first.text : juce::String();
            for (size_t i = firstHeight <= room ? 1 : 0; i < s.notes.size(); ++i)
                s.hidden.add (s.notes[i].text);
        }
        s.noticeHeight = measure (notice, s.inlineText);
        s.used = s.required + heading + s.noticeHeight + (s.guidance ? s.causeHeight + s.recoveryHeight : 0);
    };
    auto now = describe (status.getText(), shown.guidanceShown ? cause.getText() : juce::String(),
                         shown.guidanceShown ? recovery.getText() : juce::String());
    // The DAW stops and plays again without a click. Both states are laid out here: the instruction
    // gives way to the stopped guidance, the notices stay. The row of ROUNDS and the top of the action
    // rows follow the larger of the two, so END is the same rectangle in both and a press meant for
    // END lands on END.
    const bool canStop = shown.reservedCause.isNotEmpty() || shown.reservedRecovery.isNotEmpty();
    const bool isStopped = shown.guidanceShown && shown.cause == shown.reservedCause && shown.recovery == shown.reservedRecovery;
    const bool sameInstruction = shown.votingComplete || actionNotice.isNotEmpty();
    auto other = !canStop ? now
        : isStopped ? describe (sameInstruction ? status.getText() : shown.unguidedInstruction, {}, {})
        : describe (sameInstruction ? status.getText() : juce::String(), shown.reservedCause, shown.reservedRecovery);
    const bool combineRounds = choosingRounds && baseHeight + (actions + 1) * minimumAction
        + juce::jmax (now.required + now.all, other.required + other.all) + titleHeight > getHeight();
    const int rows = actions + static_cast<int> (choosingRounds && !combineRounds);
    fit (now, rows);
    fit (other, rows);
    if (!now.status && now.statusHeight > 0 && !isRoutineTrialHint()) now.hidden.addIfNotAlreadyThere (status.getText());
    notice.setText (now.inlineText, juce::dontSendNotification);
    const int kept = juce::jmax (now.used, other.used) - now.used;
    const auto actionHeight = trial_layout::actionHeightFor (
        getHeight() - baseHeight - rows * minimumAction - now.used - kept, rows, preferredAction);

    // Fixed-height regions are consumed from the top in order. No rectangle is centred
    // into insufficient remaining height or allowed to extend over an adjacent row.
    place (title, area.removeFromTop (now.title ? titleHeight : 0));
    place (status, area.removeFromTop (now.status ? now.statusHeight : 0));
    auto sourceRow = area.removeFromTop (sourceHeight);
    approve.setBounds (area.removeFromTop (approvalHeight));
    place (detail, area.removeFromTop (endHeight));
    if (now.guidance)
    {
        place (cause, area.removeFromTop (now.causeHeight));
        place (recovery, area.removeFromTop (now.recoveryHeight));
    }
    else { place (cause, {}); place (recovery, {}); }
    place (notice, area.removeFromTop (now.noticeHeight));
    if (outcomeHeight > 0) place (outcome, area.removeFromTop (outcomeHeight));
    area.removeFromTop (kept);

    moreNotices.setPresentationContext (context);
    moreNotices.setNotices (now.hidden);
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
