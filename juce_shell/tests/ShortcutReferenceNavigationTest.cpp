#include "ShortcutKeyRig.h"
#include "../src/HyphaReferenceVersionRows.h"
#include <string>

#if ! KIRIN_HYPHA_PRE_DISPLAY
namespace hypha::tests
{
namespace
{
using namespace reference_ui;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

SelectionOption option (const juce::String& id, const juce::String& work = {})
{
    SelectionOption row { id, id };
    row.workId = work;
    return row;
}

SelectionOption check (const juce::String& checkId, const juce::String& song)
{
    SelectionOption row { checkId + "/" + song, checkId + "  /  " + song };
    row.checkLabel = checkId;
    row.itemLabel = song;
    return row;
}

State readyState()
{
    State state;
    state.separateComparisons = state.libraryReceived = state.aAvailable = state.blindLargeScreen = true;
    state.osAccess = os_access::State::ready;
    state.versionReady = state.checkReady = state.referenceReady = true;
    state.songs = { option ("s1"), option ("s2"), option ("s3") };
    state.songId = "s2";
    state.songSets = { option ("set1"), option ("set2") };
    state.songSetId = "set1";
    state.presets = { option ("p1"), option ("p2"), option ("p3") };
    state.presetId = "p1";
    state.checks = { check ("tone", "one"), check ("tone", "two"), check ("space", "two"), check ("space", "three"), check ("low", "one") };
    state.checkId = "tone/two";
    state.cues = { option ("cue1"), option ("cue2") };
    state.cueId = "cue1";
    state.versions = { option ("a1", "song-a"), option ("a2", "song-a"), option ("b1", "song-b"), option ("c1", "song-c"), option ("c2", "song-c") };
    state.versionId = "a2";
    state.comparisonMode = "loudness_match";
    return state;
}

struct Spy
{
    juce::StringArray calls;
    void watch (Component& component)
    {
        component.onSelectA = [this] { calls.add ("A"); };
        component.onSelectB = [this] { calls.add ("V"); };
        component.onSelectC = [this] { calls.add ("C"); };
        component.onSelectRef = [this] { calls.add ("B"); };
        component.onMatch = [this] { calls.add ("match"); };
        component.onSelectSong = [this] (const juce::String& id) { calls.add ("song " + id); };
        component.onSelectSongSet = [this] (const juce::String& id) { calls.add ("songset " + id); };
        component.onSelectVersion = [this] (const juce::String& id) { calls.add ("version " + id); };
        component.onSelectCheck = [this] (const juce::String& id) { calls.add ("check " + id); };
        component.onSelectCue = [this] (const juce::String& id) { calls.add ("cue " + id); };
        component.onSelectPreset = [this] (const juce::String& id) { calls.add ("preset " + id); };
        component.onOpenLarge = [this] (int slot) { calls.add ("open " + juce::String (slot)); };
        component.onSelectVisualSlot = [] (int) {};
        component.onExplain = [this] (const juce::String&) { calls.add ("explain"); };
    }
    void expect (const char* call, const std::string& what)
    {
        require (calls.size() == 1 && calls[0] == call, what + " (saw: " + calls.joinIntoString (", ").toStdString() + ")");
        calls.clear();
    }
    void expectNone (const std::string& what)
    {
        require (calls.isEmpty(), what + " (saw: " + calls.joinIntoString (", ").toStdString() + ")");
    }
};

void verifyRolesFromKeys()
{
    ShortcutKeyRig rig;
    auto& reference = rig.editor.get()->*shortcutMember (ShortcutReference {});
    rig.editor->setSize (900, 600);
    rig.view().onDomainChange (observatory::Domain::reference);
    Spy spy;
    spy.watch (reference);
    reference.setState (readyState());
    reference.setVisible (true);
    rig.editor->grabKeyboardFocus();
    rig.press ('1'); spy.expect ("A", "1 is A");
    rig.press ('2'); spy.expect ("B", "2 is B (REF)");
    rig.press ('3'); spy.expect ("C", "3 is C");
    rig.press ('4'); spy.expect ("V", "4 is V");
    rig.press ('T'); spy.expect ("A", "T returns to A");

    auto state = readyState();
    state.comparisonSlot = 3;
    reference.setState (state);
    rig.press ('D'); spy.expect ("song s3", "D is the next B song");
    rig.press ('A'); spy.expect ("song s1", "A is the B song before");
    rig.pressShift ('W'); spy.expect ("songset set2", "Shift+W is the next B SET");
    state.comparisonSlot = 2;
    reference.setState (state);
    rig.press ('C'); spy.expect ("check space/two", "C is the next Check, on the same song");
    rig.pressShift ('C'); spy.expect ("cue cue2", "Shift+C is the next Cue");
    rig.pressShift ('Q'); spy.expectNone ("the first CHECK SET stays");
    rig.press ('G'); spy.expect ("C", "G plays C matched");
    state.bSelected = true; state.audibleComparisonSlot = 2;
    reference.setState (state);
    rig.press ('G'); spy.expect ("match", "G matches a playing C again");
    state.comparisonSlot = 1;
    reference.setState (state);
    rig.pressShift ('D'); spy.expectNone ("the last Version of the song stays");
    rig.pressShift ('A'); spy.expect ("version a1", "Shift+A is the Version before");
    rig.press ('D'); spy.expect ("version b1", "D is the next song's first Version");

    // Small windows: C and V open first and do not play on that press.
    state = readyState();
    state.blindLargeScreen = false;
    reference.setState (state);
    rig.press ('3'); spy.expect ("open 2", "a small C opens and stays silent");
    rig.press ('4'); spy.expect ("open 1", "a small V opens and stays silent");

    // BLIND shuts all of it.
    auto& blindOpen = rig.editor.get()->*shortcutMember (ShortcutLiveBlindOpen {});
    blindOpen = true;
    for (const auto key : { '1', '3', 'A', 'D', 'X', 'C', 'G' }) rig.press (key);
    rig.pressShift ('A'); rig.pressShift ('Q');
    spy.expectNone ("BLIND blocks Reference keys");
    blindOpen = false;
}

void verifyMoves()
{
    Component component;
    component.setPresentationContext (presentation::forEditor (900, 600));
    component.setBounds (0, 0, 900, 600);
    Spy spy;
    spy.watch (component);
    auto state = readyState();

    state.comparisonSlot = 3;
    component.setState (state);
    component.stepSong (1); spy.expect ("song s3", "next B song");
    state.songId = "s3"; component.setState (state);
    require (component.stepSong (1) == "Last in the list", "the end says so"); spy.expectNone ("the end stays");
    state.songId = "s1"; component.setState (state);
    require (component.stepSong (-1) == "First in the list", "the start says so"); spy.expectNone ("the start stays");
    state.songId = "gone"; component.setState (state);
    component.stepSong (1); spy.expect ("song s1", "a lost choice restarts at the first");
    component.stepSong (-1); spy.expect ("song s3", "or at the last");
    state.songs.clear(); component.setState (state);
    require (component.stepSong (1) == "Nothing to choose", "an empty list says so"); spy.expectNone ("nothing to choose");
    require (component.stepTab (1) == "Available in C and V", "B has no tabs");
    require (component.stepCue (1) == "Available in C", "B has no Cue");
    require (component.stepVersion (1) == "Available in V", "B has no Versions");

    state = readyState();
    state.comparisonSlot = 2;
    component.setState (state);
    component.stepSong (-1); spy.expect ("check tone/one", "the song before, in this Check");
    state.checkId = "tone/one"; component.setState (state);
    component.stepSong (1); spy.expect ("check tone/two", "the next song, in this Check");
    state.checkId = "low/one"; component.setState (state);
    component.stepTab (-1); spy.expect ("check space/two", "no song one in this Check: its first song");
    require (component.stepTab (1) == "Last in the list", "the last Check"); spy.expectNone ("last Check");
    // 2026-11-11：役を押した直後の古い画面（今の画面は別）のタブは何も替えない（画面が描き直される）。
    int pageChecks = 0;
    component.pageCurrent = [&pageChecks] (int page) { ++pageChecks; return page == 1; };
    component.stepTab (-1); spy.expectNone ("a tab on a stale page");
    require (pageChecks == 1, "the tab asked whether its page is still the current one");
    component.pageCurrent = [] (int page) { return page == 2; };
    component.stepTab (-1); spy.expect ("check space/two", "a tab on the current page applies");
    component.pageCurrent = nullptr;

    // V walks the order Kirin OS sent, not the shown order that lifts the chosen song to the top.
    state = readyState();
    state.comparisonSlot = 1;
    arrangeVersionRows (state.versions, "b1", {});
    state.versionId = "b1";
    component.setState (state);
    require (state.versions.front().id == "b1", "the shown order moved the chosen song");
    component.stepSong (1); spy.expect ("version c1", "after song b comes song c");
    component.stepSong (-1); spy.expect ("version a1", "before song b comes song a");
    state = readyState();
    state.comparisonSlot = 1;
    state.versionId = "c1";
    arrangeVersionRows (state.versions, "c1", {});
    component.setState (state);
    component.stepVersion (1); spy.expect ("version c2", "the next Version of the song");

    // The V tabs change the view alone.
    state = readyState();
    state.comparisonSlot = 1;
    component.setState (state);
    require (component.stepTab (1).isEmpty(), "the next V tab");
    spy.expectNone ("a V tab changes no sound and no selection");

    // 100% keeps the choice of B's song to the larger sizes.
    component.setPresentationContext (presentation::forEditor (300, 200));
    state = readyState();
    state.comparisonSlot = 3;
    component.setState (state);
    require (component.stepSong (1) == "Available from 125%", "100% does not change B's song");
    require (component.stepSet (1) == "Available from 125%", "100% does not change B's set");
    spy.expectNone ("100% changed nothing");

    state.blindPhase = BlindPhase::active;
    component.setState (state);
    require (! component.stepSong (1).isEmpty() && ! component.stepSet (1).isEmpty() && ! component.stepCue (1).isEmpty()
             && ! component.requestRole (2).isEmpty(), "BLIND refuses");
    spy.expectNone ("BLIND changed nothing");
}
}

void verifyShortcutReferenceMoves()
{
    verifyMoves();
    verifyRolesFromKeys();
}
}
#endif
