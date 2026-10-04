#include "HyphaReferenceComponent.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceVersionPage.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaReferenceHelpText.h"

#include <algorithm>

// H12: C（CHECK）の画面（方向設計 §4、300%。300% 未満の C は押すと 300% に広がる、H10）。上から CHECK SET（順位つき）と曲、Check のタブ
// （CHECK セットの順）、Cue と MATCH（右上に 1 つ）、見比べの窓（Cue 対 A の同じ長さの直近）、4 帯域の
// 要約、Cue の時間軸。V の選択は V の画面にだけ出す。
namespace hypha::reference_ui
{
namespace
{
constexpr const char* noSource = " / NO SOURCE IN KIRIN OS";

// state.checks は「checkId/candidateId」と「Check の名前  /  曲」の組（Kirin OS の Preset の順）。
struct CheckGroup
{
    juce::String checkId, label;
    std::vector<SelectionOption> songs;
};

std::vector<CheckGroup> checkGroups (const std::vector<SelectionOption>& targets)
{
    std::vector<CheckGroup> groups;
    for (const auto& target : targets)
    {
        const auto checkId = target.id.upToFirstOccurrenceOf ("/", false, false);
        const auto separator = target.label.indexOf ("  /  ");
        const bool missing = target.label.endsWith (noSource);
        const auto label = missing ? target.label.dropLastCharacters (juce::String (noSource).length())
                         : separator >= 0 ? target.label.substring (0, separator) : target.label;
        const auto song = missing ? juce::String ("NO SOURCE IN KIRIN OS")
                        : separator >= 0 ? target.label.substring (separator + 5) : target.label;
        auto group = std::find_if (groups.begin(), groups.end(), [&checkId] (const auto& item) { return item.checkId == checkId; });
        if (group == groups.end())
        {
            groups.push_back ({ checkId, label, {} });
            group = std::prev (groups.end());
        }
        group->songs.push_back ({ target.id, song });
    }
    return groups;
}

const CheckGroup* currentGroup (const std::vector<CheckGroup>& groups, const juce::String& targetId)
{
    const auto checkId = targetId.upToFirstOccurrenceOf ("/", false, false);
    for (const auto& group : groups)
        if (group.checkId == checkId) return &group;
    return nullptr;
}
}

bool Component::versionPage() const noexcept
{
    return current.separateComparisons && current.comparisonSlot == 1
        && presentationContext.density == observatory::Density::inspection && ! isBlindSession (current.blindPhase);
}

bool Component::checkPage() const noexcept
{
    return current.separateComparisons && current.comparisonSlot == 2
        && presentationContext.density == observatory::Density::inspection && ! isBlindSession (current.blindPhase);
}

void Component::configureCheckPage()
{
    checkTabs.onChoose = [this] (const juce::String& checkId)
    {
        // V の画面のタブは見るものだけを替える（音も C の選択も変えない）。
        if (versionPage()) { versionTab = checkId; setState (current); return; }
        // 同じ曲のまま Check を替える（その Check に同じ曲が無ければ最初の曲）。
        const auto song = current.checkId.fromFirstOccurrenceOf ("/", false, false);
        for (const auto& group : checkGroups (current.checks))
            if (group.checkId == checkId && ! group.songs.empty())
            {
                auto chosen = group.songs.front().id;
                for (const auto& option : group.songs)
                    if (option.id.fromFirstOccurrenceOf ("/", false, false) == song) chosen = option.id;
                if (onSelectCheck) onSelectCheck (chosen);
            }
    };
    addChildComponent (checkTabs);
    checkSongBox.setLookAndFeel (&selectorLookAndFeel);
    checkSongBox.setComponentID ("reference-check-song");
    checkSongBox.setTitle ("C Song");
    checkSongBox.setTooltip ("Choose the song C plays for this Check. A stays the current DAW input.");
    checkSongBox.setDescription (checkSongBox.getTooltip());
    checkSongBox.setColour (juce::ComboBox::backgroundColourId, kFieldFill.withAlpha (0.94f));
    checkSongBox.setColour (juce::ComboBox::outlineColourId, COL_MUTED.withAlpha (0.46f));
    checkSongBox.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    checkSongBox.setColour (juce::ComboBox::arrowColourId, COL_FLORA.withAlpha (0.84f));
    checkSongBox.onChange = [this]
    {
        const auto groups = checkGroups (current.checks);  // group は groups の中を指すので先に持つ
        const auto* group = currentGroup (groups, current.checkId);
        if (group == nullptr) return;
        const auto id = selectedOptionId (checkSongBox, group->songs);
        if (id.isNotEmpty() && id != current.checkId && onSelectCheck) onSelectCheck (id);
    };
    addChildComponent (checkSongBox);
    matchButton.setComponentID ("reference-match");
    matchButton.setTitle ("Match C to A again");
    matchButton.onClick = [this]
    {
        // 鳴っている C は今の A の窓で合わせ直す。鳴っていなければ C を鳴らす（選ぶときに合わせる）。
        if (current.bSelected && current.audibleComparisonSlot == 2) { if (onMatch) onMatch(); }
        else if (! openLarge (2) && ! explainUnavailable (false) && onSelectC) onSelectC();
    };
    addChildComponent (matchButton);
}

void Component::syncCheckPage (bool blindSession)
{
    const bool page = checkPage();
    const bool vPage = versionPage();  // H13
    const auto groups = checkGroups (current.checks);
    const auto* group = currentGroup (groups, current.checkId);
    std::vector<CheckTabs::Tab> tabs;
    if (vPage) tabs.push_back ({ "whole", "WHOLE" });
    for (const auto& item : groups) tabs.push_back ({ item.checkId, item.label });
    juce::String sameSection;
    for (const auto& tab : tabs) if (vPage && tab.id == versionTab && tab.id != "whole") sameSection = tab.label;
    if (vPage && sameSection.isEmpty()) versionTab = "whole";
    checkTabs.setTabs (std::move (tabs), vPage ? versionTab : group != nullptr ? group->checkId : juce::String {}, presentationContext);
    checkTabs.setVisible ((page || vPage) && ! groups.empty());
    const auto views = current.checkViewBindings.find (versionTab);
    // V を鳴らしていなくても、WHOLE と同じ位置合わせの gain（同じ区間の音量差）で合わせて比べる（鳴らしていれば実際の
    // gain）。2026-10-04 まで V を鳴らさないと「音量未調整」で 4 帯域が「—」だった。
    const auto* timeline = current.visualTimeline.get();
    const auto sameSectionGain = current.bSelected && current.audibleComparisonSlot == 1 ? current.appliedGainDb
        : timeline != nullptr && timeline->binding.aligned && timeline->binding.matched ? timeline->binding.gainDb
        : std::numeric_limits<double>::quiet_NaN();
    comparisonView.setSameSection (sameSection, sameSectionGain,
                                   views != current.checkViewBindings.end() ? views->second : std::vector<juce::String> {},
                                   current.listeningChecks.count (versionTab) > 0);
    syncSelectionControl (checkSongBox, group != nullptr ? group->songs : std::vector<SelectionOption> {}, current.checkId);
    checkSongBox.setVisible (page && group != nullptr);
    const bool matching = current.comparisonMode == "loudness_match";
    matchButton.setVisible (page && matching && ! blindSession);
    matchButton.setReady (current.bSelected ? current.audibleComparisonSlot == 2 : canHearCheck (current));
    matchButton.setTooltip (current.bSelected && current.audibleComparisonSlot == 2
        ? "Match C to the latest A over the Cue's length again, and keep it fixed."
        : "Play C matched to the latest A over the Cue's length.");
    if (page)
    {
        // C の画面では V の選択と、Check と曲を 1 つにした選択欄を出さない（V の画面とタブ・曲で選ぶ）。
        versionBox.setVisible (false);
        checkBox.setVisible (false);
    }
    if (vPage)
    {
        // V の画面では C の曲と Cue を出さない（C の画面で選ぶ）。CHECK SET は C と共用。
        checkBox.setVisible (false);
        cueBox.setVisible (false);
    }
}

void Component::layoutCheckPage (juce::Rectangle<int>& area, juce::Rectangle<int> selectors)
{
    // 選択欄は A・B・C・V のボタンと同じ段（V：V / VERSION と CHECK SET。C：CHECK SET と C / SONG）。
    const auto first = selectors.removeFromLeft ((selectors.getWidth() - 8) / 2).removeFromBottom (25);
    selectors.removeFromLeft (8);
    const auto second = selectors.removeFromBottom (25);
    area.removeFromTop (panelGap());
    auto row = area.removeFromTop (checkPageRows);
    if (versionPage())
    {
        versionBox.setBounds (first);
        presetBox.setBounds (second);
        if (blindButton.isVisible())  // VERSION BLIND は V の見比べの操作なので、V のタブの段の右に置く
        {
            if (blindButton.getParentComponent() != this) addChildComponent (blindButton);
            blindButton.setBounds (row.removeFromRight (120).withSizeKeepingCentre (120, 24));
            row.removeFromRight (8);
        }
        checkTabs.setBounds (row);
        return;
    }
    presetBox.setBounds (first);
    checkSongBox.setBounds (second);
    // タブの段の右に、今の合わせ方の読み（paintCheckPageLabels）と MATCH。
    if (matchButton.isVisible())
    {
        matchButton.setBounds (row.removeFromRight (80).withSizeKeepingCentre (80, 24));
        row.removeFromRight (156);
    }
    checkTabs.setBounds (row);
    cueBox.setBounds (cueRowBounds().removeFromLeft (180).withSizeKeepingCentre (180, 22));  // CUE は時間軸の段の左
}

// C の画面の一番下の段（CUE の選択と Cue の時間軸）。状態の行は 300% では足元の段にある。
juce::Rectangle<int> Component::cueRowBounds() const noexcept
{
    auto area = panelArea();
    if (! statusInFooter()) area.removeFromBottom (statusRowHeight());
    return area.removeFromBottom (checkFooterRow);
}

bool Component::listeningCheck() const
{
    return current.listeningChecks.count (current.checkId.upToFirstOccurrenceOf ("/", false, false)) > 0;
}

// 2026-10-04（Daisuke「箱だけ作って中身が伴っていない」）：耳で聴き比べる Check は Kirin OS も測っていない
// （「この項目は耳で聴き比べます」）。測っていない結果の箱（鳴らすと C−A がいつも 0.0）を出さず、案内だけ。
void Component::paintListeningPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    surface_material::paintPanel (g, area.toFloat(), 0.72f);
    auto inner = area.reduced (12, 8);
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (presentationContext, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, current.checkLabel, inner.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
    g.setFont (labelFont (presentationContext, typography::TextRole::status, typography::Composition::visualization));
    text_style::drawLines (g, listeningGuide ('C'), inner.reduced (24, 0), juce::Justification::centred, 3);
}

void Component::paintCheckPageLabels (juce::Graphics& g) const
{
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
    // 選択肢が 1 つの欄は読むだけの表示に替わる（selectionVisible）。そのときも見出しは出す。
    const auto above = [this, &g] (const juce::ComboBox& item, const juce::String& text)
    {
        if (item.isVisible() || selectionVisible (item))
            text_style::drawEllipsized (g, text, item.getBounds().withY (item.getY() - 17).withHeight (15),
                                        juce::Justification::centredLeft);
    };
    above (presetBox, "CHECK SET");
    if (versionPage()) above (versionBox, "V / VERSION");
    above (checkSongBox, "C / SONG");
    // MATCH の左に、今の合わせ方（鳴っていればその gain と固定、鳴っていなければ鳴らすときの gain）。
    if (matchButton.isVisible())
    {
        const auto readout = matchReadout (current);
        const auto space = matchButton.getBounds().withX (matchButton.getX() - 156).withWidth (150);
        help::note (space, help_text::match);
        g.setColour (current.bSelected && current.audibleComparisonSlot == 2 ? COL_SPECTRUM_DELTA_BR : COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, readout, space, juce::Justification::centredRight);
    }
}

int Component::checkFooterHeight() const noexcept
{
    const bool cueRow = std::isfinite (current.sourceDurationSeconds) || selectionVisible (cueBox);
    const bool bands = current.cueKirin && current.cueKirin->medianDb.size() == current.cueKirin->centersHz.size();
    return (cueRow ? checkFooterRow : 0) + (bands ? checkFooterRow + 4 : 0);
}

juce::Rectangle<int> Component::paintCheckFooter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (! checkPage()) return area;
    if (std::isfinite (current.sourceDurationSeconds) || selectionVisible (cueBox))
    {
        auto row = area.removeFromBottom (checkFooterRow);
        if (selectionVisible (cueBox) || cueBox.isVisible()) row.removeFromLeft (cueBox.getWidth() + 8);  // CUE の選択
        if (std::isfinite (current.sourceDurationSeconds)) paintCueBar (g, row, current, presentationContext);
    }
    if (current.cueKirin && current.cueKirin->medianDb.size() == current.cueKirin->centersHz.size())
    {
        area.removeFromBottom (4);
        paintBandSummary (g, area.removeFromBottom (checkFooterRow), current, presentationContext);
    }
    return area;
}
}
