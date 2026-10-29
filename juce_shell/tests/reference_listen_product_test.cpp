// LISTEN while REF plays B (2026-10-18). The real PRE, POST and shipping editor, a Kirin OS Reference
// library in a test home (never the user's Kirin OS), and four sources told apart by their sound: PRE
// is a 542 Hz tone, POST the same tone turned over and 6 dB down (the chain), B a 1500 Hz tone. Each
// block's actual output is named from its samples and compared with what the user asked for and what
// the editor shows. B keeps the output while REF plays it (INV-S53); LISTEN keeps the user's choice on
// screen and says nothing for a short wait (INV-LC21); past 3 s it says what plays and how to return,
// and A in REF hands the output back to LISTEN's choice.
#include "ReferenceOneSongLibrary.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveCompareNoticeProductChecks.h"
#include "EditorEntryReachability.h"
#include "ReferenceAmbiguousEditorTest.h"
#include "../src/HyphaReferenceAuditionRecoveryText.h"
#include <map>
#if JUCE_MAC
void initialiseBlindProductHostApplication();
#endif

namespace hypha::tests::editor_product
{
namespace
{
enum class Heard { silence, pre, post, b, mixed };

const char* nameOf (Heard heard)
{
    switch (heard)
    {
        case Heard::silence: return "silence";
        case Heard::pre: return "PRE";
        case Heard::post: return "POST";
        case Heard::b: return "B";
        case Heard::mixed: return "mixed";
    }
    return "?";
}

// What the block that just played sounds like. PRE's tone correlates +1 with the rig's input, POST's
// -1; B's 1500 Hz tone makes exactly 16 cycles in 512 samples, so it alone fills that frequency.
Heard heard (const NoticeRig& r)
{
    const auto start = r.clock.position - 512;
    double input = 0.0, output = 0.0, cross = 0.0, sine = 0.0, cosine = 0.0;
    for (int n = 0; n < 512; ++n)
    {
        const double value = 0.05f * std::sin (float (start + n) * 0.071f);
        const double out = r.postAudio.getSample (0, n);
        input += value * value; output += out * out; cross += value * out;
        const double phase = 6.283185307179586 * 1500.0 * n / 48000.0;
        sine += out * std::sin (phase); cosine += out * std::cos (phase);
    }
    if (output < 512 * 1.0e-8) return Heard::silence;
    if ((sine * sine + cosine * cosine) * 2.0 / (512.0 * output) > 0.9) return Heard::b;
    const auto correlation = cross / std::sqrt (input * output);
    return correlation > 0.98 ? Heard::pre : correlation < -0.98 ? Heard::post : Heard::mixed;
}

struct ListenWithReference
{
    NoticeRig r;
    juce::Button& preButton() { return *dynamic_cast<juce::Button*> (find (*r.editor, "observatory-live-pre")); }
    // LISTEN's MENU section, every line (headers and items). It never offers PIN 4 S (1.1.52).
    juce::String menuHeaders()
    {
        juce::PopupMenu menu;
        (r.editor.get()->*privateMember (NoticeAddMenu {})) (menu, false);
        juce::String lines;
        for (juce::PopupMenu::MenuItemIterator item (menu); item.next();)
            lines << item.getItem().text << '\n';
        require (! lines.contains ("PIN"), "LISTEN's MENU offers no PIN 4 S");
        return lines;
    }
    // Plays for `seconds` at the audio rate (sleeping only while ahead) and counts what each block sounds like.
    std::map<Heard, int> play (double seconds, Heard* last = nullptr)
    {
        std::map<Heard, int> counts;
        const auto period = std::chrono::nanoseconds (512LL * 1'000'000'000LL / 48000);
        auto due = std::chrono::steady_clock::now();
        for (int i = 0; i < static_cast<int> (std::ceil (seconds * 48000 / 512)); ++i)
        {
            r.block();
            const auto now = heard (r);
            ++counts[now];
            if (last != nullptr) *last = now;
            due += period;
            if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
        }
        return counts;
    }
    bool onlyB (const std::map<Heard, int>& counts)
    {
        for (const auto& [kind, count] : counts)
            if (kind != Heard::b && count > 0)
            {
                std::cerr << "heard " << nameOf (kind) << " in " << count << " blocks while B holds the output\n";
                return false;
            }
        return true;
    }
    // A in REF: B fades back to A, then LISTEN's choice returns by itself once PRE is confirmed.
    void returnTo (Heard wanted)
    {
        r.post->selectReferenceA();
        Heard last = Heard::silence;
        for (int n = 0; n < 60 && last != wanted; ++n) play (0.1, &last);
        const auto settled = play (0.5, &last);
        if (last != wanted || r.post->referenceOutputSlot() != 0)
        {
            const auto status = r.post->liveCompareStatus();
            std::cerr << "after A: last " << nameOf (last) << ", slot " << r.post->referenceOutputSlot() << ", active "
                      << status.active << ", pre selected " << status.preSelected << ", audible " << status.preAudible
                      << ", observation " << static_cast<int> (status.observation) << ", reason " << static_cast<int> (status.reason)
                      << ", heard";
            for (const auto& [kind, count] : settled) std::cerr << ' ' << nameOf (kind) << '=' << count;
            std::cerr << '\n';
        }
        require (last == wanted && r.post->referenceOutputSlot() == 0, "A in REF hands the output back to LISTEN's choice");
        require (! r.view().feedback().contains ("plays from REF") && menuHeaders().indexOf ("plays from REF") < 0,
                 "the notice leaves once LISTEN's choice plays again");
    }
    void hold (bool preChosen)
    {
        const auto shortWait = play (1.5);
        require (onlyB (shortWait), "B keeps the output; PRE and POST never leak into it");
        require (r.post->referenceOutputSlot() == 3, "the Audio Thread reports B as what it played");
        require (preButton().getToggleState() == preChosen, "the footer keeps the user's choice during a short wait");
        require (! r.view().feedback().contains ("plays from REF") && menuHeaders().indexOf ("plays from REF") < 0,
                 "a wait under 3 s says nothing (INV-LC21)");
        require (onlyB (play (2.0)), "B still holds the output past 3 s");
        const juce::String notice = "B plays from REF: press A there to listen";
        require (preButton().getToggleState() == preChosen, "the choice stays on screen past 3 s");
        require (r.view().feedback() == notice && preButton().getTooltip() == notice
                     && menuHeaders().contains (notice),
                 "past 3 s the footer, the PRE control and MENU say what plays and how to return");
        const auto story = r.editor->statusStory().joinIntoString (" ");
        require (story.contains (preChosen ? "B plays now. Press A in REF to hear PRE."
                                           : "B plays now. Press A in REF to hear POST."),
                 "the status window tells what plays now and how to hear the choice");
        matrix ("LISTEN beside REF", { observatory::Domain::level, observatory::Domain::reference });
    }
    // While this state runs: every control at every size, display scale and language on these pages, and no
    // PIN 4 S in the editor or any MENU level (LISTEN's own section included).
    void matrix (const char* what, std::initializer_list<observatory::Domain> pages)
    {
        const std::vector<observatory::Domain> shownPages (pages);
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage shown (language);
            (r.editor.get()->*privateMember (SyncLanguage {})) (true);
            checkEveryCell (*r.editor, juce::String (what) + (language == i18n::Language::japanese ? " ja" : " en"), shownPages);
        }
        (r.editor.get()->*privateMember (SyncLanguage {})) (true);
    }
};
}

void verifyListenWhileReferencePlays (const juce::File& sandbox)
{
    ScopedChainTimingStorage preferences (sandbox);
    const i18n::ScopedLanguage english (i18n::Language::english);
    // The pure presentation: nothing while LISTEN is off, ends or waits under 3 s, nothing without an audition.
    for (int slot = 0; slot <= 3; ++slot)
        for (const bool prolonged : { false, true })
        {
            live_compare::Status state;
            state.active = state.preSelected = true;
            state.prolongedWait = prolonged;
            const juce::String text = live_compare_ui::currentListenPresentation (state, live_compare::StartResult::started, slot).instruction;
            require (text.isNotEmpty() == (prolonged && slot != 0), "only a held audition past 3 s is named");
            state.finishing = true;
            require (! juce::String (live_compare_ui::currentListenPresentation (state, live_compare::StartResult::started, slot)
                                         .instruction).contains ("plays from REF"), "END's return is not an audition notice");
        }
    for (const auto* text : { "B plays from REF: press A there to listen", "B plays now. Press A in REF to hear PRE." })
        require (i18n::translate (text, i18n::Language::japanese) != text, "the notice reads in Japanese too");

    ListenWithReference test;
    auto& r = test.r;
    r.chainGain = -0.5f;
    r.expectOrdinaryAudio = false;
    r.post->refreshLicenseForUserAction();
    require (r.post->licenseIsOs(), "the test home grants the Kirin OS license");
    r.selectPair();
    for (int n = 0; n < 300 && ! r.post->referenceAuditionSnapshot().referenceReady; ++n) test.play (0.05);
    require (r.post->referenceAuditionSnapshot().referenceReady, "B's song is prepared");
    test.play (4.0);  // A's level window for B's MATCH
    require (test.play (0.2).count (Heard::post) > 0, "POST plays before anything is chosen");
    require (r.post->selectReferenceRef(), "B plays");
    Heard last = Heard::silence;
    for (int n = 0; n < 60 && last != Heard::b; ++n) test.play (0.1, &last);
    require (last == Heard::b, "B is heard");

    // LISTEN may start while B plays; choosing PRE does not take the output from B.
    require (r.post->startLiveCompare() == live_compare::StartResult::started, "LISTEN starts while B plays");
    r.post->selectLiveComparePre (true);
    test.hold (true);
    test.returnTo (Heard::pre);

    // The same with POST chosen, B pressed while LISTEN runs.
    r.post->selectLiveComparePre (false);
    test.play (0.5, &last);
    require (last == Heard::post, "POST is chosen and heard");
    require (r.post->selectReferenceRef(), "B plays again");
    for (int n = 0; n < 60 && last != Heard::b; ++n) test.play (0.1, &last);
    require (last == Heard::b && r.post->liveCompareStatus().active, "B takes the output; LISTEN stays on");
    test.hold (false);
    test.returnTo (Heard::post);
    // BLIND from the running LISTEN: its screen and every MENU level offer no PIN 4 S either.
    require (r.post->beginLiveBlind() == live_compare::StartResult::started, "BLIND starts from LISTEN");
    for (int n = 0; n < 80 && r.post->liveBlindStatus().stage != live_compare::BlindStage::active; ++n) test.play (0.1);
    require (r.post->liveBlindStatus().stage == live_compare::BlindStage::active, "BLIND runs");
    for (int n = 0; n < 5; ++n) test.play (0.1);  // the editor's refresh opens the BLIND screen
    test.matrix ("BLIND", { observatory::Domain::level });
    r.post->finishLiveCompare(); test.play (0.5);
    require (r.pre->getLatencySamples() == 0 && r.post->getLatencySamples() == 0, "no added latency");
}
}

