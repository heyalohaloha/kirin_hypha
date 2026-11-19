#pragma once
#include "ListenTextLayoutTest.h"

namespace hypha::tests
{
// Narrow presentation evidence only. The processor/editor transition and three detail strings
// are exercised separately by the existing live product --after-tail-only entry.
template <typename Require>
void verifyKeepTailNoticeLayout (Require require)
{
    const juce::String text (live_compare_ui::keepTailEndedNotice);
    const auto previewDir = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    require (previewDir.isNotEmpty(), "Keep notice preview directory must be supplied");
    observatory::View view (observatory::Role::post);
    view.setVisible (true);
    int checked = 0, images = 0, clipped = 0;
    juce::String inventory ("language\twidth\theight\tchain\tsession\tfolded\tfont_px\tcomplete\timage\ttext\n");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const juce::String lang (language == i18n::Language::english ? "en" : "ja");
        require (text_style::shownText (text) == i18n::translate (text, language), "notice uses the selected language");
        if (language == i18n::Language::japanese)
            require (text_style::shownText (text) != text, "Japanese notice is translated");
        for (const int width : { 300, 375, 450, 600, 750, 900 })
        for (const bool chain : { false, true })
        for (const int session : { 0, 1, 2 })
        {
            view.setFeedback ({});
            view.setSize (width, width * 2 / 3);
            view.setChainReadout (chain ? "CHAIN LOAD 100% / 100%" : "", true);
            observatory::LiveCompareFooter footer;
            footer.entryEnabled = true;
            footer.active = session == 0;
            footer.postHeldTenthsDb = session == 2 ? -240 : 0;
            footer.preSelected = footer.preWaiting = true;
            footer.recoveryHelp = text.toRawUTF8();
            view.setLiveCompareFooter (footer);
            const auto originalBody = view.analysisBodyBounds();
            int detailsOpened = 0;
            view.onFeedbackDetails = [&] { ++detailsOpened; };
            view.setFeedback (text);
            FeedbackStrip strip;
            strip.setBounds (view.statusStripBounds());
            strip.setPresentationContext (view.presentationContext());
            strip.setFeedback (text);
            auto& painted = view.statusStripFolded() ? static_cast<juce::Component&> (strip)
                                                     : view.feedbackDetailsAnchor();
            text_style::ShownTextLog actual;
            const auto pixels = painted.createComponentSnapshot (painted.getLocalBounds());
            const auto room = painted.getLocalBounds().reduced (view.statusStripFolded() ? 6 : 3,
                                                                view.statusStripFolded() ? 0 : 1);
            const auto font = footer_notice_font::fontForText (view.presentationContext(), text, room.getWidth());
            const bool complete = pixels.isValid() && font.getHeight() >= 12.0f
                && withoutWhitespace (actual.texts().joinIntoString ("")) == withoutWhitespace (text_style::shownText (text))
                && text_style::shownWrappedHeight (text, font, room.getWidth()) <= room.getHeight() + 0.5f;
            if (! complete)
            {
                ++clipped;
                std::cerr << "Keep notice incomplete: " << lang << '/' << width << "/chain" << chain
                          << "/session" << session << '\n';
            }
            for (const auto* id : { "observatory-live-pre", "observatory-live-post", "observatory-live-end", "observatory-menu" })
                if (const auto* action = view.findChildWithID (id); action && action->isVisible())
                    require (! action->getBounds().intersects (view.statusStripBounds()), "notice never covers controls");
            if (! view.statusStripFolded())
            {
                auto* details = dynamic_cast<juce::Button*> (&view.feedbackDetailsAnchor());
                require (details && details->isVisible() && details->isEnabled() && details->onClick,
                         "existing details action remains reachable");
                details->onClick();
                require (detailsOpened == 1, "existing notice click opens details");
            }
            if (! view.statusStripFolded())
                require (! view.analysisBodyBounds().intersects (view.statusStripBounds()), "notice reserves analysis space");
            juce::String imageName;
            if (session == 0)
            {
                imageName = "keep-tail-" + lang + "-" + juce::String (width) + "x" + juce::String (width * 2 / 3)
                    + (chain ? "-chain-on.png" : "-chain-off.png");
                auto snapshot = view.createComponentSnapshot (view.getLocalBounds());
                if (view.statusStripFolded())
                { juce::Graphics graphics (snapshot); graphics.drawImageAt (pixels, strip.getX(), strip.getY()); }
                auto output = juce::File (previewDir).getChildFile (imageName).createOutputStream();
                require (snapshot.isValid() && output && output->setPosition (0) && output->truncate().wasOk()
                    && juce::PNGImageFormat().writeImageToStream (snapshot, *output), "Keep notice preview saved");
                ++images;
            }
            inventory += lang + "\t" + juce::String (width) + "\t" + juce::String (width * 2 / 3)
                + "\t" + juce::String (chain ? 1 : 0) + "\t" + juce::String (session)
                + "\t" + juce::String (view.statusStripFolded() ? 1 : 0) + "\t" + juce::String (font.getHeight())
                + "\t" + juce::String (complete ? 1 : 0) + "\t" + imageName + "\t" + text_style::shownText (text) + "\n";
            view.setFeedback ({});
            require (view.analysisBodyBounds() == originalBody, "dismiss restores analysis space");
            ++checked;
        }
    }
    require (hypha::storage::writeText (juce::File (previewDir).getChildFile ("keep-tail-notices.tsv"), inventory),
             "Keep notice inventory saved");
    std::cout << "Keep notice only: " << checked << " cases; " << images << " previews; " << clipped << " incomplete\n";
    require (checked == 72 && images == 24 && clipped == 0, "all narrow Keep notice cases must paint completely");
}
}
