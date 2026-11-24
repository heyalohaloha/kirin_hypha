#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaBlindImageSheet.h"
#include "../src/HyphaBlindResultImage.h"
#include "../src/HyphaCapturePngFile.h"
#include "../src/HyphaLanguage.h"
#include "TestPathBudget.h"
#include <memory>

// The window closes and opens again while the plugin lives: the RESULT, its chance line, the matched levels and
// the SAVE IMAGE facts come back from the plugin's memory; a new trial, END or a lost trial never gets them.
namespace hypha::tests::blind_trial_memory
{
struct Window
{
    live_blind_ui::Component view;
    Window (std::shared_ptr<live_blind_ui::TrialMemory> kept, live_compare::LiveBlindStatus& status, const juce::File& file, int rounds)
    {
        view.setPreferenceFile (file); view.setPreferenceRounds (rounds);
        view.setTrialMemory (std::move (kept)); // the editor's order: the next trial's rounds, then the kept trial
        view.onPreferenceAnswer = [&status] (int source, std::uint64_t generation, std::uint32_t epoch) -> std::optional<bool> {
            if (generation != status.generation || epoch != status.trial.epoch || status.trial.played != 3 || status.trial.revealed) return {};
            return (source == 1) == ((epoch % 3u) != 0);
        };
        view.onPreferenceNext = [this, &status] (std::uint64_t, std::uint32_t) {
            ++status.trial.epoch; status.trial.played = 0; show (status); return true;
        };
        view.setBounds (0, 0, 600, 400); view.setVisible (true);
        show (status);
    }
    void show (const live_compare::LiveBlindStatus& status) { view.setState (status, true, 1.0f); }
};

inline live_compare::LiveBlindStatus trial (bool candidate)
{
    live_compare::LiveBlindStatus status;
    status.stage = live_compare::BlindStage::active; status.generation = 41; status.candidate = candidate;
    status.trial.epoch = 9; status.trial.selected = 1; status.trial.firstPre = false;
    return status;
}

inline juce::Image sheet (live_blind_ui::Component& view) { return blind_image::render (view.resultFacts()); }

inline bool same (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); y += 3)
        for (int x = 0; x < a.getWidth(); x += 3)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return false;
    return true;
}

// The trial really ends while SAVE IMAGE is open: the sheet, its answers and the save window's late answer go
// together. A closed and reopened window, and a sheet opened again on the same RESULT, still save.
template <typename Require>
void verifyRevoked (Require require, const juce::File& root)
{
    using Status = live_compare::LiveBlindStatus;
    const auto folder = root.getChildFile ("revoked"); // the remembered names keep a lock file beside them
    const auto names = folder.getChildFile ("names"), target = folder.getChildFile ("result.png");
    const auto revealed = [&] (Window& window, Status& status, int rounds) {
        for (int i = 0; i < (rounds == 1 ? 1 : 2); ++i)
        {
            status.trial.played = 3; window.show (status);
            if (rounds > 1) window.view.onGuess (1);
        }
        status.trial.revealed = true; window.show (status);
        window.view.openImageSheet();
        require (window.view.imageSheetOpen() && window.view.trialSummary().total() == (rounds == 1 ? 0u : 2u), "SAVE IMAGE is open on the RESULT");
    };
    const auto lost = [&] (const char* what, auto&& change) {
        for (const int rounds : { 1, 5 })
        {
            const auto where = juce::String (what) + ", " + juce::String (rounds) + " rounds: ";
            const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
            auto status = trial (false);
            Window window (kept, status, names, rounds);
            revealed (window, status, rounds);
            const auto late = window.view.imageSave();
            change (status); window.show (status);
            require (! window.view.imageSheetOpen(), where + "its sheet closes");
            require (window.view.trialSummary().total() == 0 && window.view.screen().preferenceResultText.isEmpty()
                     && ! window.view.screen().saveImageShown, where + "the old RESULT is gone");
            require (! late (target) && ! window.view.saveImageTo (target) && ! target.exists(), where + "a late save writes nothing");
            require (window.view.actionNoticeShown() == "Image not saved", where + "and says so");
            window.view.openImageSheet();
            require (! window.view.imageSheetOpen() && ! late (target) && ! target.exists(), where + "nothing opens it again");
            status = trial (false); status.generation += 9; status.trial.epoch = 70;
            window.show (status); revealed (window, status, rounds);
            require (! late (target) && ! target.exists(), where + "a new RESULT never answers the old save");
        }
    };
    lost ("a lost pair or owner", [] (Status& status) { status.stage = live_compare::BlindStage::invalidated; status.trial.active = false; });
    lost ("a revoked trial", [] (Status& status) { status.stage = live_compare::BlindStage::invalidated; status.trial = {}; });
    lost ("a trial the audio ended", [] (Status& status) { status.trial.invalidated = true; });
    lost ("END", [] (Status& status) { status.stage = live_compare::BlindStage::finishing; ++status.generation; status.trial = {}; });
    for (const int rounds : { 1, 5 })
    {
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (false);
        auto window = std::make_unique<Window> (kept, status, names, rounds);
        revealed (*window, status, rounds);
        // The plugin forgets first; the window's next refresh has not come yet.
        const auto before = *kept;
        const auto late = window->view.imageSave();
        *kept = {};
        require (! late (target) && ! target.exists(), "a forgotten trial is never saved, also before the window refreshes");
        *kept = before;
        window->view.closeImageSheet();
        window->view.openImageSheet();
        require (! late (target) && ! target.exists(), "an earlier opening's answer never saves a later one");
        window.reset();
        window = std::make_unique<Window> (kept, status, names, rounds);
        window->view.openImageSheet();
        const auto again = window->view.imageSave();
        window->show (status); // the same RESULT, refreshed
        require (window->view.imageSheetOpen() && again (target) && target.existsAsFile() && target.deleteFile()
                 && ! window->view.imageSheetOpen(), "a reopened window still saves its RESULT");
    }
    require (! target.exists() && removeFixtureDirectory (folder), "the revoked fixture leaves no file");
}

