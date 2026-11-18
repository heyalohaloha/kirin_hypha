#pragma once

#include <memory>
#include <optional>
#include "HyphaBlindPreferenceTrial.h"
#include "HyphaBlindDetailsCollector.h"

#include "HyphaBlindScreen.h"
#include "live_compare/LiveCompareProcessorState.h"

// The image and its sheet stay out of the editor's headers (HyphaBlindImageSheet.h).
namespace hypha::blind_image
{
struct Facts;
class Sheet;
}

namespace hypha::live_blind_ui
{
// GUESS (2026-11-11, optional, off by default): whether this trial asks "Which one is B?" and the
// answer given so far (0 before one is chosen). The answer lives on the screen only and is never stored.
struct Guess
{
    bool asked = false;
    int answer = 0;
};

// PRE/POST の LIVE BLIND。見た目と置き方は VERSION BLIND と共通（HyphaBlindScreen.h）で、ここは中身だけを作る。
blind_ui::Screen screenFor (const live_compare::LiveBlindStatus&, bool playing, float postActual, bool repeatingAudio = false,
                            Guess = {});
// The RESULT's own note after REVEAL (a source that stood in, an audio estimate), or nothing.
juce::String resultNote (const live_compare::LiveBlindStatus&);
// After REVEAL: the source that was B (POST in PRE/POST Blind).
inline int sourceOfB (const live_compare::BlindView& trial) noexcept { return trial.firstPre ? 2 : 1; }

class Component final : public blind_ui::ScreenComponent
{
public:
    Component();
    ~Component() override;
    void setState (const live_compare::LiveBlindStatus& next, bool hostPlaying, float postActual, bool repeatingAudio = false);
    const live_compare::LiveBlindStatus& state() const noexcept { return current; }

    // GUESS from MENU. Off, the screen is exactly the Blind it always was.
    void setGuessing (bool on);
    bool guessing() const noexcept { return guessOn; }
    bool setPreferenceRounds (int);
    bool canChangePreferenceRounds() const noexcept;
    std::function<void (int)> onPreferenceRoundsChanged;
    void observePreferenceDetails (const KirinReferenceLiveMetricFrame*, const KirinPerceptualBatch*,
                                   const KirinSpectrumBatch*, std::uint64_t deltaContinuity);
    int preferenceRounds() const noexcept { return rounds; }
    int activePreferenceRounds() const noexcept { return activeRounds; }
    const blind_preference::Trial& preferenceTrial() const noexcept { return preference; }
    blind_preference::Summary trialSummary() const noexcept { return preference.summary(); }
    // Caller supplies only currently revalidated approved gain, never estimated audio.
    void setPreferenceApprovedGains (std::uint64_t generation, std::optional<live_compare::GainSnapshot> gain);
    std::function<std::optional<bool> (int, std::uint64_t, std::uint32_t)> onPreferenceAnswer;
    std::function<bool (std::uint64_t, std::uint32_t)> onPreferenceNext;
    // Where GUESS and the names are remembered (ui-preferences.txt; tests use their own file).
    void setPreferenceFile (const juce::File& file) { preferences = file; }
    const juce::File& preferenceFile() const noexcept { return preferences; }

    // SAVE IMAGE: the RESULT as it is now, with the remembered names.
    blind_image::Facts resultFacts() const;
    void openImageSheet();
    bool imageSheetOpen() const noexcept;
    blind_image::Sheet& imageSheet() noexcept { return *sheet; }
    // Writes the sheet's image to `file`; a failure stays on the sheet with its reason.
    bool saveImageTo (const juce::File& file);
    juce::File imageSaveInitialDirectory() const;
    void closeImageSheet();
    void resized() override;

private:
    void show();
    void refreshPreference();
    void choosePreference (int);
    void resetPreference();
    void decoratePreference (blind_ui::Screen&) const;
    void retainPreferenceNotes();
    bool rememberNames();
    void chooseImageFile();

    blind_preference::Trial preference;
    blind_details::Collector detailsMeasurements;
    blind_details::Result measuredDetails;
    int rounds = 1, activeRounds = 0; // configured next trial / captured active trial
    bool preferenceBusy = false, awaitingEpoch = false, detailsOpen = false;
    std::uint32_t expectedEpoch = 0;
    bool preferenceFallback = false, preferenceTimingFallback = false, preferenceCeilingFallback = false;
    double preferenceContentNoticeMs = 0;
    bool approvedGainsValid = false;
    std::optional<live_compare::GainSnapshot> approvedGainAtStart;
    double approvedPreDb = 0, approvedPostDb = 0;
    std::uint64_t approvedGainGeneration = 0;
    live_compare::LiveBlindStatus current;
    bool playing = false, repeating = false, guessOn = false, namesRemembered = true;
    float actualPost = 1.0f;
    int answer = 0;
    std::uint64_t answerGeneration = 0;
    std::uint32_t answerEpoch = 0;
    juce::File preferences;
    juce::File lastImageFolder;
    std::unique_ptr<blind_image::Sheet> sheet;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
