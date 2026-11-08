#include "ShortcutKeyRig.h"
#include "../src/HyphaShortcutText.h"
#include <algorithm>
#include <set>
#include <string>

namespace hypha::tests
{
namespace
{
using namespace shortcuts;
using Domain = hypha::observatory::Domain;
constexpr auto shift = juce::ModifierKeys::shiftModifier;

void require (bool condition, const std::string& message)
{
    if (! condition) throw std::runtime_error (message);
}

void verifyKeysGoBackToTheHost (ShortcutKeyRig& rig)
{
    const auto domain = rig.view().domain();
    const auto vu = rig.view().hybridVuVisible();
    using KeyPress = juce::KeyPress;
    require (! rig.press (KeyPress::spaceKey, 0, ' '), "Space stays the host's");
    for (const auto pad : { KeyPress::numberPad0, KeyPress::numberPad1, KeyPress::numberPad5, KeyPress::numberPadAdd,
                            KeyPress::numberPadSubtract, KeyPress::numberPadDecimalPoint, KeyPress::numberPadDelete })
        require (! rig.press (pad), "the numeric keypad stays the host's");
    for (const auto mods : { juce::ModifierKeys::commandModifier, juce::ModifierKeys::ctrlModifier, juce::ModifierKeys::altModifier })
        for (const auto key : { 'Q', 'S', 'Z', '1' })
            require (! rig.press (key, mods), "Cmd, Ctrl and Alt combinations stay the host's");
    require (! rig.press ('Z', juce::ModifierKeys::commandModifier | shift), "Redo stays the host's");
    require (! rig.press ('J'), "a key without a job stays the host's");
   #if KIRIN_HYPHA_PRE_DISPLAY
    require (! rig.press ('W'), "W stays the host's in PRE: A/B runs in POST");
   #else
    require (rig.press ('W') && ! rig.processor.candidateCompareActive(), "W asks first: it never starts A/B by itself");
    juce::PopupMenu::dismissAllActiveMenus();
    for (int n = 0; n < 40 && juce::Component::getNumCurrentlyModalComponents() > 0; ++n)
        require (afterQueuedMessages ([] {}), "the menu's close finishes");
    rig.clearToast();
    rig.editor->grabKeyboardFocus();
   #endif
    require (! rig.press (KeyPress::escapeKey), "Esc stays the host's when no list is open");
    require (! rig.press (KeyPress::returnKey, 0, '\r'), "Return stays the host's with no control chosen");
    require (rig.view().domain() == domain && rig.view().hybridVuVisible() == vu, "keys for the host changed the window");
}

void verifyTextAndMenusComeFirst (ShortcutKeyRig& rig)
{
    auto& view = rig.view();
    const auto vu = view.hybridVuVisible();
   #if KIRIN_HYPHA_PRE_DISPLAY
    auto& name = rig.editor.get()->*shortcutMember (ShortcutName {});
    name.grabKeyboardFocus();
    require (! rig.press (juce::KeyPress::spaceKey, 0, ' ') && ! name.isEditing(), "Space does not open the name");
    require (rig.press (juce::KeyPress::returnKey, 0, '\r') && name.isEditing(), "Return opens the name");
    require (dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr, "the name takes text");
    for (const auto letter : { 'V', 'Q', 'F', '1' })
        require (rig.press (letter, 0, (juce::juce_wchar) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) letter)), "the field takes the letter");
    rig.press ('@', shift, '@');
    rig.press (juce::KeyPress::spaceKey, 0, ' ');
    require (view.hybridVuVisible() == vu && view.domain() == Domain::level && ! rig.keys().isVisible(), "typing ran a command");
    rig.press (juce::KeyPress::escapeKey);
    require (afterQueuedMessages ([] {}) && ! name.isEditing(), "Esc leaves the name");
   #endif
    rig.editor->grabKeyboardFocus();
    view.onDomainMenu();
    require (juce::Component::getNumCurrentlyModalComponents() > 0, "the menu opened");
    rig.press ('V');
    rig.press ('F');
    require (view.hybridVuVisible() == vu && ! rig.keys().isVisible(), "a key behind an open menu ran a command");
    juce::PopupMenu::dismissAllActiveMenus();
    for (int wait = 0; wait < 20 && juce::Component::getNumCurrentlyModalComponents() > 0; ++wait)
        require (afterQueuedMessages ([] {}), "menu close did not finish");
    rig.editor->grabKeyboardFocus();
}

