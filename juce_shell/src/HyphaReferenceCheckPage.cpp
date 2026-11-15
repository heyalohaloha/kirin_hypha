#include "HyphaReferenceNavigationModel.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceVersionPage.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaReferenceHelpText.h"

#include <algorithm>

// C（CHECK）は Check を一つ選び、主の比較と再生方法を出す。詳しい比較と時間軸は表示だけを開閉する。
// V の選択は V の画面にだけ出す。小さな表示で C を押すと 300% の画面が開く。
namespace hypha::reference_ui
{
bool Component::versionPage() const noexcept
{
    return rolePage() && current.comparisonSlot == 1;
}

bool Component::checkPage() const noexcept
{
    return rolePage() && current.comparisonSlot == 2;
}

void Component::chooseTab (const juce::String& checkId)
{
    // 役を押した直後で、描いている画面が古い（今の画面と違う）ときのタブは捨てる（その間に画面は描き直される）。
    if (pageCurrent && ! pageCurrent (current.comparisonSlot)) return;
    // V の画面のタブは見るものだけを替える（音も C の選択も変えない）。
    if (versionPage()) { versionTab = checkId; setState (current); return; }
    // 同じ曲のまま Check を替える（その Check に同じ曲が無ければ最初の曲）。
    const auto song = current.checkId.fromFirstOccurrenceOf ("/", false, false);
    juce::String source;
    for (const auto& option : current.checks)
        if (option.id == current.checkId) { source = option.sourceIdentityKey; break; }
    for (const auto& group : checkGroups (current.checks))
        if (group.checkId == checkId && ! group.songs.empty())
        {
            auto chosen = group.songs.front().id;
            bool found = false;
            for (const auto& option : group.songs)
                if ((source.isNotEmpty() && source == option.sourceIdentityKey)
                    || (source.isEmpty() && option.sourceIdentityKey.isEmpty() && song.isNotEmpty()
                        && option.id.fromFirstOccurrenceOf ("/", false, false) == song))
                { chosen = option.id; found = true; break; }
            checkChangeNotice = ! found && song.isNotEmpty() ? "SONG CHANGED: NOT IN THIS CHECK" : juce::String();
            checkChangeTarget = checkChangeNotice.isNotEmpty() ? chosen : juce::String();
            if (onSelectCheck) onSelectCheck (chosen);
            repaint();
        }
}

void Component::configureCheckPage()
{
    checkTabs.onChoose = [this] (const juce::String& checkId) { chooseTab (checkId); };
    addChildComponent (checkTabs);
    checkChooser.setLookAndFeel (&selectorLookAndFeel);
    checkChooser.setComponentID ("reference-check-choice");
    checkChooser.setTitle ("Check");
    checkChooser.setTooltip (help_text::checkTabs);
    checkChooser.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    checkChooser.setColour (juce::ComboBox::arrowColourId, COL_FLORA);
    checkChooser.onChange = [this]
    {
        const auto groups = checkGroups (current.checks);
        const auto index = checkChooser.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int> (groups.size())) chooseTab (groups[static_cast<size_t> (index)].checkId);
    };
    addChildComponent (checkChooser);
    detailsButton.setComponentID ("reference-check-details");
    detailsButton.setTitle ("Details");
    detailsButton.setTooltip ("Show or hide details without changing the sound or comparison.");
    detailsButton.onClick = [this] { detailsOpen = ! detailsOpen; setState (State (current)); };
    addChildComponent (detailsButton);
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
        checkChangeNotice.clear(); checkChangeTarget.clear();
        if (id.isNotEmpty() && onSelectCheck) onSelectCheck (id);
    };
    addChildComponent (checkSongBox);
    matchButton.setComponentID ("reference-match");
    matchButton.setTitle ("Match C to A again");
    matchButton.onClick = [this] { requestCheckMatch(); };
    addChildComponent (matchButton);
    // CUE／SONG は比べる値だけを替える（音・MATCH は変えない）。役を押した直後の古い画面の押しは捨てる。
    songSwitch.onChoose = [this] (bool song)
    { if ((! pageCurrent || pageCurrent (current.comparisonSlot)) && onSetCheckSong) onSetCheckSong (song); };
    addChildComponent (songSwitch);
}

