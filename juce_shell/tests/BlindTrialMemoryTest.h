#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaBlindResultImage.h"
#include "../src/HyphaCapturePngFile.h"
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
}
}