// The gain receipt is still on its way when an answer is accepted: the answer stays, the next round starts once when
// the receipt stands, and nothing is answered or assigned twice. REVEAL, END and a lost trial ask for none.
template <typename Require>
void verifyNextAssignmentWaits (Require require, const juce::File& root)
{
    using Phase = blind_preference::Phase;
    const auto file = root.getChildFile ("blind-next-fixture");
    for (const bool candidate : { false, true }) for (const int rounds : { 5, 10 })
    {
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (candidate);
        unsigned asked = 0, granted = 0, refuse = 0;
        bool random = true;
        std::unique_ptr<Window> window;
        const auto open = [&] {
            window = std::make_unique<Window> (kept, status, file, rounds);
            window->view.onPreferenceNext = [&] (std::uint64_t generation, std::uint32_t epoch) {
                ++asked;
                require (generation == status.generation && epoch == status.trial.epoch && ! status.trial.revealed, "only the answered round asks");
                if (refuse > 0) { --refuse; window->show (status); return false; } // a refusal refreshes the window too
                if (! random) { status.stage = live_compare::BlindStage::invalidated; status.reason = live_compare::RecoveryReason::randomUnavailable;
                                window->show (status); return false; }
                ++granted; ++status.trial.epoch; status.trial.played = 0; window->show (status); return true;
            };
        };
        const auto answer = [&] (unsigned refusals) {
            status.trial.played = 3; window->show (status);
            refuse = refusals; window->view.onGuess (1);
        };
        open();
        answer (3);
        const auto epoch = status.trial.epoch;
        require (window->view.trialSummary().total() == 1 && asked == 1 && granted == 0
                 && window->view.preferenceTrial().phase() == Phase::awaitingAssignment && ! window->view.screen().guessOpen,
                 "the accepted answer stays while its receipt is on its way");
        window->view.onGuess (2); window->view.onGuess (1);
        require (window->view.trialSummary().total() == 1 && asked == 1, "the waiting round takes no second answer");
        window->show (status); window->show (status);
        require (asked == 3 && granted == 0 && window->view.trialSummary().total() == 1, "each refresh asks once");
        window->show (status);
        require (asked == 4 && granted == 1 && status.trial.epoch == epoch + 1 && window->view.trialSummary().total() == 1
                 && window->view.preferenceTrial().phase() == Phase::listening && window->view.preferenceTrial().roundNumber() == 2,
                 "the standing receipt starts the next round once");
        for (int again = 0; again < 4; ++again) window->show (status);
        require (asked == 4 && granted == 1, "nothing is assigned twice");
        answer (0);
        require (asked == 5 && granted == 2 && window->view.trialSummary().total() == 2, "an ordinary answer moves at once");
        // The window closes while the round waits: the reopened one goes on with it.
        answer (1);
        require (granted == 2 && window->view.trialSummary().total() == 3, "the third answer waits");
        const auto waiting = status.trial.epoch;
        open();
        window->show (status);
        require (status.trial.epoch == waiting + 1 && window->view.trialSummary().total() == 3
                 && window->view.preferenceTrial().phase() == Phase::listening, "a reopened window starts the waiting round once");
        // REVEAL while waiting: the answers so far, and no further assignment.
        answer (100);
        const auto before = asked;
        status.trial.revealed = true; window->show (status); window->show (status);
        require (asked == before && window->view.preferenceTrial().phase() == Phase::revealed && window->view.trialSummary().total() == 4,
                 "REVEAL takes the answers so far and asks for nothing");
        // A real loss while waiting clears the answers, and nothing is assigned afterwards.
        status = trial (candidate); status.generation += 5; status.trial.epoch = 90; window->show (status);
        answer (100);
        const auto lost = asked;
        status.stage = live_compare::BlindStage::invalidated; window->show (status); window->show (status);
        require (asked == lost && window->view.trialSummary().total() == 0, "a lost trial clears the answers and asks for nothing");
        // Randomness fails at the waiting request: it says so and ends.
        status = trial (candidate); status.generation += 6; status.trial.epoch = 120; window->show (status);
        answer (1); random = false;
        const auto failing = asked;
        window->show (status); window->show (status);
        require (asked == failing + 1 && window->view.trialSummary().total() == 0
                 && window->view.actionNoticeShown() == "Random assignment was unavailable", "failed randomness ends the waiting round once");
    }
    require (! file.exists(), "waiting writes no file");
}

