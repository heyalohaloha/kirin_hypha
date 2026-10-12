#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaTextStyle.h"

namespace hypha::tests
{
template <typename Require>
void verifyLiveTimingAvailabilityUi (Require require)
{
    using namespace live_compare;
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (int width : { 300, 375, 450 })
            for (auto stage : { BlindStage::preparing, BlindStage::approval, BlindStage::settling })
                for (auto reason : { RecoveryReason::none, RecoveryReason::calibrating,
                                     RecoveryReason::loopUnproven, RecoveryReason::loopClockUnavailable, RecoveryReason::clockMissing })
                {
                    LiveBlindStatus state;
                    state.stage = stage; state.observation = reason;
                    state.lowerPostDb = -8.0;
                    state.timingUnavailable = reason == RecoveryReason::loopClockUnavailable;
                    state.clockUnavailable = reason == RecoveryReason::clockMissing;
                    const auto screen = live_blind_ui::screenFor (state, true, 1.0f);
                    require (screen.instruction.contains ("unavailable") == (state.timingUnavailable || state.clockUnavailable),
                             "ordinary AU stages never show the unavailable instruction in either language");
                    if (! state.timingUnavailable && ! state.clockUnavailable && stage == BlindStage::approval)
                        require (screen.approveShown && screen.instruction == "Lower POST to match levels?",
                                 "approval question and button belong to the same ordinary state");
                    if (reason == RecoveryReason::loopUnproven && stage != BlindStage::approval)
                    {
                        require (screen.guidanceShown && screen.recovery == "Keep playing; resumes when confirmed",
                                 "a timing check gives its reason and automatic remedy");
                        require (i18n::tr (screen.cause).contains (language == i18n::Language::japanese
                            ? juce::String::fromUTF8 (u8"未確認") : juce::String ("not verified")),
                            "both languages describe an unconfirmed observation rather than an impossible comparison");
                    }
                    live_blind_ui::Component view;
                    view.setSize (width, width * 2 / 3); view.setState (state, true, 1.0f);
                    const auto pixels = view.createComponentSnapshot (view.getLocalBounds());
                    require (pixels.isValid(), "availability screen renders");
                    for (auto* child : view.getChildren())
                    {
                        if (! child->isVisible()) continue;
                        require (! child->getBounds().isEmpty() && view.getLocalBounds().contains (child->getBounds()),
                                 "availability controls fit at 100/125/150 percent");
                        if (auto* label = dynamic_cast<juce::Label*> (child))
                        {
                            juce::AttributedString text;
                            text.append (text_style::shownText (label->getText()), label->getFont(), juce::Colours::white);
                            juce::TextLayout layout;
                            const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                            layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                            require (layout.getHeight() <= bounds.getHeight(), "day-to-day waiting and approval translations fit");
                        }
                    }
                    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
                    if (preview.isNotEmpty() && (reason == RecoveryReason::loopUnproven || reason == RecoveryReason::clockMissing))
                    {
                        const auto file = juce::File (preview).getChildFile ((state.clockUnavailable ? "clock-unavailable-" : "availability-")
                            + juce::String (static_cast<int> (language)) + "-" + juce::String (width)
                            + "-" + juce::String (static_cast<int> (stage)) + ".png");
                        auto out = file.createOutputStream();
                        require (out && out->setPosition (0) && out->truncate().wasOk()
                            && juce::PNGImageFormat().writeImageToStream (pixels, *out), "disposable availability preview saved");
                    }
                    ++cases;
                }
    }
    std::cout << "Timing availability UI: PASS " << cases << " language/size/phase/reason cases\n";
}
}