void Component::syncCheckPage (bool blindSession)
{
    const bool page = checkPage();
    const bool compact = presentationContext.referenceCheck && ! detailedLayout() && ! blindSession;
    const bool vPage = versionPage();
    const auto groups = checkGroups (current.checks);
    const auto* group = currentGroup (groups, current.checkId);
    // V のタブは C の CHECK SET と切り離した決まった項目（versionTabs、2026-10-04）。C のタブは CHECK セットの順。
    std::vector<CheckTabs::Tab> tabs;
    if (vPage)
        for (const auto& tab : versionTabs()) tabs.push_back ({ tab.id, tab.label });
    else
        for (const auto& item : groups) tabs.push_back ({ item.checkId, item.label });
    if (vPage && findVersionTab (versionTab) == nullptr) versionTab = "whole";
    const auto* versionChoice = vPage ? findVersionTab (versionTab) : nullptr;
    const auto sameSection = versionChoice != nullptr && versionChoice->id != "whole" ? versionChoice->label : juce::String {};
    checkTabs.setTabs (std::move (tabs), vPage ? versionTab : group != nullptr ? group->checkId : juce::String {}, presentationContext);
    checkTabs.setVisible (vPage);
    std::vector<SelectionOption> choices;
    for (const auto& item : groups) choices.push_back ({ item.checkId, item.label });
    syncSelectionControl (checkChooser, choices, group != nullptr ? group->checkId : juce::String {});
    unavailable::set (checkChooser, ! choices.empty(), "Nothing to choose yet");
    checkChooser.setVisible (page && ! groups.empty());
    detailsButton.setVisible (page);
    detailsButton.setToggleState (detailsOpen, juce::dontSendNotification);
    // V を鳴らしていなくても、WHOLE と同じ位置合わせの gain（同じ区間の音量差）で合わせて比べる（鳴らしていれば実際の
    // gain）。2026-10-04 まで V を鳴らさないと「音量未調整」で 4 帯域が「—」だった。
    const auto* timeline = current.visualTimeline.get();
    const auto sameSectionGain = current.bSelected && current.audibleComparisonSlot == 1 ? current.appliedGainDb
        : timeline != nullptr && timeline->binding.aligned && timeline->binding.matched ? timeline->binding.gainDb
        : std::numeric_limits<double>::quiet_NaN();
    comparisonView.setSameSection (sameSection, sameSectionGain,
                                   versionChoice != nullptr ? versionChoice->views : std::vector<juce::String> {}, false);
    syncSelectionControl (checkSongBox, group != nullptr ? group->songs : std::vector<SelectionOption> {}, current.checkId);
    checkSongBox.setVisible (page && group != nullptr);
    const bool matching = current.comparisonMode == "loudness_match";
    matchButton.setVisible ((page || compact) && ! blindSession);
    unavailable::set (matchButton, matching, "This Check does not use loudness MATCH.");
    matchButton.setReady (matching && (current.bSelected ? current.audibleComparisonSlot == 2 : canHearCheck (current)));
    matchButton.setTooltip (! matching ? "This Check does not use loudness MATCH." : current.bSelected && current.audibleComparisonSlot == 2
        ? "Match C to the latest A over the Cue's length again, and keep it fixed."
        : "Play C matched to the latest A over the Cue's length.");
    const bool songAvailable = songSupported (current);  // 範囲の帯の Check は CUE のまま（押すと理由）
    songSwitch.setChoice (current.song.chosen, songAvailable,
                          compact ? juce::String ("CUE") : SongSwitch::cueText (current.cueStartSeconds, current.cueEndSeconds, current.followA), presentationContext);
    songSwitch.setVisible ((page || compact) && ! blindSession && group != nullptr && ! listeningCheck());
    if (page)
    {
        // C の画面では V の選択と、Check と曲を 1 つにした選択欄を出さない（V の画面とタブ・曲で選ぶ）。
        versionBox.setVisible (false);
        checkBox.setVisible (false);
    }
    else if (current.separateComparisons && current.comparisonSlot == 2 && ! detailedLayout())
        versionBox.setVisible (false);
    if (vPage)
    {
        // V の画面では C の CHECK SET・曲・Cue を出さない（C の画面で選ぶ。V の項目は決まっている）。
        presetBox.setVisible (false);
        checkBox.setVisible (false);
        cueBox.setVisible (false);
    }
}

