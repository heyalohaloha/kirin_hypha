#include "HyphaReferenceComponent.h"
#include "HyphaReferencePendingUI.h"
#include "HyphaReferenceStatusModel.h"
#include "HyphaTheme.h"

// H10: A／B／C／V の B（REF）。Kirin OS が「Hypha に出す」にした B セットの曲を鳴らす役のボタンと、
// B SET（順位つき、最大 3）と曲の選択。B を押すと B の画面になり、B のまま曲を選ぶとすぐ切り替わる。
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

juce::String referenceUnavailableText (const State& state)
{
    const auto step = ! state.libraryReceived ? SourceStep::waitingForKirinOs : state.referenceStep;
    return "B: " + (! state.libraryReceived && ! state.osOnline ? juce::String ("Open Kirin OS")
                    : state.songSets.empty() ? juce::String ("Rank a B set for Hypha in Kirin OS")
                    : stepText (step));
}
}

void Component::configureRoles()
{
    refButton.setComponentID ("reference-ref");
    refButton.setTitle ("Audition B Reference");
    refButton.onClick = [this] { if (! explainReference() && onSelectRef) onSelectRef(); };
    addAndMakeVisible (refButton);
    songSetBox.setLookAndFeel (&selectorLookAndFeel);
    songBox.setLookAndFeel (&selectorLookAndFeel);
    configureBox (songSetBox, "reference-song-set", "B Set", "Choose a B set Kirin OS ranked for Hypha.");
    configureBox (songBox, "reference-song", "B Song", "Choose the song B plays. A stays the current DAW input.");
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
    songList.onChoose = [this] (const juce::String& id) { if (id != current.songId && onSelectSong) onSelectSong (id); };
    addChildComponent (songList);
}

// H10: 300% 未満の C と V は薄く描き、押すと 300% に広げてその役の画面を開く（Blind と同じ動き）。
// 鳴らすのは 300% でもう一度押したとき。広げる前に音は変えない。
bool Component::openLarge (int slot)
{
    if (! current.separateComparisons || current.blindLargeScreen || isBlindSession (current.blindPhase)) return false;
    if (onSelectVisualSlot) onSelectVisualSlot (slot);
    if (onOpenLarge) onOpenLarge (slot);
    return true;
}

void Component::syncRoles (bool blindSession, bool workflowActive)
{
    const bool waiting = current.pendingAudition.waiting() && current.pendingAudition.slot == 3;
    const bool audible = canHearReference (current), queue = canQueueReference (current);
    refButton.setToggleState (current.bSelected && current.audibleComparisonSlot == 3, juce::dontSendNotification);
    refButton.setVisible (! blindSession && current.separateComparisons);
    refButton.setButtonText (waiting ? "B..." : "B");
    refButton.setAttention (waiting);
    refButton.setReady (audible || queue);
    refButton.setTooltip (queue ? "Queue B for DAW playback. A stays live until ready; press A to cancel."
        : audible ? juce::String ("Audition a song of the B set from Kirin OS (B).") : referenceUnavailableText (current));
    syncSelectionControl (songSetBox, current.songSets, current.songSetId);
    syncSelectionControl (songBox, current.songs, current.songId);
    const bool referenceView = current.separateComparisons && current.comparisonSlot == 3 && ! blindSession;
    // H10: 100% の B は曲名・gain・状態だけ。B SET は出さず、曲の切替は 125% 以上。
    const bool glance = presentationContext.density == observatory::Density::compact;
    songSetBox.setVisible (referenceView && ! glance && ! workflowActive && ! current.songSets.empty());
    songBox.setVisible (referenceView && ! workflowActive && ! current.songs.empty());
    songBox.setEnabled (songBox.isEnabled() && ! glance);
    // H11: 300% 以上の B の画面は、左に曲の一覧、右に Balance。
    songList.setVisible (referenceView && detailedLayout() && ! workflowActive && ! current.songs.empty());
    std::vector<SongList::Row> rows;
    for (size_t index = 0; index < current.songs.size(); ++index)
    {
        const auto& song = current.songs[index];
        const auto fact = index < current.songFacts.size() ? current.songFacts[index] : SongFact {};
        const bool selected = song.id == current.songId;
        const bool playing = selected && current.bSelected && current.audibleComparisonSlot == 3;
        rows.push_back ({ song.id, song.label.upToFirstOccurrenceOf ("   PREPARING", false, false), fact.lufsI,
                          playing ? current.appliedGainDb : std::numeric_limits<double>::quiet_NaN(),
                          selected, playing, ! fact.prepared, preparationWord (fact.preparation) });
    }
    songList.setRows (std::move (rows), presentationContext);
    if (! referenceView) return;
    // B の画面には V・C の選択を出さない（B SET と曲だけ）。
    for (auto* box : { &versionBox, &checkBox, &presetBox, &cueBox }) box->setVisible (false);
}

bool Component::explainReference()
{
    if (canHearReference (current) || canQueueReference (current)) return false;
    if (onSelectVisualSlot) onSelectVisualSlot (3);
    if (onExplain) onExplain (referenceUnavailableText (current));
    return true;
}
}
