#pragma once
#include <iostream>
#include "ListenTextCases.h"
#include "ListenTextPaintChecks.h"
#include "../src/AtomicFile.h"
namespace hypha::tests
{
inline juce::String withoutWhitespace (juce::String text)
{ return text.removeCharacters (" \t\r\n"); }

template <typename Require>
void verifyAllListenText (Require require)
{
    const auto notices = listen_text::notices();
    const auto blindCases = listen_text::blindCases();
    int namedCount = 0, blindCount = 0, clipped = 0;
    juce::StringArray failures;
    juce::String inventory;
    const auto previewDir = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    observatory::View named (observatory::Role::post); named.setVisible (true);
    live_blind_ui::Component blind;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        require (text_style::shownText ("Rendering offline: press again after it finishes")
                     == (language == i18n::Language::japanese
                         ? juce::String::fromUTF8 (u8"書き出し中です。終わってから押してください")
                         : juce::String ("Rendering offline: press again after it finishes")),
                 "offline return notice has the intended wording in each language");
        if (language == i18n::Language::japanese)
            require (i18n::tr ("Cannot write to folder. Check access.") != "Cannot write to folder. Check access.",
                     "storage failure guidance is translated");
        if (language == i18n::Language::japanese)
            require (i18n::tr ("2MIX has no DRUM; showing TIME / HISTORY") != "2MIX has no DRUM; showing TIME / HISTORY",
                     "page-change guidance is translated");
        if (language == i18n::Language::japanese)
            require (i18n::tr ("Measurements saved; waiting for Kirin OS audio (auto link)") != "Measurements saved; waiting for Kirin OS audio (auto link)",
                     "audio-binding guidance is translated");
        if (language == i18n::Language::japanese)
            require (i18n::tr ("No new PRE processing; DAW may pause it (POST)") != "No new PRE processing; DAW may pause it (POST)",
                     "PRE processing guidance is translated");
        if (language == i18n::Language::japanese)
            for (const auto& item : notices)
                if (item.text.startsWith ("Waiting up to 10 s, then "))
                    require (i18n::tr (item.text) != item.text, "automatic wait guidance is translated");
        if (language == i18n::Language::japanese)
            for (const auto* text : { "Before realtime bounce, press END and wait for normal level",
                                      "Press END to finish the current comparison, then try again" })
                require (i18n::tr (text) != text, "comparison instructions are translated");
        for (const auto& item : notices)
            inventory += juce::String (static_cast<int> (language)) + "\t" + item.text + "\t"
                + text_style::shownText (item.text) + "\n";
        for (int width : { 300, 375, 450, 600, 750, 900 })
        {
            for (bool chain : { false, true })
            for (int session : { 0, 1, 2 }) // listening, entry refusal / END, held POST attenuation
            for (const auto& item : notices)
            {
                if (width > 450 && item.text != "Rendering offline: press again after it finishes"
                    && item.text != live_compare_ui::bypassHoldsReturnNotice
                    && item.text != "Cannot write to folder. Check access."
                    && item.text != "2MIX has no DRUM; showing TIME / HISTORY"
                    && item.text != "Measurements saved; waiting for Kirin OS audio (auto link)"
                    && item.text != "No new PRE processing; DAW may pause it (POST)"
                    && ! item.text.startsWith ("Waiting up to 10 s, then ")
                    && item.text != "Before realtime bounce, press END and wait for normal level"
                    && item.text != "Press END to finish the current comparison, then try again") continue;
                named.setFeedback ({});
                named.setSize (width, width * 2 / 3);
                named.setChainReadout (chain ? "CHAIN LOAD 100% / 100%" : "", true);
                observatory::LiveCompareFooter footer;
                footer.entryEnabled = true;
                footer.active = session == 0;
                footer.postHeldTenthsDb = session == 2 ? -240 : 0;
                footer.preSelected = footer.preWaiting = true;
                footer.recoveryHelp = item.text.toRawUTF8();
                named.setLiveCompareFooter (footer);
                const auto originalBody = named.analysisBodyBounds();
                named.setFeedback (item.text);
                FeedbackStrip strip;
                strip.setBounds (named.statusStripBounds());
                strip.setPresentationContext (named.presentationContext()); strip.setFeedback (item.text);
                auto& painted = named.statusStripFolded() ? static_cast<juce::Component&> (strip)
                                                         : named.feedbackDetailsAnchor();
                text_style::ShownTextLog actual;
                const auto pixels = painted.createComponentSnapshot (painted.getLocalBounds());
                const auto room = painted.getLocalBounds().reduced (named.statusStripFolded() ? 6 : 3,
                                                                    named.statusStripFolded() ? 0 : 1);
                const auto font = footer_notice_font::fontForText (named.presentationContext(), item.text, room.getWidth());
                if (item.text == "Cannot write to folder. Check access." || item.text == "2MIX has no DRUM; showing TIME / HISTORY"
                    || item.text == "Measurements saved; waiting for Kirin OS audio (auto link)"
                    || item.text == "No new PRE processing; DAW may pause it (POST)"
                    || item.text.startsWith ("Waiting up to 10 s, then ")
                    || item.text == "Before realtime bounce, press END and wait for normal level"
                    || item.text == "Press END to finish the current comparison, then try again")
                    require (font.getHeight() >= 14.0f, "storage guidance is readable at every size");
                const bool complete = pixels.isValid()
                    && withoutWhitespace (actual.texts().joinIntoString ("")) == withoutWhitespace (text_style::shownText (item.text))
                    && text_style::shownWrappedHeight (item.text, font, room.getWidth()) <= room.getHeight() + 0.5f;
                if (!complete)
                {
                    ++clipped;
                    failures.addIfNotAlreadyThere ("LISTEN " + juce::String (static_cast<int> (language)) + "/"
                        + juce::String (width) + "/session" + juce::String (session)
                        + (chain ? "/chain " : " ") + item.text);
                }
                for (const auto* id : { "observatory-live-pre", "observatory-live-post", "observatory-live-end", "observatory-menu" })
                    if (const auto* action = named.findChildWithID (id); action && action->isVisible())
                        require (!action->getBounds().intersects (named.statusStripBounds()), "notice never covers LISTEN controls");
                if (!named.statusStripFolded())
                    require (!named.analysisBodyBounds().intersects (named.statusStripBounds()), "notice reserves analysis space");
                if (previewDir.isNotEmpty() && session == 0 && listen_text::previewNotice (item.text))
                {
                    auto snapshot = named.createComponentSnapshot (named.getLocalBounds());
                    if (named.statusStripFolded()) { juce::Graphics g (snapshot); g.drawImageAt (pixels, strip.getX(), strip.getY()); }
                    auto file = juce::File (previewDir).getChildFile ("footer-" + juce::String (namedCount) + ".png");
                    auto out = file.createOutputStream();
                    require (out && out->setPosition (0) && out->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream (snapshot, *out), "footer preview saved");
                }
                named.setFeedback ({});
                require (named.analysisBodyBounds() == originalBody, "clearing notice restores analysis space");
                ++namedCount;
            }
            for (const auto& item : blindCases)
            {
                if (width > 450 && ! (item.state.stage == live_compare::BlindStage::failed
                    && item.state.waiting == live_compare::MatchFailure::notProven && item.state.prolongedWait)) continue;
                blind.setSize (width, width * 2 / 3); blind.setState (item.state, item.playing, item.post);
                const auto pixels = blind.createComponentSnapshot (blind.getLocalBounds());
                require (pixels.isValid(), "BLIND screen rendered");
                for (auto* child : blind.getChildren())
                {
                    if (!child->isVisible()) continue;
                    require (blind.getLocalBounds().contains (child->getBounds()), "BLIND controls inside screen");
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        const auto room = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                        juce::AttributedString text;
                        text.append (text_style::shownText (label->getText()), label->getFont(), juce::Colours::white);
                        juce::TextLayout layout; layout.createLayout (text, static_cast<float> (room.getWidth()));
                        const auto height = std::max (layout.getHeight(), text_style::shownWrappedHeight (
                            label->getText(), label->getFont(), room.getWidth()));
                        if (height > room.getHeight() + 0.5f || !listen_text::labelPaintsCompletely (*label))
                        {
                            ++clipped;
                            failures.addIfNotAlreadyThere ("BLIND " + juce::String (static_cast<int> (language)) + "/"
                                + juce::String (width) + " " + label->getText());
                        }
                        for (auto* other : blind.getChildren())
                            if (other != label && other->isVisible())
                                if (label->getBounds().intersects (other->getBounds()))
                                { ++clipped; failures.addIfNotAlreadyThere ("BLIND overlap " + label->getText()); }
                    }
                }
                if (previewDir.isNotEmpty() && ((item.state.stage == live_compare::BlindStage::preparing
                    && item.state.reason == live_compare::RecoveryReason::none && item.state.timingUnavailable)
                    || (item.state.stage == live_compare::BlindStage::active && item.state.trial.usedPostFallback)))
                {
                    auto out = juce::File (previewDir).getChildFile ("blind-" + juce::String (blindCount) + ".png").createOutputStream();
                    require (out && out->setPosition (0) && out->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream (pixels, *out), "BLIND preview saved");
                }
                ++blindCount;
            }
        }
    }
    if (previewDir.isNotEmpty())
        require (hypha::storage::writeText (juce::File (previewDir).getChildFile ("notices.tsv"), inventory), "notice inventory saved");
    for (const auto& failure : failures) std::cerr << "Clipped " << failure << '\n';
    std::cout << "All LISTEN text: " << notices.size() << " notices / " << namedCount << " cases; BLIND: "
              << blindCases.size() << " screens / " << blindCount << " cases; " << clipped << " clipped\n";
    require (clipped == 0, "all LISTEN / BLIND instructions must paint completely at the tested sizes");
}