// RESULT and its picture take the estimate of the answers: one first measured after the last answer is not theirs.
template <typename Require>
void verifyEstimateFixedAtAnswer (Require require, const juce::File& root)
{
    const auto file = root.getChildFile ("blind-estimate-fixture");
    const auto told = [&] (double atAnswer, double afterAnswer, int answers, const juce::String& expected, const char* why) {
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (false);
        auto window = std::make_unique<Window> (kept, status, file, answers == 0 ? 1 : 5);
        for (int i = 0; i < answers; ++i)
        {
            status.trial.played = 3;
            if (i == answers - 1) status.contentNoticeMs = atAnswer;
            window->show (status); window->view.onGuess (1);
        }
        status.contentNoticeMs = afterAnswer; status.trial.played = 3; window->show (status);
        status.trial.revealed = true; window->show (status);
        for (int again = 0; again < 2; ++again)
        {
            require (window->view.resultFacts().note == expected && window->view.screen().instruction.contains ("Audio estimate") == expected.isNotEmpty()
                     && (expected.isEmpty() || window->view.screen().instruction == expected), juce::String (why) + ": " + window->view.resultFacts().note);
            window = std::make_unique<Window> (kept, status, file, 5); // closed and opened again
        }
    };
    const auto estimate = [] (const char* ms) { return "Audio estimate: PRE may have been " + juce::String (ms) + " ms off"; };
    told (0.0, 1.25, 5, {}, "an estimate after the last answer stays out of RESULT");
    told (0.8, 3.5, 5, estimate ("0.80"), "the estimate at the last answer is kept");
    told (0.8, 0.8, 5, estimate ("0.80"), "an unchanged estimate is told once");
    told (0.0, 1.25, 2, estimate ("1.25"), "an unanswered round tells its own estimate at REVEAL");
    told (0.0, 1.25, 0, estimate ("1.25"), "one listen tells its estimate at REVEAL");
    require (! file.exists(), "an estimate writes no file");
}

