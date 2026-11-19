#pragma once
#include "FooterNoticeContractChecks.h"
#include <stdexcept>

namespace hypha::tests
{
inline void verifyFooterShortNoticeBoundary()
{
    const auto require = [] (bool ok) {
        if (!ok) throw std::runtime_error ("short footer must retain its complete text and bounds");
    };
    require (footer_notice::verifyCompletePaintOracle());
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        for (const int width : { 300, 375, 450 })
        for (const char* message : { "Copied", "Check delay compensation", "Delay unmeasured",
                                     "Position unconfirmed; POST plays", "AUTO stopped: press MATCH again" })
        {
            observatory::View view (observatory::Role::post);
            view.setVisible (true); view.setSize (width, width * 2 / 3);
            view.setFeedback (message);
            FeedbackStrip strip;
            strip.setBounds (view.statusStripBounds());
            strip.setPresentationContext (view.presentationContext());
            strip.setFeedback (message);
            auto& component = view.statusStripFolded() ? static_cast<juce::Component&> (strip)
                                                       : view.feedbackDetailsAnchor();
            const auto room = component.getLocalBounds().reduced (view.statusStripFolded() ? 6 : 3,
                                                                  view.statusStripFolded() ? 0 : 1);
            const auto expected = text_style::shownText (message);
            text_style::ShownTextLog actual;
            const auto image = component.createComponentSnapshot (component.getLocalBounds());
            // Expected text is independent of the candidate's available width. Ellipsis cannot pass.
            require (image.isValid() && footer_notice::completePaint (actual, expected, room));
            text_style::ShownTextLog missing;
            missing.add (expected.dropLastCharacters (1), room.toFloat());
            require (!footer_notice::completePaint (missing, expected, room));
            text_style::ShownTextLog shortened;
            shortened.add (expected.substring (0, 1) + juce::String::fromUTF8 (u8"…"), room.toFloat());
            require (!footer_notice::completePaint (shortened, expected, room));
            text_style::ShownTextLog outside;
            outside.add (expected, room.toFloat());
            require (!footer_notice::completePaint (outside, expected, room.withWidth (1)));
        }
    }
}
}
