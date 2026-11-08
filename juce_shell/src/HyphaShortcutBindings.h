#pragma once

// The fixed keys of the Hypha window. Input (HyphaShortcutRouter.h), the marks on hover and the KEYS
// list (HyphaShortcutText.h) all read the two tables below and nothing else names a key, so a later
// version can put the user's own assignment here.
//
// Rules of the table: a key is one character, with or without Shift. Space, the numeric keypad and
// Cmd / Ctrl / Alt stay the host's. Approving a level change, Record / Keep and the pair have no key:
// they are reached by Tab and Return, so a stray press cannot approve anything.
namespace hypha::shortcuts
{
enum class Command
{
    showLevel, showTime, showFrequency, showSpace, showReference,
    showAbsolute, showDelta,
    previousTimePage, nextTimePage,
    startListen, listenPre, listenPost, matchListen, chooseFollow, endListen,
    startCandidateCompare, candidateA, candidateB, endCandidateCompare,
    startBlind, blindSourceOne, blindSourceTwo, revealBlind, endBlind,
    referenceA, referenceB, referenceC, referenceV, matchReference, endReference,
    previousSong, nextSong, previousVersion, nextVersion,
    previousTab, nextTab, previousCue, nextCue, previousSet, nextSet,
    returnNormalLevel, toggleHybridVu, clearPeakClip, resetMeterSession,
    toggleKeys, closeKeys,
    moveFocus, moveFocusBack, activateFocused,
};

// Who owns a key at this moment, highest first (scopeOrder in HyphaShortcutRouter.h).
enum class Scope
{
    keysList,          // the KEYS list is open
    blind,             // a Blind runs: its anonymous controls and nothing else
    candidateCompare,  // A/B runs
    listen,            // LISTEN runs, on whichever page is showing
    reference,         // the REFERENCE page
    time,              // the TIME page
    everywhere,        // any page, outside Blind
    always,            // any page, Blind included
    focus,             // moving between controls: JUCE does this; the row is only listed
    candidateEntry,    // opens A/B
};

constexpr int escapeCode = 0x1b, tabCode = 0x09, returnCode = 0x0d;

struct Binding
{
    Command command;
    Scope scope;
    int key;              // the key's upper-case character, or one of the codes above
    bool shift;
    const char* label;    // the short word of the mark, as the control names itself
    bool postOnly = false;
    bool pairsWithNext = false;  // previous / next: one row in KEYS, one mark
};

namespace table
{
using C = Command;
using S = Scope;
inline constexpr Binding bindings[] {
    { C::toggleKeys,            S::always,           'F', false, "KEYS" },
    { C::closeKeys,             S::keysList,         escapeCode, false, "CLOSE" },
    { C::showLevel,             S::everywhere,       '1', true,  "LEVEL" },
    { C::showTime,              S::everywhere,       '2', true,  "TIME" },
    { C::showFrequency,         S::everywhere,       '3', true,  "FREQ", true },
    { C::showSpace,             S::everywhere,       '4', true,  "SPACE" },
    { C::showReference,         S::everywhere,       '5', true,  "REFERENCE", true },
    { C::showAbsolute,          S::everywhere,       'Z', false, "ABSOLUTE" },
    { C::showDelta,             S::everywhere,       'Z', true,  "DELTA", true },
    { C::startListen,           S::everywhere,       'Q', false, "LISTEN", true },
    { C::listenPre,             S::listen,           '1', false, "PRE", true },
    { C::listenPost,            S::listen,           '2', false, "POST", true },
    { C::matchListen,           S::listen,           'G', false, "MATCH", true },
    { C::chooseFollow,          S::listen,           'G', true,  "AUTO", true },
    { C::endListen,             S::listen,           'T', false, "END", true },
    { C::startBlind,            S::everywhere,       'E', false, "BLIND", true },
    { C::blindSourceOne,        S::blind,            '1', false, "SOURCE 1", true },
    { C::blindSourceTwo,        S::blind,            '2', false, "SOURCE 2", true },
    { C::revealBlind,           S::blind,            'E', true,  "REVEAL", true },
    { C::endBlind,              S::blind,            'T', false, "END", true },
    { C::previousTimePage,      S::time,             'X', false, "PAGE", true, true },
    { C::nextTimePage,          S::time,             'C', false, "PAGE", true },
    { C::referenceA,            S::reference,        '1', false, "A", true },
    { C::referenceB,            S::reference,        '2', false, "B", true },
    { C::referenceC,            S::reference,        '3', false, "C", true },
    { C::referenceV,            S::reference,        '4', false, "V", true },
    { C::matchReference,        S::reference,        'G', false, "MATCH", true },
    { C::endReference,          S::reference,        'T', false, "END", true },
    { C::previousSong,          S::reference,        'A', false, "SONG", true, true },
    { C::nextSong,              S::reference,        'D', false, "SONG", true },
    { C::previousVersion,       S::reference,        'A', true,  "VERSION", true, true },
    { C::nextVersion,           S::reference,        'D', true,  "VERSION", true },
    { C::previousTab,           S::reference,        'X', false, "TAB", true, true },
    { C::nextTab,               S::reference,        'C', false, "TAB", true },
    { C::previousCue,           S::reference,        'X', true,  "CUE", true, true },
    { C::nextCue,               S::reference,        'C', true,  "CUE", true },
    { C::previousSet,           S::reference,        'Q', true,  "SET", true, true },
    { C::nextSet,               S::reference,        'W', true,  "SET", true },
    { C::returnNormalLevel,     S::everywhere,       'T', true,  "RETURN", true },
    { C::toggleHybridVu,        S::everywhere,       'V', false, "VU" },
    { C::clearPeakClip,         S::everywhere,       'F', true,  "CLEAR" },
    { C::resetMeterSession,     S::everywhere,       'V', true,  "RESET" },
    { C::moveFocus,             S::focus,            tabCode, false, "NEXT" },
    { C::moveFocusBack,         S::focus,            tabCode, true,  "BACK" },
    { C::activateFocused,       S::focus,            returnCode, false, "PRESS" },
    // A/B runs in POST: Context::candidateCompareAvailable turns these on there. W opens the same menus
    // as the button, placement confirmation included (PluginEditorShortcutDispatch.cpp).
    { C::startCandidateCompare, S::candidateEntry,   'W', false, "A/B", true },
    { C::candidateA,            S::candidateCompare, '1', false, "A", true },
    { C::candidateB,            S::candidateCompare, '2', false, "B", true },
    { C::endCandidateCompare,   S::candidateCompare, 'T', false, "END", true },
};

// The controls that do what a key does. Hovering one shows its key; a previous / next pair shows both
// keys, from the row of the previous one. `last`: the control stands for a run of keys from `command`.
struct Control { const char* id; Command command; const Command* last = nullptr; };
inline constexpr Command lastDomain = C::showReference;
inline constexpr Control controls[] {
    { "observatory-domain-cycle", C::showLevel, &lastDomain },
    { "observatory-level", C::showLevel },
    { "observatory-time", C::showTime },
    { "observatory-frequency", C::showFrequency },
    { "observatory-space", C::showSpace },
    { "observatory-reference", C::showReference },
    { "observatory-target", C::showAbsolute },
    { "observatory-delta", C::showDelta },
    { "observatory-live-compare", C::startListen },
    { "observatory-live-pre", C::listenPre },
    { "observatory-live-post", C::listenPost },
    { "observatory-live-match", C::matchListen },
    { "observatory-live-end", C::endListen },
    { "observatory-live-return", C::returnNormalLevel },
    { "observatory-candidate-compare", C::startCandidateCompare },
    { "observatory-local-blind", C::startBlind },
    { "observatory-hybrid-vu", C::toggleHybridVu },
    { "observatory-clear-peak-clip", C::clearPeakClip },
    { "observatory-reset", C::resetMeterSession },
    { "time-page-cycle", C::previousTimePage },
    { "time-page-history", C::previousTimePage },
    { "time-page-run", C::previousTimePage },
    { "time-page-drum", C::previousTimePage },
    { "time-page-sharp", C::previousTimePage },
    { "time-page-live", C::previousTimePage },
    { "reference-a", C::referenceA },
    { "reference-ref", C::referenceB },
    { "reference-c", C::referenceC },
    { "reference-b", C::referenceV },   // the V button keeps its old id
    { "reference-match", C::matchReference },
    { "reference-blind", C::startBlind },
    { "reference-song", C::previousSong },
    { "reference-song-list", C::previousSong },
    { "reference-check-song", C::previousSong },
    { "reference-version", C::previousVersion },
    { "reference-check-tabs", C::previousTab },
    { "reference-cue", C::previousCue },
    { "reference-preset", C::previousSet },
    { "reference-song-set", C::previousSet },
    { "live-blind-source-1", C::blindSourceOne },
    { "live-blind-source-2", C::blindSourceTwo },
    { "live-blind-reveal", C::revealBlind },
    { "live-blind-end", C::endBlind },
    { "version-blind-source-1", C::blindSourceOne },
    { "version-blind-source-2", C::blindSourceTwo },
    { "version-blind-reveal", C::revealBlind },
    { "version-blind-end", C::endBlind },
    { "local-blind-source-1", C::blindSourceOne },
    { "local-blind-source-2", C::blindSourceTwo },
    { "local-blind-reveal", C::revealBlind },
    { "local-blind-stop", C::endBlind },
    { "keys-close", C::closeKeys },
};
// A/B runs on the LISTEN buttons. While it does, they carry this property and name its keys.
inline constexpr const char* candidateKeysProperty = "shortcutCandidateKeys";
inline constexpr Control candidateControls[] {
    { "observatory-live-pre", C::candidateA },
    { "observatory-live-post", C::candidateB },
    { "observatory-live-end", C::endCandidateCompare },
};
}
}
