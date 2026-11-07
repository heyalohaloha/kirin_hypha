#include "HyphaReferenceNavigationModel.h"
#include "HyphaReferenceVersionPage.h"

// What the keys and the buttons of REF ask for (PluginEditorShortcutDispatch.cpp). A move goes through
// the callback a mouse choice uses, so a song still preparing is chosen and reported the same way. It
// stops at either end of the list, in the list's own order, and never skips a row. Each request
// answers with why it did nothing (English), or with nothing.
namespace hypha::reference_ui
{
namespace
{
constexpr const char* notHere = "Not available on this page";

struct Step
{
    juce::String id, refusal;
};

Step stepIn (const juce::StringArray& ids, const juce::String& selected, int direction)
{
    if (ids.isEmpty()) return { {}, "Nothing to choose" };
    const auto at = ids.indexOf (selected);
    if (at < 0) return { direction < 0 ? ids[ids.size() - 1] : ids[0], {} };  // the choice left the list
    const auto next = at + direction;
    if (next < 0) return { {}, "First in the list" };
    if (next >= ids.size()) return { {}, "Last in the list" };
    return { ids[next], {} };
}

juce::StringArray idsOf (const std::vector<SelectionOption>& options)
{
    juce::StringArray ids;
    for (const auto& option : options) ids.add (option.id);
    return ids;
}

// The rows as Kirin OS sent them. The shown order moves the chosen song to the top (arrangeVersionRows),
// and walking that order would only go back and forth between two songs.
std::vector<SelectionOption> sourceOrder (std::vector<SelectionOption> rows)
{
    std::stable_sort (rows.begin(), rows.end(), [] (const auto& left, const auto& right)
                      { return left.sourceIndex < right.sourceIndex; });
    return rows;
}

juce::String workOf (const std::vector<SelectionOption>& rows, const juce::String& id)
{
    for (const auto& row : rows)
        if (row.id == id) return row.workId;
    return {};
}

juce::String choose (const Step& step, const std::function<void (const juce::String&)>& callback)
{
    if (step.id.isNotEmpty() && callback) callback (step.id);
    return step.refusal;
}
}

// slot：0 = A、1 = V、2 = C、3 = B。The button of a role that is not there cannot be pressed, nor can its key.
juce::String Component::requestRole (int slot)
{
    const auto& button = slot == 1 ? bButton : slot == 2 ? cButton : slot == 3 ? refButton : aButton;
    if (! button.isVisible() || ! button.isEnabled()) return notHere;
    if (slot == 0) { if (onSelectA) onSelectA(); }
    else if (slot == 3) { if (! explainReference() && onSelectRef) onSelectRef(); }
    else if (! openLarge (slot) && ! explainUnavailable (slot == 1))
    {
        if (slot == 1 && onSelectB) onSelectB();
        if (slot == 2 && onSelectC) onSelectC();
    }
    return {};
}

// 鳴っている C は今の A の窓で合わせ直す。鳴っていなければ C を鳴らす（選ぶときに合わせる）。
juce::String Component::requestCheckMatch()
{
    if (! current.separateComparisons || current.comparisonSlot != 2) return "Available in C";
    if (current.bSelected && current.audibleComparisonSlot == 2) { if (onMatch) onMatch(); }
    else if (! openLarge (2) && ! explainUnavailable (false) && onSelectC) onSelectC();
    return {};
}

juce::String Component::requestVersionBlind()
{
    if (! blindButton.isVisible() || ! blindButton.isEnabled()) return "Choose a Version in V";
    if (onStartBlind) onStartBlind();
    return {};
}

juce::String Component::stepSong (int direction)
{
    if (isBlindSession (current.blindPhase) || ! current.separateComparisons) return notHere;
    if (current.comparisonSlot == 3)
    {
        if (presentationContext.density == observatory::Density::compact) return "Available from 125%";
        return choose (stepIn (idsOf (current.songs), current.songId, direction), onSelectSong);
    }
    if (current.comparisonSlot == 2)
    {
        const auto groups = checkGroups (current.checks);
        const auto* group = currentGroup (groups, current.checkId);
        return choose (stepIn (group != nullptr ? idsOf (group->songs) : juce::StringArray(), current.checkId, direction), onSelectCheck);
    }
    // V：曲ごとに、Kirin OS の順。次の曲はその曲の最初の Version から。
    const auto rows = sourceOrder (current.versions);
    juce::StringArray works;
    for (const auto& row : rows) works.addIfNotAlreadyThere (row.workId);
    const auto step = stepIn (works, workOf (rows, current.versionId), direction);
    for (const auto& row : rows)
        if (step.id.isNotEmpty() && row.workId == step.id) return choose ({ row.id, {} }, onSelectVersion);
    return step.refusal;
}

juce::String Component::stepVersion (int direction)
{
    if (isBlindSession (current.blindPhase) || ! current.separateComparisons) return notHere;
    if (current.comparisonSlot != 1) return "Available in V";
    const auto rows = sourceOrder (current.versions);
    const auto work = workOf (rows, current.versionId);
    juce::StringArray ids;
    for (const auto& row : rows)
        if (row.workId == work) ids.add (row.id);
    return choose (stepIn (ids, current.versionId, direction), onSelectVersion);
}

juce::String Component::stepTab (int direction)
{
    if (isBlindSession (current.blindPhase) || ! current.separateComparisons) return notHere;
    juce::StringArray ids;
    juce::String selected;
    if (current.comparisonSlot == 1)
    {
        if (! versionPage()) return "Available at 300%";
        for (const auto& tab : versionTabs()) ids.add (tab.id);
        selected = versionTab;
    }
    else if (current.comparisonSlot == 2)
    {
        for (const auto& group : checkGroups (current.checks)) ids.add (group.checkId);
        selected = current.checkId.upToFirstOccurrenceOf ("/", false, false);
    }
    else return "Available in C and V";
    const auto step = stepIn (ids, selected, direction);
    if (step.id.isNotEmpty()) chooseTab (step.id);
    return step.refusal;
}

juce::String Component::stepCue (int direction)
{
    if (isBlindSession (current.blindPhase) || ! current.separateComparisons) return notHere;
    if (current.comparisonSlot != 2) return "Available in C";
    if (current.candidatePreparationPending) return "Not available on this page";
    return choose (stepIn (idsOf (current.cues), current.cueId, direction), onSelectCue);
}

juce::String Component::stepSet (int direction)
{
    if (isBlindSession (current.blindPhase) || ! current.separateComparisons) return notHere;
    if (current.comparisonSlot != 3) return choose (stepIn (idsOf (current.presets), current.presetId, direction), onSelectPreset);
    if (presentationContext.density == observatory::Density::compact) return "Available from 125%";
    return choose (stepIn (idsOf (current.songSets), current.songSetId, direction), onSelectSongSet);
}
}
