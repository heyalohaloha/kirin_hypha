#pragma once
#include "FooterNoticeContractChecks.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
namespace hypha::tests
{
template <typename Require>
void verifyReaperTimingUi (Require require)
{
    using namespace live_compare;
    int cases = 0, clipped = 0;
    // Resize/change language on the same screen, as an open editor does.
    observatory::View named (observatory::Role::post); named.setVisible (true);
    live_blind_ui::Component view;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (bool chain : { false, true })
        for (int width : { 300, 375, 450 })
            for (auto reason : { RecoveryReason::reaperFirstCycle, RecoveryReason::reaperDelayUnverified,
                                 RecoveryReason::reaperLoopTooShort })
            {
                named.setSize (width, width * 2 / 3);
                named.setChainReadout (chain ? "CHAIN LOAD 100% / 100%" : "", true);
                named.setFeedback ({});
                const auto originalBody = named.analysisBodyBounds();
                Status state; state.active = state.preSelected = state.preWaiting = true;
                state.observation = reason;
                const auto* notice = live_compare_ui::namedRecovery (state);
                observatory::LiveCompareFooter footer;
                footer.active = footer.entryEnabled = footer.preSelected = footer.preWaiting = true;
                footer.recoveryHelp = notice;
                named.setLiveCompareFooter (footer);
                named.setFeedback (notice);
                require (named.statusStripFolded() || !named.analysisBodyBounds().intersects (named.statusStripBounds()),
                         "complete notice reserves space outside the analysis body");
                require (footer_notice::retainedInFooter (named, notice), "REAPER footer paint fits and retains full text");
                FeedbackStrip strip;
                strip.setBounds (named.statusStripBounds());
                strip.setPresentationContext (named.presentationContext()); strip.setFeedback (notice);
                auto& painted = named.statusStripFolded() ? static_cast<juce::Component&> (strip)
                                                       : named.feedbackDetailsAnchor();
                text_style::ShownTextLog actual;
                const auto noticePixels = painted.createComponentSnapshot (painted.getLocalBounds());
                const bool complete = noticePixels.isValid()
                    && actual.texts().contains (text_style::shownText (notice));
                if (!complete)
                {
                    ++clipped;
                    std::cerr << "Clipped LISTEN: language=" << static_cast<int> (language)
                              << " width=" << width << " reason=" << static_cast<int> (reason) << '\n';
                }
                LiveBlindStatus blind; blind.stage = BlindStage::preparing; blind.observation = reason;
                const auto screen = live_blind_ui::screenFor (blind, true, 1.0f);
                require (!screen.sourcesShown && screen.endEnabled, "unproven REAPER waits without sources, END remains");
                require (!screen.cause.contains ("no verified audio clock"), "REAPER no longer claims absent audio clock");
                view.setSize (width, width * 2 / 3); view.setState (blind, true, 1.0f);
                auto pixels = view.createComponentSnapshot (view.getLocalBounds());
                require (pixels.isValid(), "REAPER waiting screen rendered");
                for (auto* child : view.getChildren())
                {
                    if (!child->isVisible()) continue;
                    require (view.getLocalBounds().contains (child->getBounds()), "REAPER controls stay inside view");
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        juce::AttributedString text;
                        text.append (text_style::shownText (label->getText()), label->getFont(), juce::Colours::white);
                        juce::TextLayout layout;
                        const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                        layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                        require (layout.getHeight() <= bounds.getHeight(), "REAPER bilingual text fits at 100/125/150 percent");
                    }
                }
                const auto dir = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
                if (dir.isNotEmpty())
                {
                    auto out = juce::File (dir).getChildFile ("reaper-" + juce::String (static_cast<int> (language))
                        + "-" + juce::String (width) + "-" + juce::String (static_cast<int> (reason))
                        + (chain ? "-chain" : "") + ".png").createOutputStream();
                    auto namedPixels = named.createComponentSnapshot (named.getLocalBounds());
                    if (named.statusStripFolded())
                    {
                        juce::Graphics g (namedPixels);
                        g.drawImageAt (noticePixels, strip.getX(), strip.getY());
                    }
                    auto namedOut = juce::File (dir).getChildFile ("listen-" + juce::String (static_cast<int> (language))
                        + "-" + juce::String (width) + "-" + juce::String (static_cast<int> (reason))
                        + (chain ? "-chain" : "") + ".png").createOutputStream();
                    require (namedOut && namedOut->setPosition (0) && namedOut->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream (namedPixels, *namedOut), "LISTEN preview saved");
                    require (out && out->setPosition (0) && out->truncate().wasOk()
                        && juce::PNGImageFormat().writeImageToStream (pixels, *out), "REAPER preview saved");
                }
                named.setFeedback ({});
                require (named.analysisBodyBounds() == originalBody, "clearing the wait restores the analysis area");
                ++cases;
            }
    }
    std::cout << "REAPER full footer: " << cases << " cases, " << clipped << " clipped\n";
    require (clipped == 0, "REAPER waiting instructions must paint in full, without ellipsis");
    std::cout << "REAPER timing UI: " << cases << " language/size/reason cases PASS\n";
}

template <typename Require>
void verifyOtherHostListenText (Require require)
{
    using namespace live_compare;
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    for (int width : { 300, 375, 450 })
    for (int host = 0; host < 4; ++host)
    {
        const i18n::ScopedLanguage scoped (language);
        Status state; state.active = state.preSelected = state.preWaiting = true;
        const char* expected = "Checking PRE: auto-resume; POST plays"; // Studio One, linear
        state.observation = RecoveryReason::calibrating;
        if (host == 1) // Studio Pro, known loop with retained MATCH
        {
            state.observation = RecoveryReason::loopWaiting; state.matchHeld = true;
            expected = "Loop timing pending; MATCH held; POST plays";
        }
        if (host == 2) // Logic AU Cycle
        {
            state.loopBlock = LoopPairingBlock::auCycle;
            expected = "Turn Cycle off to compare (POST)";
        }
        if (host == 3) // Pro Tools, delay compensation disabled
        {
            state.compensationOff = true;
            expected = "Compensation off: enable it (POST)";
        }
        const auto* notice = live_compare_ui::namedRecovery (state);
        require (juce::String (notice) == expected, "other host LISTEN text is unchanged");
        require (language != i18n::Language::japanese || text_style::shownText (notice) != notice,
                 "other host notices retain Japanese translations");
        observatory::View view (observatory::Role::post); view.setVisible (true);
        view.setSize (width, width * 2 / 3);
        observatory::LiveCompareFooter footer;
        footer.active = footer.preSelected = footer.preWaiting = true;
        footer.recoveryHelp = notice;
        view.setLiveCompareFooter (footer);
        view.setFeedback (notice);
        require (footer_notice::retainedInFooter (view, notice),
                 "other hosts keep bounded bilingual paint and full details");
        ++cases;
    }
    std::cout << "Other host LISTEN text/layout: " << cases << " cases PASS\n";
}
}