void verifyOnePressOneAction (ShortcutKeyRig& rig)
{
    auto& view = rig.view();
    view.onDomainChange (Domain::level);
    require (rig.hold ('@', shift, '@') && view.domain() == Domain::time, "Shift+2 moves to TIME");
    view.onDomainChange (Domain::level);
    require (rig.hold ('@', shift, '@') && view.domain() == Domain::level, "a repeat is the same press, and is still used");
    rig.up();
    require (rig.hold ('@', shift, '@') && view.domain() == Domain::time, "the next press is a new one");
    rig.up();
    view.onDomainChange (Domain::level);

    const auto vu = view.hybridVuVisible();
    rig.hold ('V');
    require (view.hybridVuVisible() != vu, "V shows the VU");
    auto& dialog = rig.editor.get()->*shortcutMember (ShortcutResetDialog {});
    rig.hold ('V', shift);
    require (dialog == nullptr, "V held, then Shift, is not Shift+V");
    rig.up();
    rig.press ('V');
    require (view.hybridVuVisible() == vu, "V hides the VU");

    rig.hold ('F');
    require (rig.keys().isVisible(), "F opens KEYS");
    rig.hold ('F'); rig.hold ('F');
    require (rig.keys().isVisible(), "F held does not flicker the list");
    rig.up();
    rig.press ('F');
    require (! rig.keys().isVisible(), "F closes KEYS");

    // Focus lost with a key down: nothing stops, and that key is free again.
    rig.hold ('V');
    rig.editor->giveAwayKeyboardFocus();
    require (view.hybridVuVisible() != vu, "losing focus changes nothing");
    rig.editor->grabKeyboardFocus();
    rig.press ('V');
    require (view.hybridVuVisible() == vu, "a key held across a focus change is not stuck");
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
// BLIND keeps its keys and shuts the rest, and says so on its own screen: the footer is covered.
void verifyBlindKeys (ShortcutKeyRig& rig)
{
    auto& view = rig.view();
    auto& open = rig.editor.get()->*shortcutMember (ShortcutLiveBlindOpen {});
    auto& blind = rig.editor.get()->*shortcutMember (ShortcutLiveBlindView {});
    int chosen = 0, reveals = 0, ends = 0;
    blind.onSelect = [&] (int source) { chosen = source; };
    blind.onReveal = [&] { ++reveals; };
    blind.onEnd = [&] { ++ends; };
    blind_ui::Screen screen;
    screen.sourcesShown = screen.sourceOneEnabled = screen.revealShown = true;
    blind.setScreen (screen);
    blind.setVisible (true);
    open = true;
    const auto domain = view.domain();
    const auto vu = view.hybridVuVisible();
    require (rig.press ('1') && chosen == 1, "1 is SOURCE 1");
    require (rig.press ('2') && chosen == 1 && blind.actionNoticeShown() == "SOURCE COULD NOT BE SELECTED", "a source that cannot be chosen says so");
    require (rig.pressShift ('E') && reveals == 0 && blind.actionNoticeShown() == "Hear both sources first", "REVEAL waits, and says so");
    require (rig.pressShift ('2') && view.domain() == domain, "BLIND holds the page");
    auto* reference = dynamic_cast<juce::Button*> (view.findChildWithID ("observatory-reference"));
    require (reference != nullptr, "REFERENCE header action exists during Blind");
    reference->onClick();
    require (view.domain() == domain, "the header REFERENCE request is refused during BLIND");
    require (blind.actionNoticeShown() == "End Blind Compare first", "the refusal is on the screen");
    require (rig.press ('V') && rig.pressShift ('F') && rig.pressShift ('V') && rig.press ('G') && rig.press ('Q'), "BLIND's refusals are used keys");
    auto& dialog = rig.editor.get()->*shortcutMember (ShortcutResetDialog {});
    require (view.hybridVuVisible() == vu && dialog == nullptr, "BLIND shuts the meter keys");
    require (rig.toast().isEmpty(), "nothing goes to the covered footer");
    rig.press ('F');
    require (rig.keys().isVisible(), "KEYS works in BLIND");
    for (const auto& row : rig.keys().rows())
        for (const auto* word : { "LEVEL", "REFERENCE", "MATCH", "LISTEN", "PRE", "POST", "A", "B", "C", "V" })
            require (row.label != word, "BLIND's list names only BLIND's controls");
    rig.press ('F');
    screen.sourceTwoEnabled = screen.revealEnabled = true;
    blind.setScreen (screen);
    require (rig.press ('2') && chosen == 2 && rig.pressShift ('E') && reveals == 1 && rig.press ('T') && ends == 1,
             "the keys reach the same requests as the buttons");
    open = false;
    blind.setVisible (false);
    rig.editor->grabKeyboardFocus();
}
#endif

void verifyKeysList (ShortcutKeyRig& rig)
{
    auto& view = rig.view();
    const auto vu = view.hybridVuVisible();
    auto* before = view.findChildWithID ("observatory-time");
    before->grabKeyboardFocus();
    rig.press ('F');
    auto& list = rig.keys();
    require (list.isVisible() && ! list.rows().empty(), "F shows the list");
    require (list.rows().front().usable, "what works now comes first");
    bool seenLater = false;
    for (const auto& row : list.rows())
    {
        require (! (seenLater && row.usable), "usable rows are not mixed with the rest");
        seenLater = seenLater || ! row.usable;
       #if KIRIN_HYPHA_PRE_DISPLAY
        require (! row.label.contains ("A/B") && row.where != "A/B", "the list does not offer what this role does not have");
       #endif
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const auto row = std::find_if (list.rows().begin(), list.rows().end(), [] (const auto& shown) { return shown.label == "A/B"; });
    require (row != list.rows().end() && row->keys == "W" && row->where == "A/B", "the list offers A/B with W in POST");
   #endif
    rig.press ('V');
    rig.pressShift ('V');
    rig.pressShift ('2');
    require (view.hybridVuVisible() == vu && view.domain() == Domain::level, "keys do nothing behind the list");
    require (! rig.press (juce::KeyPress::spaceKey, 0, ' '), "Space still reaches the host under the list");
    rig.press (juce::KeyPress::escapeKey);
    require (! list.isVisible(), "Esc closes the list");
    require (before->hasKeyboardFocus (false), "focus returns to where it was");
    rig.editor->grabKeyboardFocus();
}
}

void verifyShortcutTable()
{
    std::set<std::string> seen;
    for (const auto& binding : table::bindings)
    {
        require (binding.key != ' ' && binding.key >= tabCode && binding.key < 0x80, "only plain keys are assigned");
        const auto key = std::to_string (static_cast<int> (binding.scope)) + "/" + std::to_string (binding.key)
            + (binding.shift ? "s" : "");
        require (seen.insert (key).second, "one scope gives a key one job: " + key);
        if (binding.pairsWithNext)
            require (std::string (binding.label) == (&binding + 1)->label, "a pair is two rows of one label");
    }
    for (const auto& control : table::controls)
        require (bindingFor (control.command) != nullptr, std::string ("a control without a key: ") + control.id);
    require (keyText (*bindingFor (Command::revealBlind)) == "Shift+E", "Shift is written out");
    require (markFor (*bindingFor (Command::previousVersion)) == "VERSION Shift+A / D", "a pair names both keys");
    require (markFor (*bindingFor (Command::startListen)) == "LISTEN Q", "the mark is the control and its key");

    const auto digit = [] (int code) { return normalise (juce::KeyPress (code, juce::ModifierKeys (shift), 0)).key; };
    require (digit ('!') == '1' && digit ('@') == '2' && digit ('"') == '2' && digit ('#') == '3'
             && digit ('$') == '4' && digit ('%') == '5' && digit ('1') == '1', "Shift+digit on every layout");
    require (normalise (juce::KeyPress ('@')).key == '@', "without Shift a symbol is not a digit");
    require (normalise (juce::KeyPress ('q')).key == 'Q', "lower case is the same key");

    Context normal; normal.post = true;
    require (resolve ({ '1', false }, normal).assigned && resolve ({ '1', false }, normal).binding == nullptr, "1 waits for LISTEN or REF");
    auto listen = normal; listen.listen = listen.reference = true;
    require (resolve ({ '1', false }, listen).binding->command == Command::listenPre, "LISTEN comes before REF");
    require (resolve ({ '3', false }, listen).binding->command == Command::referenceC, "a key LISTEN leaves free goes on down");
    auto blind = listen; blind.blind = true;
    require (resolve ({ '1', false }, blind).binding->command == Command::blindSourceOne, "BLIND comes first");
    require (resolve ({ '1', true }, blind).binding == nullptr && resolve ({ 'A', false }, blind).binding == nullptr
             && resolve ({ 'G', false }, blind).binding == nullptr && resolve ({ 'V', true }, blind).binding == nullptr, "BLIND shuts the rest");
    require (resolve ({ 'F', false }, blind).binding->command == Command::toggleKeys, "KEYS works in BLIND");
    auto pre = normal; pre.post = false;
    require (resolve ({ 'Q', false }, pre).binding == nullptr && juce::String (resolve ({ 'Q', false }, pre).refusal) == "Available in POST", "PRE says POST");
    require (resolve ({ 'V', false }, pre).binding->command == Command::toggleHybridVu, "PRE has its own keys");
    require (! resolve ({ 'W', false }, normal).assigned, "a role without A/B leaves W to the host");
    auto entry = normal; entry.candidateCompareAvailable = true;
    require (resolve ({ 'W', false }, entry).binding->command == Command::startCandidateCompare, "W opens A/B");
    require (resolve ({ '1', false }, entry).binding == nullptr && resolve ({ 'T', false }, entry).binding == nullptr,
             "before A/B runs, 1 and T are not its keys");
    auto running = entry; running.listen = running.candidateCompareActive = true;
    require (resolve ({ '1', false }, running).binding->command == Command::candidateA
             && resolve ({ '2', false }, running).binding->command == Command::candidateB
             && resolve ({ 'T', false }, running).binding->command == Command::endCandidateCompare,
             "1, 2 and T are A/B's while it runs, on any page");
    require (resolve ({ 'G', false }, running).binding->command == Command::matchListen, "A/B keeps LISTEN's MATCH");
    auto listening = entry; listening.listen = true;
    require (resolve ({ '1', false }, listening).binding->command == Command::listenPre, "LISTEN keeps its PRE");
    auto blindOverCandidate = running; blindOverCandidate.blind = true;
    require (resolve ({ '1', false }, blindOverCandidate).binding->command == Command::blindSourceOne
             && resolve ({ 'W', false }, blindOverCandidate).binding == nullptr, "a Blind over A/B names no side");
    const auto rows = listRows (running);
    const auto startRow = std::find_if (rows.begin(), rows.end(), [] (const auto& shown) { return shown.label == "A"; });
    require (startRow != rows.end() && startRow->usable && startRow->keys == "1" && startRow->where == "A/B", "the list names A's key");
    auto candidate = normal; candidate.candidateCompareAvailable = candidate.candidateCompareActive = true;
    candidate.reference = true;
    require (resolve ({ 'W', false }, candidate).binding->command == Command::startCandidateCompare
             && resolve ({ '2', false }, candidate).binding->command == Command::candidateB, "A/B needs only the two flags");
    for (const auto& row : listRows (blind))
        require (row.where == "BLIND" || row.where == "ALL" || row.where == "KEYS", "BLIND's list has only BLIND");
}

void verifyShortcutKeys()
{
    i18n::holdLanguage (true);
    ShortcutKeyRig rig;
    auto& view = rig.view();
    rig.editor->setSize (900, 600);  // every tab is a button of its own at this size
    view.onDomainChange (Domain::level);
    rig.editor->giveAwayKeyboardFocus();
    require (! rig.pressShift ('2') && view.domain() == Domain::level, "a window without focus takes no key");

    // The click: on a meter, which takes no focus of its own.
    rig.click (rig.editor->getLocalBounds().getCentre());
    require (rig.editor->hasKeyboardFocus (true), "a click in the window gives it the keys");
    require (rig.press ('@', shift, '@') && view.domain() == Domain::time, "Shift+2 as macOS sends it on a US layout");
    require (rig.press ('!', shift, '!') && view.domain() == Domain::level, "Shift+1");
    require (rig.press ('"', shift, '"') && view.domain() == Domain::time, "Shift+2 as macOS sends it on a JIS layout");
    require (rig.press ('!', shift, '!') && view.domain() == Domain::level, "Shift+1 again");
    require (rig.press ('2', shift, '@') && view.domain() == Domain::time, "Shift+2 as Windows sends it");
    require (rig.press ('$', shift, '$') && view.domain() == Domain::space, "Shift+4");
    rig.pressShift ('1');
    // A key reaches the window from a chosen control too.
    view.findChildWithID ("observatory-time")->grabKeyboardFocus();
    require (rig.press ('@', shift, '@') && view.domain() == Domain::time, "a focused control passes the key up");
    rig.pressShift ('1');
    rig.editor->grabKeyboardFocus();

    verifyKeysGoBackToTheHost (rig);
    verifyTextAndMenusComeFirst (rig);
    verifyOnePressOneAction (rig);
    verifyKeysList (rig);

    // A control does not keep the keys after a click, so Return stays the host's.
    auto* button = view.findChildWithID ("observatory-time");
    button->grabKeyboardFocus();
    rig.click (rig.editor->getLocalBounds().getCentre());
    require (rig.editor->hasKeyboardFocus (false) && ! button->hasKeyboardFocus (false), "the window keeps the keys, the control does not");
    require (! rig.press (juce::KeyPress::returnKey, 0, '\r'), "so Return is the host's again");
    rig.pressShift ('V');
    auto& dialog = rig.editor.get()->*shortcutMember (ShortcutResetDialog {});
    require (dialog != nullptr, "Shift+V opens the RESET question");
    rig.press ('V');
    require (dialog != nullptr && dialog->isCurrentlyModal(), "the question stays over the window");
    dialog->exitModalState (0);
    require (afterQueuedMessages ([] {}) && dialog == nullptr, "CANCEL closes it");
    rig.editor->grabKeyboardFocus();

   #if KIRIN_HYPHA_PRE_DISPLAY
    rig.press ('Q');
    require (rig.toast() == "Available in POST", "PRE says where LISTEN is");
    rig.clearToast();
    rig.pressShift ('3');
    require (view.domain() == Domain::level && rig.toast() == "Available in POST", "PRE has no FREQ");
   #else
    rig.press ('1');
    require (rig.toast() == "LISTEN is not active", "an assigned key says why not");
    rig.clearToast();
    rig.hold ('1'); rig.hold ('1'); rig.hold ('1');
    rig.up();
    require (rig.toast() == "LISTEN is not active", "still one notice");

    // TIME's pages, round and round, and DRUM only where it can be read.
    auto& pages = rig.editor.get()->*shortcutMember (ShortcutTimePages {});
    using Page = analysis_navigation::Page;
    rig.pressShift ('2');
    require (view.domain() == Domain::time && pages.page() == Page::meters, "TIME opens on HISTORY");
    rig.press ('C');
    require (pages.page() == Page::run, "C is the next page");
    rig.press ('X');
    require (pages.page() == Page::meters, "X is the page before");
    rig.press ('X');
    require (pages.page() == Page::absolute, "the pages go round");
    rig.pressShift ('1');
    rig.clearToast();
    rig.press ('C');
    require (rig.toast() == "Available in TIME", "a page key says where it works");

    rig.pressShift ('3');
    require (view.domain() == Domain::frequency, "Shift+3 is FREQ in POST");
    rig.press ('Z');
    require (view.preferredTarget() == observatory::ObservationTarget::absolute, "Z is the absolute reading");
    rig.pressShift ('1');
    rig.clearToast();
    verifyBlindKeys (rig);
   #endif
    i18n::holdLanguage (false);
}
}