// The DAW stays stopped on the RESULT: why and how to go on stand beside the RESULT's own note, small and large.
template <typename Require>
void verifyStoppedResultKeepsNote (Require require, const juce::File& root)
{
    const auto file = root.getChildFile ("blind-stopped-fixture");
    const juce::String note = "POST was used briefly when timing was unavailable";
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const bool stoodIn : { true, false }) for (const int rounds : { 1, 5 })
    {
        const i18n::ScopedLanguage shown (language);
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (false);
        live_compare::GainSnapshot gain;
        gain.coherent = true; gain.retained = true; gain.revision = 2; gain.pre = 1.2f; gain.post = 0.8f; gain.ceiling = 0.9f;
        gain.identity = { 1, 2, 3, 4, 48000 };
        Window window (kept, status, file, rounds);
        window.view.setPreferenceApprovedGains (status.generation, gain);
        status.trial.played = 3;
        status.trial.usedPostFallback = status.trial.fallbackForTiming = stoodIn;
        window.show (status);
        if (rounds > 1) window.view.onGuess (1);
        status.trial.revealed = true; window.show (status);
        window.view.setPreferenceApprovedGains (status.generation, gain);
        const auto playing = window.view.screen();
        const auto facts = window.view.resultFacts().note;
        require (facts == (stoodIn ? note : juce::String()), "the RESULT's own note");
        for (const int width : { 300, 450, 900 })
        {
            window.view.setSize (width, width * 2 / 3);
            const auto where = juce::String (width) + (stoodIn ? " with" : " without") + " a substitute, " + juce::String (rounds) + " rounds: ";
            const auto told = [&] (bool stopped) {
                auto* body = dynamic_cast<juce::TextEditor*> (window.view.findChildWithID ("live-blind-preference-note"));
                require (body != nullptr && body->isVisible() && ! body->getBounds().isEmpty() && window.view.getLocalBounds().contains (body->getBounds()),
                         where + "the notes have their own place");
                if (body == nullptr) return;
                for (auto* sibling : window.view.getChildren())
                    require (sibling == body || ! sibling->isVisible() || ! sibling->getBounds().intersects (body->getBounds()),
                             where + "no overlap with " + sibling->getComponentID());
                require (body->getText().contains (i18n::tr (note)) == stoodIn, where + "the note is on the screen");
                const auto& guided = window.view.screen();
                require (! stopped || (guided.guidanceShown && body->getText().contains (i18n::tr (guided.recovery))),
                         where + "the stopped guidance is beside it");
            };
            told (false);
            auto stopped = status; stopped.prolongedWait = true; stopped.reason = stopped.observation = live_compare::RecoveryReason::stopped;
            window.show (stopped); told (true);
            require (window.view.resultFacts().note == facts, where + "SAVE IMAGE keeps the same note");
            window.show (status); told (false);
            require (window.view.screen() == playing, where + "playing again restores the RESULT");
        }
    }
    require (! file.exists(), "a stopped RESULT writes no file");
}

