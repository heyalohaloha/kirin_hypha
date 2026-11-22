#pragma once

#include <functional>

#include "HyphaTextButton.h"
#include "HyphaTextLookAndFeel.h"
#include "HyphaBlindNoticeStack.h"

// PRE/POST の LIVE BLIND と REF の VERSION BLIND が同じ画面で出す（2026-10-04。形式と見た目を
// そろえて、覚えることを減らす）。窓全体に、上から題・指示・大きな SOURCE 1｜SOURCE 2・開示｜終了・説明。
// 開示すると題が BLIND RESULT になり、ボタンが「1: PRE」「2: POST」（VERSION は「1: A」「2: V」）に変わって、
// そのまま切り替えられる。中身（Screen）はそれぞれの Blind が作り、見た目と置き方はここだけが決める。
namespace hypha::blind_ui
{
struct Screen
{
    juce::String title, instruction, detail, endExplanation, trialNotice;
    bool guidanceShown = false;  // 止まった・待っている理由（cause）と直し方（recovery）の 2 行
    juce::String cause, recovery;
    // Guidance the host can raise without a click (the DAW stopped). The trial keeps its rows free so
    // END is where it was when the guidance appears, and a press meant for END lands on END.
    juce::String reservedCause, reservedRecovery;
    bool sourcesShown = false, sourceOneEnabled = false, sourceTwoEnabled = false;
    int audible = 0;  // 1・2：鳴っているソース（そのボタンを点ける）
    juce::String sourceOne { "SOURCE 1" }, sourceTwo { "SOURCE 2" };
    juce::String sourceReason;  // why a SOURCE cannot be chosen now (a click on it says so)
    bool revealShown = false, revealEnabled = false;
    bool alignOffShown = false;
    juce::String revealTitle { "Reveal the sources without an answer" };
    bool approveShown = false;
    juce::String approveText { "LOWER POST" }, approveTitle, approveDescription;
    bool endEnabled = true;
    juce::String endTitle;
    // GUESS (optional, MENU): above REVEAL the question and the two answers, the same for both
    // assignments; after REVEAL the chosen answer stays lit beside its outcome.
    bool guessShown = false, guessOpen = false, guessRight = false, votingComplete = false;
    int guessAnswer = 0;
    juce::String guessQuestion, guessOutcome;
    bool roundsShown = false, roundsEnabled = false, guessQuestionFixed = false;
    int roundsValue = 1;
    juce::String roundsProgress;
    juce::String matchedGainText, preferenceResultText, preferenceResultNote;
    bool saveImageShown = false;  // SAVE IMAGE on the RESULT, where REVEAL stood

    bool operator== (const Screen&) const noexcept;
    bool operator!= (const Screen& other) const noexcept { return ! (*this == other); }
};

class ScreenComponent : public juce::Component, private juce::Timer
{
public:
    // 部品の ID は「<prefix>-screen」「<prefix>-source-1」…（LIVE BLIND は live-blind、VERSION BLIND は version-blind）。
    // The frozen export model has already localized its note fields.
    enum class NoteLanguage { translate, prelocalized };
    explicit ScreenComponent (const juce::String& idPrefix, NoteLanguage = NoteLanguage::translate);
    ~ScreenComponent() override;
    std::function<void(int)> onSelect;
    std::function<void()> onReveal, onEnd, onApprove, onAlignOff, onSaveImage;
    std::function<void(int)> onGuess, onRounds;
    bool requestSource (int);
    bool requestReveal();
    bool requestEnd();
    const juce::String& sourceReason() const noexcept { return shown.sourceReason; }
    // Why a key did nothing, in place of the instruction for a moment. Never a name or a side.
    void setActionNotice (const juce::String&);
    const juce::String& actionNoticeShown() const noexcept { return actionNotice; }
    void setScreen (const Screen&);
    // A sheet over the whole screen (SAVE IMAGE) hides the screen's own text and controls meanwhile.
    void setCovered (bool);
    const Screen& screen() const noexcept { return shown; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { if (!isVisible()) moreNotices.dismiss(); }

private:
    void timerCallback() override;
    void apply();
    void layoutTrial (bool choosingRounds, bool fixedProgress);
    juce::String extraTrialNotice() const { return shown.votingComplete ? actionNotice : juce::String(); }
    bool isRoutineTrialHint() const
    {
        return status.getText() == "Try both sources while playing"
            || status.getText() == "Reveal the sources when ready"
            || status.getText() == "Sources revealed; keep comparing";
    }
    juce::String actionNotice;
    bool covered = false;
    struct ScreenLook : TextLookAndFeel
    {
        presentation::Context wrappedContext = presentation::defaultContext();
        const juce::Label* fixedQuestion = nullptr;
        const juce::Label *wrappedDetail = nullptr, *wrappedNotice = nullptr, *fixedStatus = nullptr;
        text_style::LabelPolicy policyFor (const juce::Label& label) const noexcept
        {
            return fixedStatus != nullptr && (&label == fixedStatus || &label == wrappedDetail)
                ? text_style::LabelPolicy::fixed : text_style::LabelPolicy::localized;
        }
        void drawLabel (juce::Graphics& g, juce::Label& label) override
        {
            if (&label == fixedQuestion) juce::LookAndFeel_V4::drawLabel (g, label);
            else if (&label == fixedStatus) drawLabelWithPolicy (g, label, policyFor (label));
            else if (&label == wrappedDetail || &label == wrappedNotice)
            {
                g.setColour (label.findColour (juce::Label::textColourId));
                g.setFont (labelFont (wrappedContext, typography::TextRole::compactExplanation, typography::Composition::information));
                text_style::drawLines (g, label.getText(),
                    label.getBorderSize().subtractedFrom (label.getLocalBounds()), label.getJustificationType(), 2, policyFor (label));
            }
            else TextLookAndFeel::drawLabel (g, label);
        }
    } look;
    presentation::Context context = presentation::defaultContext();
    Screen shown;
    unsigned languageRevision = 0;
    juce::Label title, status, detail, cause, recovery, question, outcome, notice;
    HyphaTextButton one { "SOURCE 1", true, text_style::LabelPolicy::fixed }, two { "SOURCE 2", true, text_style::LabelPolicy::fixed };
    HyphaTextButton reveal { "REVEAL", true, text_style::LabelPolicy::fixed }, alignOff { "ALIGN OFF", true, text_style::LabelPolicy::fixed };
    HyphaTextButton end { "END", true, text_style::LabelPolicy::fixed }, approve { "LOWER POST", true, text_style::LabelPolicy::fixed };
    HyphaTextButton guessOne { "1", true, text_style::LabelPolicy::fixed }, guessTwo { "2", true, text_style::LabelPolicy::fixed };
    HyphaTextButton saveImage { "SAVE IMAGE", true, text_style::LabelPolicy::fixed };
    juce::Label roundsLabel;
    notice_stack::More moreNotices;
    HyphaTextButton roundsOne { "1", true, text_style::LabelPolicy::fixed }, roundsFive { "5", true, text_style::LabelPolicy::fixed }, roundsTen { "10", true, text_style::LabelPolicy::fixed };
    juce::TextEditor matchedGainBody, preferenceResultBody, preferenceNoteBody;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScreenComponent)
};
}