template <typename Require>
void verifyFooterSurfaceRestoration (Require require)
{
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    for (auto role : { observatory::Role::pre, observatory::Role::post })
    for (auto domain : { observatory::Domain::level, observatory::Domain::time,
                        observatory::Domain::frequency, observatory::Domain::space, observatory::Domain::reference })
    for (const auto preset : observatory::sizePresets)
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View view (role);
        view.setSize (preset.width, preset.height); view.setDomain (domain);
        const auto original = view.analysisBodyBounds();
        view.setFeedback ("This DAW has no verified audio clock; PRE is unavailable (POST)");
        require (!view.analysisBodyBounds().isEmpty(), "notice leaves an analysis body in every domain");
        if (!view.statusStripFolded())
            require (!view.analysisBodyBounds().intersects (view.statusStripBounds()), "notice and analysis stay separate");
        if (domain == observatory::Domain::level && preset.width >= 600)
        {
            auto* history = view.findChildWithID ("history-previous-tp");
            require (history && history->isVisible(), "history control exists before Record view");
            const auto beforeRecord = history->getBounds();
            KirinRecordDisplay record {};
            record.phase = KIRIN_RECORD_DISPLAY_FINALIZING;
            view.setRecordDisplay (record, true); view.setRecordDisplay (record, false);
            require (history->getBounds() == beforeRecord,
                     "footer reservation and Record restoration agree on history control geometry");
        }
        view.setFeedback ({});
        require (view.analysisBodyBounds() == original, "every domain restores its body after notice");
        ++cases;
    }
    std::cout << "Footer surface restoration: " << cases << " cases\n";
}
}
