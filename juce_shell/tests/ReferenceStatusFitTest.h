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
#include "../src/HyphaReferenceStages.h"
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
        offer.bSelected = true; offer.audibleComparisonSlot = 3; offer.tracking = Tracking::following;
        offer.peakShortfallDb = 0.8;
        offer.action = { reference_ui::ActionKind::lowerAAndPlay, { 3, -0.8, "b-song", 1, 1 } };
        offer.actionText = "LOWER A 0.8 DB";
        cases.push_back ({ "B approval", offer });
        offer.rangeShortfallDb = 4.0;
        cases.push_back ({ "B range limit with approval", offer });
    }
    for (const double shortfall : { 0.0, 0.4 })
    {
        auto playing = base;
        playing.comparisonSlot = playing.audibleComparisonSlot = 3;
        playing.bSelected = true;
        playing.tracking = Tracking::following;
        playing.appliedGainDb = 0.0;
        playing.heldAttenuationDb = -0.8;  // 足元の RETURN +0.8 dB が言う
        playing.peakShortfallDb = shortfall;
        cases.push_back ({ shortfall > 0.0 ? "B at the ceiling, under A" : "B following, A held", playing });
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
    {
        // 2026-11-11：V の Version を替える間は前の Version が鳴る。どちらが鳴り、どちらの位置を合わせているかを言う。
        using namespace reference_audition;
        Snapshot runtime;
        auto playing = std::make_shared<Snapshot>();
        playing->presetId = "p"; playing->checkId = "c"; playing->candidateId = "old";
        playing->bSelected = playing->auditionBuffered = runtime.transportPlaying = true;
        runtime.versionSelection = playing; runtime.checkSelection = std::make_shared<Snapshot>();
        runtime.versions = { { "p/c/old", "Mix 3", {}, false }, { "p/c/new", "Mix 4", {}, false } };
        runtime.selectedVersionId = "p/c/new";
        auto version = base;
        version.comparisonSlot = 1;
        version.bSelected = true;
        version.tracking = Tracking::following;
        for (const int audible : { 0, 1 })
        {
            runtime.audibleComparisonSlot = version.audibleComparisonSlot = audible;
            reference_ui::runtime_view::setSourceSteps (version, runtime);
            require ((reference_ui::referenceStatusLine (version).text == "V plays Mix 3; positioning Mix 4") == (audible == 1),
                     "while V is heard, the line names the Version that plays and the one being positioned");
        }
        require (i18n::translate (reference_ui::referenceStatusLine (version).text, i18n::Language::japanese)
                     == juce::String::fromUTF8 (u8"VはMix 3を再生中。Mix 4の位置を合わせています"),
                 "the Version switch line has Japanese with both names");
        cases.push_back ({ "V plays one Version, positions the next", version });
        // 足りない差と保存先の知らせが先、替え中の文は後（入りきらなければ後ろから省く）。
        auto limited = version;
        limited.rangeShortfallDb = 4.0;
        limited.status = "LOW DISK SPACE / REFERENCE AVAILABLE";
        const auto both = reference_ui::referenceStatusLine (limited).text;
        require (both.startsWith ("V 4.0 DB UNDER A (FOLLOW LIMIT)  /  LOW DISK SPACE") && both.endsWith ("positioning Mix 4"),
                 "a follow limit and a storage notice stay ahead of the Version switch line: " + both);
        cases.push_back ({ "V switch with follow limit and storage", limited });
        limited.rangeShortfallDb = 0.0; limited.peakShortfallDb = 0.8; limited.status = {};
        require (reference_ui::referenceStatusLine (limited).text.startsWith ("0.8 DB UNDER A (PEAK LIMIT)  /  V plays"),
                 "a peak limit stays ahead of the Version switch line");
        // V が選ばれていても、ページが足りず A が鳴っている間は「V が鳴る」と言わない（読み込みの案内のまま）。
        playing->auditionBuffered = false;
        reference_ui::runtime_view::setSourceSteps (version, runtime);
        require (version.versionPlaying.isEmpty() && ! reference_ui::referenceStatusLine (version).text.contains ("V plays"),
                 "while V's pages starve and A plays, the line does not say V plays");
        playing->auditionBuffered = true;
        runtime.selectedVersionId = "p/c/old";
        reference_ui::runtime_view::setSourceSteps (version, runtime);
        require (version.versionPlaying.isEmpty() && ! reference_ui::referenceStatusLine (version).text.contains ("positioning"),
                 "the line says nothing of a switch once the chosen Version plays");
    }
    for (const int slot : { 1, 3 })
        for (const double residual : { -12.3, 12.3 })
        {
            auto limited = base;
            limited.comparisonSlot = limited.audibleComparisonSlot = slot;
            limited.bSelected = true; limited.tracking = Tracking::following;
            limited.rangeShortfallDb = residual;
            cases.push_back ({ "following at range limit", limited });
        }
    // 2026-10-06：段階の表の全部の段階 × 全部の役（今までは 5 つの状態だけだった）。
    std::vector<juce::String> stageNames;
    stageNames.reserve (static_cast<size_t> (reference_ui::stageCount) * 3);  // Case は名前の文字を指すので動かさない
    for (int index = 0; index < reference_ui::stageCount; ++index)
        for (const int slot : { 1, 2, 3 })
        {
            auto stage = base;
            stage.comparisonSlot = slot;
            stage.songSets = { { "set", "Set   1 / 1" } };
            stage.versionStep = stage.checkStep = stage.referenceStep = static_cast<reference_ui::SourceStep> (index);
            stageNames.push_back (juce::String (reference_ui::roleLetter (slot)) + " stage " + juce::String (index));
            cases.push_back ({ stageNames.back().toRawUTF8(), stage });
        }
    {
        auto failed = base;
        failed.comparisonSlot = 1;
        failed.bSelected = false;
        failed.pendingAudition = { 1, reference_audition::PendingAuditionView::Stage::sourceChanged,
                                   "reference_audio_read_failed" };
        require (reference_ui::referenceStatusLine (failed).text.contains ("AUDIO READ FAILED"),
                 "the stopped case exercises the read failure");
        cases.push_back ({ "V audio read failure", failed });
        failed.pendingAudition = { 1, reference_audition::PendingAuditionView::Stage::checking,
                                   "reference_audio_read_failed" };
        require (reference_ui::referenceStatusLine (failed).text.contains ("AUDIO READ FAILED / CHECK THE FILE IN KIRIN OS"),
                 "the pending case exercises the full read failure message");
        cases.push_back ({ "V pending audio read failure", failed });
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
            const auto first = shown.replace (juce::String::fromUTF8 (u8" ／"), " /")
                .upToFirstOccurrenceOf (" /", false, false).trimEnd();
            require (first.isNotEmpty() && layout.text.startsWith (first)
                         && text_style::shownWidth (readout, first) <= static_cast<float> (layout.textArea.getWidth()),
                     "the footer status shows at least its first part whole at 300% (" + juce::String (item.name) + ", "
                         + tag + "): " + shown);
            require (text_style::shownWidth (readout, layout.text) <= static_cast<float> (layout.textArea.getWidth()),
                     "complete English and Japanese status segments fit the footer before painting (" + tag + "): " + layout.text);
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