template <typename Require>
void verify (Require require, const juce::File& root)
{
    const auto file = root.getChildFile ("blind-memory-fixture");
    for (const bool candidate : { false, true }) for (const int rounds : { 1, 5, 10 })
    {
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (candidate);
        live_compare::GainSnapshot gain;
        gain.coherent = true; gain.retained = true; gain.revision = 2; gain.pre = 1.2f; gain.post = 0.8f; gain.ceiling = 0.9f;
        gain.identity = { 1, 2, 3, 4, 48000 };
        const auto where = juce::String (candidate ? "A/B " : "PRE/POST ") + juce::String (rounds) + " rounds: ";
        auto window = std::make_unique<Window> (kept, status, file, rounds);
        window->view.setPreferenceApprovedGains (status.generation, gain);
        const auto answers = rounds == 1 ? 0 : rounds == 10 ? 10 : 3;
        for (int i = 0; i < answers; ++i)
        {
            status.trial.played = 3; window->show (status);
            window->view.onGuess (i % 4 == 0 ? 2 : 1);
            require (window->view.trialSummary().total() == static_cast<unsigned> (i + 1), where + "one vote per round");
            if (i == 1)
            {
                // Closed and opened in the middle: the answers stay, and nothing tells a side.
                const auto before = window->view.screen();
                window = std::make_unique<Window> (kept, status, file, rounds);
                window->view.setPreferenceApprovedGains (status.generation, gain);
                require (window->view.trialSummary().total() == 2 && window->view.screen() == before, where + "a reopened trial continues");
                require (window->view.screen().preferenceResultText.isEmpty() && ! window->view.screen().saveImageShown
                         && window->view.screen().sourceOne == "SOURCE 1" && window->view.screen().sourceTwo == "SOURCE 2"
                         && ! window->view.screen().matchedGainText.isNotEmpty(), where + "no answer or side shows before REVEAL");
            }
        }
        if (rounds == 1) { status.trial.played = 3; window->show (status); }
        status.trial.revealed = true; window->show (status);
        window->view.setPreferenceApprovedGains (status.generation, gain);
        const auto result = window->view.screen();
        const auto image = sheet (window->view);
        require (rounds == 1 || result.preferenceResultText.isNotEmpty(), where + "the RESULT tells the answers");
        require (result.saveImageShown && result.matchedGainText.isNotEmpty(), where + "SAVE IMAGE and the matched level are offered");
        // The window closes on the RESULT and opens again, twice.
        for (int again = 0; again < 2; ++again)
        {
            window.reset();
            window = std::make_unique<Window> (kept, status, file, rounds);
            window->view.setPreferenceApprovedGains (status.generation, gain);
            require (window->view.screen() == result, where + "the reopened RESULT is the same");
            require (window->view.resultFacts().preference.total() == static_cast<unsigned> (answers), where + "SAVE IMAGE keeps the count");
            require (same (image, sheet (window->view)), where + "SAVE IMAGE draws the same picture");
        }
        // A reopened RESULT neither changes the next trial's rounds nor survives END.
        require (window->view.preferenceRounds() == rounds, where + "the next trial keeps its rounds");
        auto ended = status; ended.stage = live_compare::BlindStage::idle; ended.trial = {};
        window->show (ended);
        require (kept->activeRounds == 0 && window->view.trialSummary().total() == 0, where + "END clears the memory");
        auto next = trial (candidate); next.generation = 42; next.trial.epoch = 70;
        window->show (next);
        require (window->view.activePreferenceRounds() == rounds && window->view.trialSummary().total() == 0, where + "a new trial starts empty");
    }
    // While the window is closed the trial is lost or replaced: its answers never reach another trial.
    const auto lost = [&] (const char* what, auto&& change) {
        const auto kept = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (false);
        {
            Window window (kept, status, file, 5);
            for (int i = 0; i < 2; ++i) { status.trial.played = 3; window.show (status); window.view.onGuess (1); }
            status.trial.revealed = true; window.show (status);
            require (window.view.trialSummary().total() == 2, "the trial held two answers");
        }
        change (status);
        Window window (kept, status, file, 5);
        require (window.view.trialSummary().total() == 0 && window.view.screen().preferenceResultText.isEmpty(), juce::String (what) + " takes no earlier answer");
        require (window.view.resultFacts().preference.total() == 0, juce::String (what) + " gives SAVE IMAGE no earlier answer");
    };
    using Status = live_compare::LiveBlindStatus;
    lost ("a new unrevealed trial", [] (Status& status) { ++status.generation; ++status.trial.epoch; status.trial.revealed = false; status.trial.played = 0; });
    lost ("a new revealed trial", [] (Status& status) { ++status.generation; ++status.trial.epoch; });
    lost ("another assignment", [] (Status& status) { status.trial.epoch += 3; });
    lost ("a changed pair or a restored project", [] (Status& status) { status.stage = live_compare::BlindStage::invalidated; status.trial = {}; });
    lost ("END", [] (Status& status) { status.stage = live_compare::BlindStage::idle; status.trial = {}; });
    lost ("an invalidated trial", [] (Status& status) { status.trial.invalidated = true; });
    // Another instance of the plugin has its own memory.
    {
        const auto first = std::make_shared<live_blind_ui::TrialMemory>(), second = std::make_shared<live_blind_ui::TrialMemory>();
        auto status = trial (false);
        Window a (first, status, file, 5); status.trial.played = 3; a.show (status); a.view.onGuess (1);
        auto other = trial (false);
        Window b (second, other, file, 5);
        require (a.view.trialSummary().total() == 1 && b.view.trialSummary().total() == 0, "instances never share answers");
    }
    require (! file.exists(), "the kept trial writes no file");
    verifyRevoked (require, root);
    verifyNextAssignmentWaits (require, root);
    verifyEstimateFixedAtAnswer (require, root);
    verifyStoppedResultKeepsNote (require, root);
}
}