int main()
{
    ReferenceValidationStorageSandbox sandbox;
    const auto& box = sandbox.directory();
    // macOS JUCE resolves the home without HOME; keep PRE display files out of the real Kirin OS.
    hypha::pre_display::Controller::placeUnderForTest (box);
    // Rust's storage (identity, plugin_data) follows HOME, which the sandbox moved into the test folder.
    const auto* homeValue = std::getenv ("HOME");
    require (homeValue != nullptr, "the sandbox sets HOME");
    const auto identity = juce::File (juce::String::fromUTF8 (homeValue))
                              .getChildFile ("Library/Application Support/Kirin OS/identity.json");
    // JUCE chooses the Reference folder and does not follow HOME on macOS: point it into the test folder.
    const auto reference = box.getChildFile ("reference-v2");
    ref::RuntimeV2Repository::setTransportRootForTesting (reference);
    // Before writing, every target is inside the test folder (never the real Kirin OS storage).
    require (box.isDirectory() && identity.isAChildOf (box) && reference.isAChildOf (box)
                 && ref::RuntimeV2Repository::transportRoot() == reference,
             "every write stays inside the sandbox");
    require (identity.getParentDirectory().createDirectory().wasOk() && hypha::storage::writeText (identity, "{\"license\":\"os\"}"),
             "sandbox Kirin OS identity");
    // B: a 1500 Hz tone whose Kirin OS level is close to A's, so its MATCH fits under the ceiling.
    OneSongLibrary song;
    song.frequencyHz = 1500.0; song.integratedLufs = -26.7; song.truePeakDbtp = -26.0;
    song.fileName = "b-tone.wav"; song.displayName = "B tone";
    writeOneSongLibrary (reference, song);
   #if JUCE_MAC
    initialiseBlindProductHostApplication();
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    hypha::i18n::holdLanguage (true);
    hypha::tests::editor_product::verifyListenWhileReferencePlays (box);
    hypha::tests::editor_product::verifyAmbiguousVersionFromEditor (box);
    std::cout << "Reference listen product: PASS (B holds the output, LISTEN's choice kept, notice past 3 s, A returns)\n";
    return EXIT_SUCCESS;
}