void Component::layoutCheckPage (juce::Rectangle<int>& area, juce::Rectangle<int> selectors)
{
    // 選択欄は A・B・C・V のボタンと同じ段（V：V / VERSION を全幅に。C：CHECK SET と C / SONG）。
    const auto first = selectors.removeFromLeft ((selectors.getWidth() - 8) / 2).removeFromBottom (25);
    selectors.removeFromLeft (8);
    const auto second = selectors.removeFromBottom (25);
    area.removeFromTop (panelGap());
    auto row = area.removeFromTop (checkTabsHeight());
    if (versionPage())
    {
        versionBox.setBounds (first.getUnion (second));  // 長い版の名前と AUTO の印が切れないよう全幅
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
    // Check を選ぶ欄と、表示だけを開閉する「詳しく」。MATCH は同じ段の右端に置く。
    if (matchButton.isVisible())
    {
        matchButton.setBounds (row.removeFromRight (80).withSizeKeepingCentre (80, 24));
        row.removeFromRight (6);
    }
    detailsButton.setBounds (row.removeFromRight (110).withSizeKeepingCentre (110, 25));
    row.removeFromRight (8);
    const auto choiceWidth = juce::jmin (300, row.getWidth());
    checkChooser.setBounds (row.removeFromLeft (choiceWidth).withSizeKeepingCentre (choiceWidth, 25));
    cueBox.setBounds (cueRowBounds().removeFromLeft (180).withSizeKeepingCentre (180, 22));  // CUE は時間軸の段の左
    auto cueRow = cueRowBounds();  // その右に流し方とずらし、右の端に CUE／SONG、残りが時間軸
    if (selectionVisible (cueBox) || cueBox.isVisible()) cueRow.removeFromLeft (188);
    if (songSwitch.isVisible())
    {
        const auto width = SongSwitch::widthFor (SongSwitch::cueText (current.cueStartSeconds, current.cueEndSeconds, current.followA), presentationContext);
        songSwitch.setBounds (cueRow.removeFromRight (width).withSizeKeepingCentre (width, SongSwitch::height));
        cueRow.removeFromRight (8);
    }
    layoutPlayback (cueRow);
}

// C の選択欄と V のタブはそれぞれ一段。
int Component::checkTabsHeight() const
{
    if (! checkPage()) return versionPageRows;
    return checkPageRows;
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

// 2026-10-04（中身の伴わない表示を置かない）：耳で聴き比べる Check は Kirin OS も測っていない
// （「この項目は耳で聴き比べます」）。測っていない結果の箱（鳴らすと C−A がいつも 0.0）を出さず、案内だけ。
void Component::paintListeningPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    surface_material::paintPanel (g, area.toFloat(), 0.72f);
    auto inner = area.reduced (12, 8);
    g.setColour (COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, current.checkLabel, inner.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::status, typography::Composition::visualization));
    text_style::drawLines (g, listeningGuide ('C'), inner.reduced (24, 0), juce::Justification::centred, 3);
}

void Component::paintCheckPageLabels (juce::Graphics& g) const
{
    g.setColour (checkPage() ? COL_NORMAL : COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
    // 選択肢が 1 つの欄は読むだけの表示に替わる（selectionVisible）。そのときも見出しは出す。
    const auto above = [this, &g] (const juce::ComboBox& item, const juce::String& text)
    {
        if (item.isVisible() || selectionVisible (item))
            text_style::drawEllipsized (g, text, item.getBounds().withY (item.getY() - 17).withHeight (15),
                                        juce::Justification::centredLeft);
    };
    above (presetBox, "CHECK SET");
    if (presetBox.isVisible() || selectionVisible (presetBox))  // 見出しを指すと C が何のためかを言う
        help::note (presetBox.getBounds().withY (presetBox.getY() - 17).withHeight (15), help_text::checkPurpose);
    if (versionPage()) above (versionBox, "V / VERSION");
    above (checkSongBox, "C / SONG");
}

int Component::checkFooterHeight() const noexcept
{
    const bool cueRow = ! current.checks.empty();
    const bool bands = ! current.viewBindings.empty() && current.viewBindings.front().startsWith ("spectrum_");
    return (cueRow ? checkFooterRow : 0) + (bands ? checkFooterRow + 4 : 0);
}

juce::Rectangle<int> Component::paintCheckFooter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (! checkPage()) return area;
    if (! current.checks.empty())
    {
        auto row = area.removeFromBottom (checkFooterRow);
        if (selectionVisible (cueBox) || cueBox.isVisible()) row.removeFromLeft (cueBox.getWidth() + 8);  // CUE の選択
        for (const auto& button : playbackButtons)  // 流し方とずらし
            if (button.isVisible()) row.setLeft (juce::jmax (row.getX(), button.getRight() + 8));
        if (songSwitch.isVisible()) row.setRight (juce::jmin (row.getRight(), songSwitch.getX() - 8));  // CUE／SONG
        if (detailsOpen && std::isfinite (current.sourceDurationSeconds)) paintCueBar (g, row, current, presentationContext);
    }
    if (checkFooterHeight() > checkFooterRow)
    {
        area.removeFromBottom (4);
        paintBandSummary (g, area.removeFromBottom (checkFooterRow), current, presentationContext);
    }
    return area;
}
}
