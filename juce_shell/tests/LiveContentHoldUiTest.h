#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaTextStyle.h"

namespace hypha::tests
{
template <typename Require>
void verifyLiveContentHoldUi (Require require)
{
    using namespace live_compare;
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        require (i18n::tr (comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED, 12))
            .contains (language == i18n::Language::english ? juce::String ("POST BYPASSED")
                : juce::String::fromUTF8 (u8"POSTをバイパス中")), "real ABI 12 has its own translated bypass reason");
        for (int width : { 300, 375, 450, 600, 900 })
            for (auto stage : { BlindStage::preparing, BlindStage::settling, BlindStage::approval,
                                BlindStage::active, BlindStage::invalidated })
                for (bool playing : { false, true })
                {
                    LiveBlindStatus state;
                    state.stage = stage; state.contentHeld = true; state.contentLagFrames = 96;
                    state.trial.played = 3; state.trial.revealed = true;
                    const auto screen = live_blind_ui::screenFor (state, playing, 0.5f);
                    require (screen.guidanceShown == (stage == BlindStage::invalidated)
                        && screen.endEnabled && screen.sourcesShown == (stage != BlindStage::approval && stage != BlindStage::invalidated)
                        && screen.sourceOneEnabled == (stage == BlindStage::active)
                        && screen.approveShown == (stage == BlindStage::approval) && !screen.revealShown,
                        "a short notice keeps the trial and source controls without a waiting notice");
                    state.prolongedWait = true;
                    const auto prolonged = live_blind_ui::screenFor (state, playing, 0.5f);
                    const bool told = stage != BlindStage::invalidated;
                    require (prolonged.guidanceShown && (prolonged.recovery == live_compare_ui::chainDelayNotice()) == told
                        && (! told || prolonged.cause == live_compare_ui::cause (RecoveryReason::preLate))
                        && ! (told && prolonged.recovery.contains ("POST")) && ! prolonged.alignOffShown,
                        "a long content notice names the chain delay without an output, ALIGN OFF or END");
                    live_blind_ui::Component view;
                    view.setSize (width, width * 2 / 3); view.setState (state, playing, 0.5f);
                    const auto image = view.createComponentSnapshot (view.getLocalBounds());
                    require (image.isValid(), "hold screen renders");
                    for (auto* child : view.getChildren())
                    {
                        if (! child->isVisible()) continue;
                        require (view.getLocalBounds().contains (child->getBounds()) && ! child->getBounds().isEmpty(),
                                 "notice controls remain inside the editor at 100 to 300 percent");
                        if (auto* label = dynamic_cast<juce::Label*> (child))
                        {
                            juce::AttributedString text;
                            text.append (text_style::shownText (label->getText()), label->getFont(), juce::Colours::white);
                            juce::TextLayout layout;
                            const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                            layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                            require (layout.getHeight() <= bounds.getHeight(), "both languages fit the hold instructions without truncation");
                        }
                    }
                    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_SHELL_PREVIEW_DIR", {});
                    if (preview.isNotEmpty() && stage == BlindStage::approval && playing)
                    {
                        const auto file = juce::File (preview).getChildFile ("content-hold-"
                            + juce::String (static_cast<int> (language)) + "-" + juce::String (width) + ".png");
                        auto out = file.createOutputStream();
                        require (out && out->setPosition (0) && out->truncate().wasOk()
                            && juce::PNGImageFormat().writeImageToStream (image, *out), "hold preview replaces disposable pixels");
                    }
                    ++cases;
                }
    }
    // BLIND never learns the assignment from the notice: the same words for either source and
    // either assignment until REVEAL. After REVEAL the estimate is told as a possibility.
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        juce::String seen;
        for (bool firstPre : { false, true })
            for (int selected : { 1, 2 })
            {
                LiveBlindStatus state;
                state.stage = BlindStage::active; state.contentHeld = state.prolongedWait = true;
                state.contentLagFrames = 96; state.contentNoticeMs = 2.0;
                state.trial.active = true; state.trial.firstPre = firstPre; state.trial.selected = selected;
                const auto screen = live_blind_ui::screenFor (state, true, 1.0f);
                const auto words = i18n::tr (screen.cause) + "|" + i18n::tr (screen.recovery) + "|" + i18n::tr (screen.instruction);
                require (seen.isEmpty() || words == seen, "the BLIND notice is the same for every source and assignment");
                seen = words;
            }
        LiveBlindStatus revealed;
        revealed.stage = BlindStage::active; revealed.contentNoticeMs = -0.4;
        revealed.trial.active = revealed.trial.revealed = true; revealed.trial.played = 3;
        const auto result = i18n::tr (live_blind_ui::screenFor (revealed, true, 1.0f).instruction);
        require (result == (language == i18n::Language::english ? juce::String ("Audio estimate: PRE may have been 0.40 ms off")
                : juce::String::fromUTF8 (u8"音の推定：PREが0.40 msずれていた可能性")),
                 "RESULT tells the estimate in both languages, without claiming POST played");
        revealed.trial.usedPostFallback = revealed.trial.fallbackForTiming = true;
        require (i18n::tr (live_blind_ui::screenFor (revealed, true, 1.0f).instruction) == result,
                 "the estimate also explains a short timing substitute");
        revealed.trial.fallbackForCeiling = true;
        require (! i18n::tr (live_blind_ui::screenFor (revealed, true, 1.0f).instruction).contains ("0.40"),
                 "a ceiling substitute is still the first thing RESULT says");
        cases += 5;
    }
    std::cout << "Content timing notice UI: PASS " << cases << " language/size/phase/transport/assignment cases\n";
}
}
