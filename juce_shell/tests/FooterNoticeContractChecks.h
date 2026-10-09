#pragma once

#include "../src/HyphaFeedbackStrip.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"

namespace hypha::tests::footer_notice
{
// Test the actual localized paint, rather than requiring the entire sentence to fit a slot.
inline bool boundedPaint (juce::Component& component, const juce::String& full,
                          const presentation::Context& context, int horizontalInset,
                          int verticalInset)
{
    const auto localized = text_style::shownText (full);
    const auto room = component.getLocalBounds().reduced (horizontalInset, verticalInset);
    const auto contracted = monoFont (context,
        footer_notice_font::roleForText (context, full, room.getWidth()));
    const auto font = requiresJapaneseGlyphs (localized) ? nativeTextFontLike (contracted) : contracted;
    const auto expected = text_style::ellipsizedText (localized, font, static_cast<float> (room.getWidth()));
    text_style::ShownTextLog shown;
    const auto image = component.createComponentSnapshot (component.getLocalBounds());
    return ! room.isEmpty() && expected.isNotEmpty() && image.isValid()
        && shown.texts().contains (expected)
        && font.getStringWidthFloat (expected) <= room.getWidth() + 0.5f;
}

inline bool retainedInFooter (observatory::View& view, const juce::String& full)
{
    auto* button = dynamic_cast<juce::Button*> (&view.feedbackDetailsAnchor());
    if (button == nullptr || button->getButtonText() != full || button->getTooltip() != full
        || view.feedback() != full || view.getDescription() != full || ! button->onClick)
        return false;
    if (full.isEmpty()) return ! button->isVisible();
    int details = 0;
    const auto previous = view.onFeedbackDetails;
    view.onFeedbackDetails = [&details] { ++details; };
    button->onClick();
    view.onFeedbackDetails = previous;
    if (details != 1) return false;
    const auto folded = observatory::footerFolds (view.presentationContext().density);
    if (view.statusStripFolded() != folded || button->isVisible() == folded) return false;
    if (folded)
    {
        FeedbackStrip strip;
        strip.setBounds (view.statusStripBounds());
        strip.setPresentationContext (view.presentationContext());
        strip.setFeedback (full);
        return strip.text() == full && strip.getTitle() == full && strip.getTooltip() == full
            && boundedPaint (strip, full, view.presentationContext(), 6, 0);
    }
    if (view.statusStripBounds() != view.sessionBounds()
        || view.statusStripBounds().intersects (view.bodyBounds())
        || ! view.sessionBounds().contains (button->getBounds())
        || view.getComponentAt (button->getBounds().getCentre()) != button)
        return false;
    for (const auto* id : { "observatory-live-pre", "observatory-live-post", "observatory-live-end",
                           "observatory-live-return", "observatory-live-match", "observatory-menu" })
        if (const auto* action = view.findChildWithID (id); action != nullptr && action->isVisible()
            && button->getBounds().intersects (action->getBounds())) return false;
    return boundedPaint (*button, full, view.presentationContext(), 3, 1);
}

inline bool guideAndChainNotices()
{
    const juce::String notice ("PRE changed: check PAIR, MENU > LISTEN (POST)");
    const juce::String chain ("CHAIN LOAD 100% / 100%");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const auto preset : observatory::sizePresets)
        {
            observatory::View view (observatory::Role::post); view.setVisible (true);
            view.setSize (preset.width, preset.height);
            view.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
            view.setChainReadout (chain, true);
            view.setFeedback (notice);
            if (! retainedInFooter (view, notice)) return false;
            text_style::ShownTextLog shown;
            const auto image = view.createComponentSnapshot (view.getLocalBounds());
            if (! image.isValid() || view.chainReadoutForTest() != chain) return false;
            if (! view.statusStripFolded())
            {
                const auto& anchor = view.feedbackDetailsAnchor();
                const auto room = view.sessionBounds().withLeft (anchor.getRight() + 6);
                const auto font = monoFont (view.presentationContext(), typography::TextRole::action);
                const auto shortest = chain.replace (" / ", "/").fromFirstOccurrenceOf ("CHAIN ", false, false);
                const auto fits = text_style::shownWidth (font, shortest) <= room.getWidth();
                if (view.chainReadoutShownForTest() != fits) return false;
                if (fits && ! shown.texts().contains (chain)
                    && ! shown.texts().contains (chain.replace (" / ", "/"))
                    && ! shown.texts().contains (shortest)) return false;
                if (anchor.getWidth() <= 0 || anchor.getBounds().intersects (view.guideBounds())) return false;
            }
            view.setFeedback ({});
            if (view.chainReadoutForTest() != chain) return false;
        }
    }
    return true;
}
}
