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
        for (int width : { 300, 375, 450 })
            for (auto stage : { BlindStage::preparing, BlindStage::settling, BlindStage::approval,
                                BlindStage::active, BlindStage::invalidated })
                for (bool playing : { false, true })
                {
                    LiveBlindStatus state;
                    state.stage = stage; state.contentHeld = state.timingUnavailable = true;
                    state.trial.played = 3; state.trial.revealed = true;
                    const auto screen = live_blind_ui::screenFor (state, playing, 0.5f);
                    require (screen.guidanceShown && screen.endEnabled && ! screen.sourcesShown
                        && ! screen.approveShown && ! screen.revealShown, "hold cannot approve, reveal, or leave an anonymous source active");
                    require (screen.cause == live_compare_ui::cause (RecoveryReason::contentChanged)
                        && screen.recovery == "Stop/play DAW; END, then BLIND",
                        "content hold overrides generic clock unavailability and gives its immediate remedy");
                    live_blind_ui::Component view;
                    view.setSize (width, width * 2 / 3); view.setState (state, playing, 0.5f);
                    const auto image = view.createComponentSnapshot (view.getLocalBounds());
                    require (image.isValid(), "hold screen renders");
                    for (auto* child : view.getChildren())
                    {
                        if (! child->isVisible()) continue;
                        require (view.getLocalBounds().contains (child->getBounds()) && ! child->getBounds().isEmpty(),
                                 "hold controls remain inside the editor at 100/125/150 percent");
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
    std::cout << "Content hold UI: PASS " << cases << " language/size/phase/transport cases\n";
}
}
