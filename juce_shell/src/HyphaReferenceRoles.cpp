#include "HyphaReferenceComponent.h"
#include "HyphaReferenceOverlay.h"
#include "HyphaReferencePreviewGain.h"
#include "HyphaReferenceSongChoice.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaReferenceStatusModel.h"
#include "HyphaTheme.h"

#include <algorithm>

// A／B／C／V の B（REF）。Kirin OS が「Hypha に出す」にした B セットの曲を鳴らす役のボタンと、
// REFERENCE SET（順位つき、最大 3）と曲の選択。B を押すと B の画面になり、B のまま曲を選ぶとすぐ切り替わる。
namespace hypha::reference_ui
{
namespace
{
void configureBox (juce::ComboBox& box, const juce::String& id, const juce::String& title, const juce::String& tooltip)
{
    box.setComponentID (id);
    box.setTitle (title);
    box.setDescription (tooltip);
    box.setTooltip (tooltip);
    box.setWantsKeyboardFocus (true);
    box.setColour (juce::ComboBox::backgroundColourId, kFieldFill.withAlpha (0.94f));
    box.setColour (juce::ComboBox::outlineColourId, COL_MUTED.withAlpha (0.46f));
    box.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    box.setColour (juce::ComboBox::arrowColourId, COL_FLORA.withAlpha (0.84f));
}

// B の段階は Kirin OS からの届き方・B セットも含めて editor が決める（PluginEditorReferenceRoles.cpp）。言い方は段階の表。
juce::String referenceUnavailableText (const State& state)
{
    return "B: " + stepText (state.referenceStep);
}
}

void Component::configureRoles()
{
    refButton.setComponentID ("reference-ref");
    refButton.setTitle ("Audition B Reference");
    refButton.onClick = [this] { requestRole (3); };
    addAndMakeVisible (refButton);
    songSetBox.setLookAndFeel (&selectorLookAndFeel);
    songBox.setLookAndFeel (&selectorLookAndFeel);
    configureBox (songSetBox, "reference-song-set", "Reference Set", "Choose a Reference set Kirin OS ranked for Hypha.");
    configureBox (songBox, "reference-song", "Reference Song", "Choose the Reference song B plays. A stays the current DAW input.");
    songSetBox.onChange = [this]
    {
        const auto id = selectedOptionId (songSetBox, current.songSets);
        if (id.isNotEmpty() && id != current.songSetId && onSelectSongSet) onSelectSongSet (id);
    };
    songBox.onChange = [this]
    {
        const auto id = selectedOptionId (songBox, current.songs);
        if (id.isNotEmpty() && id != current.songId && onSelectSong) onSelectSong (id);
    };
    addChildComponent (songSetBox);
    addChildComponent (songBox);
    songList.onChoose = [this] (const juce::String& id)
    {
        // 目標の行は音が無いので鳴らせない：理由を言う（選択も音も変えない）。
        const bool song = std::any_of (current.songs.begin(), current.songs.end(), [&id] (const auto& option) { return option.id == id; });
        if (! song) { if (current.targets.collection.find (id) != nullptr && onExplain) onExplain (targetSilentText); return; }
        if (id != current.songId && onSelectSong) onSelectSong (id);
    };
    songList.onToggle = [this] (const juce::String& id)
    {
        std::vector<juce::String> listed;
        for (const auto& item : overlayItems (current)) listed.push_back (item.id);
        auto next = current.overlay;
        next.toggle (id, listed);
        applyOverlay (std::move (next));
    };
    songList.onPoint = [this] (const juce::String& id)
    {
        if (current.overlayPointed == id) return;
        current.overlayPointed = id;
        repaint (balanceBounds);
    };
    addChildComponent (songList);
    // ALL を初めて開いた時（今の一覧の線が 1 本も出ていない時）は、セットの曲を全部出す。目標は番号で足す。
    overlaySwitch.onChoose = [this] (bool all)
    {
        if (current.overlay.all == all) return;
        auto probe = current;
        probe.overlay.all = all;
        const auto items = overlayItems (probe);
        if (all && std::none_of (items.begin(), items.end(), [] (const auto& item) { return item.line != nullptr; }))
        {
            std::vector<juce::String> listed;
            for (const auto& item : items) listed.push_back (item.id);
            for (const auto& item : items) if (! item.target) probe.overlay.toggle (item.id, listed);
        }
        applyOverlay (std::move (probe.overlay));
    };
    addChildComponent (overlaySwitch);
}

void Component::applyOverlay (reference_audition::BalanceOverlay next)
{
    if (current.visualPreferences) current.visualPreferences->setOverlay (next);
    current.overlay = std::move (next);
    setState (State (current));
}

// 300% 未満の C と V は薄く描き、押すと 300% に広げてその役の画面を開く（Blind と同じ動き）。
// 鳴らすのは 300% でもう一度押したとき。広げる前に音は変えない。
bool Component::openLarge (int slot)
{
    if (! current.separateComparisons || current.blindLargeScreen || isBlindSession (current.blindPhase)) return false;
    if (onSelectVisualSlot) onSelectVisualSlot (slot);
    if (onOpenLarge) onOpenLarge (slot);
    return true;
}

void Component::syncRoles (bool blindSession)
{
    const bool waiting = current.pendingAudition.waiting() && current.pendingAudition.slot == 3;
    // 曲の切替のあいだも B は鳴っている。新しい曲の準備で押せないように見せない。
    const bool roleSounds = current.bSelected && current.audibleComparisonSlot == 3;
    const bool audible = canHearReference (current) || roleSounds, queue = canQueueReference (current);
    refButton.setToggleState (roleSounds, juce::dontSendNotification);
    refButton.setVisible (! blindSession && current.separateComparisons);
    // 予約（DAW の再生を待つ）は色で示し、何を待つかは状態の行が言う（「B...」は文字が切れたように見えた。2026-10-04）。
    refButton.setButtonText ("B");
    refButton.setAttention (waiting);
    refButton.setReady (audible || queue);
    refButton.setTooltip (queue ? "Queue B for DAW playback. A stays live until ready; press A to cancel."
        : audible ? juce::String ("Audition a Reference song from Kirin OS (B).") : referenceUnavailableText (current));
    syncSelectionControl (songSetBox, current.songSets, current.songSetId);
    syncSelectionControl (songBox, current.songs, current.songId);
    const bool referenceView = current.separateComparisons && current.comparisonSlot == 3 && ! blindSession;
    // B の曲と Reference のセットは、どの大きさでも選べる（2026-11-11：100% では見つからず、切り替えられなかった）。
    // A を鳴らしたままでも替えられる（B が鳴っていれば前の曲を鳴らしたまま次の曲を準備する）。
    songSetBox.setVisible (referenceView && ! current.songSets.empty());
    songBox.setVisible (referenceView && ! current.songs.empty());
    // 300% の B の画面は、左に曲の一覧、右に Balance。Balance の見出しの右に ONE／ALL。
    songList.setVisible (referenceView && rolePage() && ! current.songs.empty());
    overlaySwitch.setVisible (songList.isVisible());
    overlaySwitch.setChoice (current.overlay.all, presentationContext);
    std::vector<SongList::Row> rows;
    for (const auto& item : overlayItems (current))
    {
        const auto colour = item.line != nullptr ? item.line->colour : 0;
        if (item.target)
        {
            // 目標（ALL だけ）：Kirin OS の値の代わりに測った値。MATCH は線をそろえた gain（鳴らさない）。
            const auto* target = current.targets.collection.find (item.id);
            rows.push_back ({ item.id, item.name, target != nullptr ? target->integrated : std::numeric_limits<double>::quiet_NaN(),
                              displayGainDb (current, item.gainDb) });
            rows.back().target = true;
            rows.back().shown = item.line != nullptr;
            rows.back().colour = colour;
            continue;
        }
        const auto index = static_cast<size_t> (item.number - 1);
        const auto& song = current.songs[index];
        const auto fact = index < current.songFacts.size() ? current.songFacts[index] : SongFact {};
        const bool selected = song.id == current.songId;
        // 選んだ行はすぐ選択中になり、PLAYING は実際に出力へ出た曲だけに付く。
        const bool playing = roleSounds && song.id == current.audibleSongId;
        // MATCH：鳴っている曲は実際の gain、ほかは押したときの gain（A の窓 − その曲の Cue の値）。どちらも読みの基準で。
        const auto would = fact.prepared && ! preparationFailed (fact.preparation)
            ? songPreviewGainDb (current, fact, selected) : std::numeric_limits<double>::quiet_NaN();
        rows.push_back ({ song.id, songChoiceName (song.label), fact.lufsI,
                          displayGainDb (current, playing ? current.appliedGainDb : would),
                          selected, playing, ! fact.prepared, preparationWord (fact.preparation),
                          selected && ! playing && current.action.kind == ActionKind::lowerAAndPlay
                              && current.action.offer.slot == 3 ? current.action.offer.db : std::numeric_limits<double>::quiet_NaN(),
                          selected && ! playing && (roleSounds || current.switchingSlot == 3), fact.loaded });
        rows.back().shown = item.line != nullptr;
        rows.back().colour = colour;
    }
    songList.setOverlay (current.overlay.all);
    songList.setRows (std::move (rows), presentationContext);
    songList.setLive (current.aWindowLoudness, juce::roundToInt (static_cast<double> (current.aWindowBlocks) / 10.0));
    if (! referenceView) return;
    // B の画面には V・C の選択を出さない（REFERENCE SET と曲だけ）。
    for (auto* box : { &versionBox, &checkBox, &presetBox, &cueBox }) box->setVisible (false);
}

bool Component::explainReference()
{
    if (canHearReference (current) || canQueueReference (current)
        || (current.bSelected && current.audibleComparisonSlot == 3)) return false;
    if (onSelectVisualSlot) onSelectVisualSlot (3);
    if (onExplain) onExplain (referenceUnavailableText (current));
    return true;
}
}
