#pragma once

// 2026-10-05（300%）：足元の REF の状態の文と承認のボタンが日本語で切れていた
// （「BはAを0.8 dB下げると合う / Aを下…」「Aを0.8 dB下げてBを…」「Bの追従は上限で停止 / Aを0.8 dB下げ中 / Aより…」
// 「VはAに追従中（直近10…」）。足元の状態の場所の実際の幅で、両言語とも、承認のボタンは文字に合い、状態の文は
// 少なくとも最初の区切りが切れずに出る（後ろの区切りは入りきらなければ省き、指すと全文を出す）。
#include "ReferenceControlLookup.h"
#include "ReferenceGuideContractTest.h"

#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaTheme.h"

namespace hypha::tests
{
inline void verifyReferenceStatusFits()
{
    using namespace reference_guide_contract;
    using Tracking = reference_audition::TrackingState;
    observatory::View shell (observatory::Role::post);
    shell.setSize (900, 600);
    shell.setDomain (observatory::Domain::reference);
    const auto place = shell.statusStripBounds();
    require (place.getWidth() > 300 && place.getHeight() > 0, "the footer has a status place at 300%");
    const auto context = presentation::forEditor (900, 600);
    reference_ui::Component panel;
    panel.setVisible (true);
    panel.setPresentationContext (context);
    panel.setSize (888, 470);
    panel.setStatusInFooter (true);
    const juce::Rectangle<int> strip (0, 0, place.getWidth(), place.getHeight());

    struct Case
    {
        const char* name;
        reference_ui::State state;
    };
    auto base = named ("ready");
    base.separateComparisons = base.libraryReceived = base.osOnline = base.transportPlaying = true;
    base.osAccess = os_access::State::ready;
    std::vector<Case> cases;
    {
        auto offer = base;
        offer.comparisonSlot = 3;
        offer.action = { reference_ui::ActionKind::lowerAAndPlay, { 3, -0.8, "b-song", 1, 1 } };
        offer.status = "B NEEDS A 0.8 DB LOWER";
        offer.actionText = "LOWER A 0.8 DB & PLAY B";
        cases.push_back ({ "B approval", offer });
    }
    for (const auto tracking : { Tracking::following, Tracking::stoppedCeiling })
    {
        auto playing = base;
        playing.comparisonSlot = playing.audibleComparisonSlot = 3;
        playing.bSelected = true;
        playing.tracking = tracking;
        playing.appliedGainDb = 0.0;
        playing.heldAttenuationDb = -0.8;  // 足元の RETURN +0.8 dB が言う
        playing.peakShortfallDb = tracking == Tracking::stoppedCeiling ? 0.4 : 0.0;
        cases.push_back ({ tracking == Tracking::following ? "B following, A held" : "B stopped at the ceiling, under A", playing });
    }
    {
        auto version = base;
        version.comparisonSlot = version.audibleComparisonSlot = 1;
        version.bSelected = true;
        version.tracking = Tracking::following;
        version.appliedGainDb = 0.6;
        version.status = "BLIND NEEDS HEADROOM / A RETURNS +0.6 dB ON END";
        version.actionText = "LOWER A 0.6 dB & START";
        cases.push_back ({ "V following, Blind needs headroom", version });
        version.bSelected = false;
        cases.push_back ({ "V Blind needs headroom", version });
    }
    const auto readout = labelFont (context, typography::TextRole::readout, typography::Composition::information);
    const auto action = labelFont (context, typography::TextRole::action, typography::Composition::information);
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        const juce::String tag = language == i18n::Language::japanese ? "JA" : "EN";
        for (auto& item : cases)
        {
            panel.setState (item.state);
            panel.footerStatusStrip().setBounds (strip);
            panel.footerStatusStrip().resized();
            const auto layout = panel.statusTextLayout (strip);
            const auto shown = text_style::shownText (layout.line.text);
            const auto first = shown.upToFirstOccurrenceOf (" /", false, false).trimEnd();
            require (first.isNotEmpty() && layout.text.startsWith (first)
                         && text_style::shownWidth (readout, first) <= static_cast<float> (layout.textArea.getWidth()),
                     "the footer status shows at least its first part whole at 300% (" + juce::String (item.name) + ", "
                         + tag + "): " + shown);
            require (! layout.line.text.contains ("A LOWERED"),
                     "the held attenuation is said by RETURN in the footer, not twice (" + juce::String (item.name) + ")");
            if (item.state.actionText.isNotEmpty())
            {
                auto* button = dynamic_cast<juce::TextButton*> (findReferenceControl (panel, "reference-action"));
                require (button != nullptr && button->isVisible()
                             && static_cast<float> (button->getWidth())
                                    >= text_style::shownWidth (action, button->getButtonText()) + 12.0f
                             && button->getRight() <= strip.getRight(),
                         "the approval button fits what it says at 300% (" + juce::String (item.name) + ", " + tag + ")");
            }
            if (juce::String (item.name) == "B approval")
                require (! layout.cut, "the approval's amount is said whole beside its button (" + tag + "): " + layout.text);
        }
    }
}
}
