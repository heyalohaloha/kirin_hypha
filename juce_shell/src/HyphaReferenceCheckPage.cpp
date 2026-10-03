#include "HyphaReferenceComponent.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

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

bool Component::checkPage() const noexcept
{
    return current.separateComparisons && current.comparisonSlot == 2
        && presentationContext.density == observatory::Density::inspection && ! isBlindSession (current.blindPhase);
}

void Component::configureCheckPage()
{
    checkTabs.onChoose = [this] (const juce::String& checkId)
    {
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

void Component::syncCheckPage (bool blindSession, bool workflowActive)
{
    const bool page = checkPage() && ! workflowActive;
    const auto groups = checkGroups (current.checks);
    const auto* group = currentGroup (groups, current.checkId);
    std::vector<CheckTabs::Tab> tabs;
    for (const auto& item : groups) tabs.push_back ({ item.checkId, item.label });
    checkTabs.setTabs (std::move (tabs), group != nullptr ? group->checkId : juce::String {}, presentationContext);
    checkTabs.setVisible (page && ! groups.empty());
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
}

void Component::layoutCheckPage (juce::Rectangle<int>& area)
{
    area.removeFromTop (panelGap());
    auto top = area.removeFromTop (40);
    auto left = top.removeFromLeft ((top.getWidth() - 5) / 2);
    top.removeFromLeft (5);
    presetBox.setBounds (left.removeFromBottom (25));
    checkSongBox.setBounds (top.removeFromBottom (25));
    area.removeFromTop (4);
    checkTabs.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    auto row = area.removeFromTop (38).withTrimmedTop (16);
    viewButton.setBounds (row.removeFromRight (96));
    row.removeFromRight (6);
    if (matchButton.isVisible())
    {
        matchButton.setBounds (row.removeFromRight (96));
        row.removeFromRight (8);
    }
    cueBox.setBounds (row.removeFromLeft (juce::jmin (row.getWidth() / 2, 260)));
}

void Component::paintCheckPageLabels (juce::Graphics& g) const
{
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
    const auto above = [&g] (const juce::Component& item, const juce::String& text)
    {
        if (item.isVisible())
            text_style::drawEllipsized (g, text, item.getBounds().withY (item.getY() - 17).withHeight (15),
                                        juce::Justification::centredLeft);
    };
    above (presetBox, "CHECK SET");
    above (checkSongBox, "C / SONG");
    above (cueBox, "CUE");
    // MATCH の左に、今の合わせ方（鳴っていればその gain と固定、鳴っていなければ鳴らすときの gain）。
    if (matchButton.isVisible())
    {
        const auto readout = matchReadout (current);
        auto space = matchButton.getBounds().withX (cueBox.getRight() + 8);
        space.setRight (matchButton.getX() - 8);
        g.setColour (current.bSelected && current.audibleComparisonSlot == 2 ? COL_SPECTRUM_DELTA_BR : COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, readout, space, juce::Justification::centredRight);
    }
}

int Component::checkFooterHeight() const noexcept
{
    return (std::isfinite (current.sourceDurationSeconds) ? 26 + 6 : 0)
         + (current.cueKirin && current.cueKirin->medianDb.size() == current.cueKirin->centersHz.size() ? 40 + 6 : 0);
}

juce::Rectangle<int> Component::paintCheckFooter (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (! checkPage()) return area;
    if (std::isfinite (current.sourceDurationSeconds))
    {
        paintCueBar (g, area.removeFromBottom (26), current, presentationContext);
        area.removeFromBottom (6);
    }
    if (current.cueKirin && current.cueKirin->medianDb.size() == current.cueKirin->centersHz.size())
    {
        paintBandSummary (g, area.removeFromBottom (40), current, presentationContext);
        area.removeFromBottom (6);
    }
    return area;
}
}
