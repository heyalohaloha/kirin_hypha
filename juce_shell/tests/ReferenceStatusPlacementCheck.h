#pragma once

// 2026-10-06：REF の状態の行の置き場所と、REF の説明の行を、出荷のエディターで確かめる（今までの試験は置き場所の規則を
// 試験の中で計算し直して比べていたので、規則が間違っていても通った。知らせ・ボタンの無い状態でしか流していなかった）。
//  - 足元の段がある大きさ：何も無ければ足元の左。知らせが出ていれば行は隠すだけ（REF の図の高さは変えない）。知らせと
//    承認・アクションのボタンが重なるときは、どちらも隠さないよう REF の一番下へ戻す。
//  - 300% の B・C・V の項目を指すと説明を足元の行に出し、「Show hover help」を切れば出さない。
#include "EditorProductChecks.h"
#include "../src/HyphaHoverHelpPreference.h"
#include "../src/HyphaReferenceHelpText.h"

namespace hypha::tests::editor_product
{
template <class Tag, typename Tag::Type Member> struct PlacementAccess
{
    friend typename Tag::Type placementMember (Tag) { return Member; }
};

struct PlaceReferenceStatus
{
    using Type = void (KirinHyphaEditor::*) ();
    friend Type placementMember (PlaceReferenceStatus);
};
template struct PlacementAccess<PlaceReferenceStatus, &KirinHyphaEditor::placeReferenceStatus>;

// 部品とエディターまでの親がどれも出ている（試験のエディターは画面に置かないので isShowing は使えない）。
inline bool visibleIn (juce::Component& editor, juce::Component& item)
{
    for (auto* component = &item; component != nullptr; component = component->getParentComponent())
    {
        if (! component->isVisible()) return false;
        if (component == &editor) return true;
    }
    return false;
}

inline reference_ui::State checkPageState()
{
    reference_ui::State state;
    state.separateComparisons = state.libraryReceived = state.osOnline = state.aAvailable = state.auditionBuffered = true;
    state.osAccess = os_access::State::ready;
    state.readiness = reference_ui::Readiness::ready;
    state.comparisonSlot = 2;
    state.checkStep = reference_ui::SourceStep::ready;
    state.checks = { { "chk-low/cand-1", "Low End  /  Song 1" }, { "chk-vocal/cand-1", "Vocal  /  Song 1" } };
    state.checkId = "chk-low/cand-1";
    state.comparisonMode = "loudness_match";
    return state;
}

// `footerExists`：この大きさに足元の段がある（150% 以上）。
inline void verifyReferenceStatusPlacement (KirinHyphaEditor& editor, observatory::View& view, reference_ui::Component& panel,
                                            bool footerExists)
{
    auto& strip = panel.footerStatusStrip();
    const auto place = [&] { (editor.*placementMember (PlaceReferenceStatus {})) (); };
    const auto saved = panel.state();
    const auto savedFeedback = view.feedback();
    const auto footer = view.statusStripBounds();
    auto plain = checkPageState();
    view.setFeedback ({});
    panel.setState (plain);
    place();
    require (strip.inFooter() == footerExists && panel.statusInFooter() == footerExists
                 && (strip.getParentComponent() == &panel) == ! footerExists,
             "with nothing else in the footer, the REF status sits there where the footer exists");
    if (footerExists)
        require (strip.isVisible() && editor.getLocalArea (strip.getParentComponent(), strip.getBounds())
                                          == editor.getLocalArea (&view, footer),
                 "the REF status takes the footer's session place");
    const auto panelHeight = panel.getHeight();
    view.setFeedback ("Saved");
    place();
    require (! footerExists || (strip.inFooter() && ! strip.isVisible() && panel.getHeight() == panelHeight),
             "a notice hides the footer status without changing REF's charts");
    auto offered = plain;
    offered.actionText = "RETRY PREPARATION";
    offered.action = { reference_ui::ActionKind::retryPresetPreparation, {} };
    panel.setState (offered);
    place();
    auto* button = find (panel, "reference-action");  // 状態の行の中にある
    require (strip.getParentComponent() == &panel && ! strip.inFooter() && strip.isVisible() && ! panel.statusInFooter()
                 && button != nullptr && visibleIn (editor, *button)
                 && panel.getLocalBounds().contains (panel.getLocalArea (button->getParentComponent(), button->getBounds())),
             ("a notice and a REF button together move the status row back to REF's bottom, so neither is hidden: parent "
              + juce::String (strip.getParentComponent() == &panel ? "REF" : "editor") + (strip.inFooter() ? " footer" : "")
              + (strip.isVisible() ? " visible" : " hidden") + " strip " + strip.getBounds().toString() + " button "
              + (button != nullptr ? button->getBounds().toString() + (button->isVisible() ? " visible" : " hidden") : juce::String ("none"))
              + " panel " + panel.getLocalBounds().toString()).toRawUTF8());
    view.setFeedback (savedFeedback);
    panel.setState (saved);
    place();
}

// REF の画面を出したうえで確かめる（試験の processor は Kirin OS の持ち主ではないので、REF の代わりに入口の案内が
// 出ている。その前に REF を出し、終わったら戻す）。
template <typename Check>
inline void withReferenceShown (KirinHyphaEditor& editor, reference_ui::Component& panel, Check&& check)
{
    auto* access = component<reference_ui::AccessPanel> (editor);
    const bool accessShown = access != nullptr && access->isVisible(), panelShown = panel.isVisible();
    if (access != nullptr) access->setVisible (false);
    panel.setVisible (true);
    check();
    panel.setVisible (panelShown);
    if (access != nullptr) access->setVisible (accessShown);
}

inline void verifyReferenceStatusPlacementShown (KirinHyphaEditor& editor, observatory::View& view, reference_ui::Component& panel,
                                                 bool footerExists)
{
    withReferenceShown (editor, panel, [&] { verifyReferenceStatusPlacement (editor, view, panel, footerExists); });
}

// 300% の C のページの項目を指すと説明の行に出し、「Show hover help」を切れば出さない（利用者のファイルは書かない）。
inline void verifyReferenceHelpLine (KirinHyphaEditor& editor, reference_ui::Component& panel)
{
    const auto saved = panel.state();
    panel.setState (checkPageState());
    auto* tabs = panel.findChildWithID ("reference-check-tabs");
    require (tabs != nullptr && visibleIn (editor, *tabs), "the 300% C page shows its tabs in the editor");
    const auto point = editor.getLocalArea (tabs->getParentComponent(), tabs->getBounds()).getCentre();
    const auto shown = editor.helpLineAt (point);
    require (shown.wholeRow && shown.text == reference_ui::help_text::checkTabs,
             ("pointing at the C page's tabs puts their help across the footer row: " + shown.text).toRawUTF8());
    HoverHelpPreference::shared().overrideForTest (false);
    const bool silent = editor.helpLineAt (point).text.isEmpty();
    HoverHelpPreference::shared().overrideForTest (std::nullopt);
    require (silent, "with Show hover help off, the REF help line says nothing");
    panel.setState (saved);
}
}
