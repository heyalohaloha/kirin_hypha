#include "HyphaLiveBlindComponent.h"
#include "HyphaBlindImageSheet.h"
#include "HyphaCapturePngFile.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaPairingLoopText.h"
#include <cmath>

namespace hypha::live_blind_ui
{
using Stage = live_compare::BlindStage;

juce::String resultNote (const live_compare::LiveBlindStatus& current)
{
    juce::String note;
    if (current.trial.usedPostFallback)
        note = current.candidate ? "Chain audio was used briefly"
             : ! current.trial.fallbackForCeiling ? "POST was used briefly when timing was unavailable"
             : ! current.trial.fallbackForTiming ? "POST played where PRE went over the ceiling"
             : "POST played for timing and the PRE ceiling";
    // INV-LC10: PRE kept its clock position; after REVEAL the estimate is told as a possibility, after
    // any POST that really stood in (both facts, that one first).
    if (current.contentNoticeMs != 0.0)
        note = (current.trial.usedPostFallback ? note + " / " : juce::String())
            + (current.candidate ? "Audio estimate: A may have been " : "Audio estimate: PRE may have been ")
            + juce::String (std::fabs (current.contentNoticeMs), 2) + " ms off";
    return note;
}

blind_ui::Screen screenFor (const live_compare::LiveBlindStatus& current, bool playing, float actualPost, bool repeatingAudio,
                            Guess guess)
{
    blind_ui::Screen screen;
    const bool live = current.stage == Stage::active && !current.trial.invalidated;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    const bool stopped = current.stage == Stage::invalidated || current.trial.invalidated;
    const auto guidance = live_compare_ui::blindRecovery (current, stopped);
    // INV-LC10: after REVEAL a standing content notice gives way to RESULT, which states its amount.
    const bool noticeOnly = ! stopped && current.contentHeld
        && juce::String (guidance.instruction) == live_compare_ui::chainDelayNotice();
    screen.guidanceShown = stopped || (current.prolongedWait && ! (revealed && noticeOnly && current.contentNoticeMs != 0.0));
    screen.cause = screen.guidanceShown && !current.signalWait ? live_compare_ui::cause (guidance.reason) : "";
    screen.recovery = screen.guidanceShown ? guidance.instruction : "";
    screen.title = revealed ? "BLIND RESULT" : "LIVE BLIND";
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    const juce::String normalLevel = "END restores normal level";
    // A lower level always says why: BLIND matched on the quieter side so nothing goes over the ceiling.
    const juce::String loweredLevel = "Lowered " + juce::String (returnDb, 1) + " dB to avoid clipping; END restores it";
    juce::String instruction, explanation = returnDb > 0.05 ? loweredLevel : normalLevel;
    if (current.stage == Stage::failed)
        instruction = current.waiting == live_compare::MatchFailure::outOfRange
            ? "MATCH over 24 dB" : "MATCH failed; try again";
    else if (stopped)
    {
        instruction = current.reason == live_compare::RecoveryReason::outputTaken
            ? "Blind stopped; output released" : "Blind stopped; POST output";
    }
    else if (finishing)
    {
        instruction = playing ? "Returning to normal level" : "Return waiting for audio";
        explanation = returnDb > 0.05 ? "END returns +" + juce::String (returnDb, 1) + " dB" : normalLevel;
    }
    else if (current.stage == Stage::approval)
    {
        instruction = current.candidate ? "Lower B to match levels?" : "Lower POST to match levels?";
        explanation = "Lower " + juce::String (std::abs (current.lowerPostDb), 1)
            + " dB; END returns the same amount";
    }
    else if (revealed)
        instruction = "Sources revealed; keep comparing";
    else if (live)
        instruction = current.trial.played == 3 ? "Reveal the sources when ready" : "Try both sources while playing";
    else
        instruction = "";
    if (const auto note = revealed ? resultNote (current) : juce::String(); note.isNotEmpty())
        instruction = note;
    if (current.candidate && current.stage == Stage::preparing && current.matchSecondsRemaining > 0)
    {
        instruction = "MATCH: " + juce::String (current.matchSecondsRemaining) + " s remaining";
        if (current.signalWait) screen.guidanceShown = false;
    }
    screen.instruction = screen.guidanceShown ? juce::String() : instruction;
    // One sentence: END says once what it gives back, the level and then the A to B chain.
    if (current.candidate)
        explanation += explanation == normalLevel || explanation == loweredLevel
            ? " and the A to B chain" : " and restores the A to B chain";
    screen.detail = explanation;
    // The rise on END is named before anything else (INV-LC14).
    if (live && !revealed && repeatingAudio && ! (returnDb > 0.05)) screen.detail = "Repeated audio: checked per block";
    const char* firstA = current.candidate ? "1: A" : "1: PRE";
    const char* firstB = current.candidate ? "1: B" : "1: POST";
    const char* secondA = current.candidate ? "2: A" : "2: PRE";
    const char* secondB = current.candidate ? "2: B" : "2: POST";
    screen.sourceOne = revealed ? (current.trial.firstPre ? firstA : firstB) : "SOURCE 1";
    screen.sourceTwo = revealed ? (current.trial.firstPre ? secondB : secondA) : "SOURCE 2";
    screen.sourceOneEnabled = screen.sourceTwoEnabled = live;
    // Only while the sources are being prepared; a stopped Blind says why in its guidance lines.
    screen.sourceReason = ! live && (current.stage == Stage::preparing || current.stage == Stage::settling)
        ? juce::String ("Preparing the sources") : juce::String();
    // Before the first source is ready the two buttons stand unlit, so the screen is not END alone.
    screen.sourcesShown = live || (! screen.guidanceShown && (current.stage == Stage::preparing || current.stage == Stage::settling));
    screen.audible = current.trial.selected; // selected button, not an audio receipt
    screen.revealShown = live && ! revealed;
    screen.revealEnabled = current.trial.played == 3 && (! guess.asked || guess.answer == 1 || guess.answer == 2);
    if (guess.asked) screen.revealTitle = "Reveal the sources after choosing an answer";
    // Keep an earned REVEAL reachable; ALIGN OFF only replaces an unavailable action.
    screen.alignOffShown = !stopped && !finishing && current.prolongedWait
        && guidance.reason == live_compare::RecoveryReason::contentAmbiguous
        && !(screen.revealShown && current.trial.played == 3);
    screen.approveShown = current.stage == Stage::approval && !current.compensationOff;
    screen.approveTitle = current.candidate ? "Lower B and begin Blind" : "Lower POST and begin Blind";
    screen.approveDescription = explanation;
    screen.endEnabled = ! finishing;
    screen.endTitle = current.candidate ? "End A/B and restore the chain" : "End and restore normal level";
    // GUESS: the same question and answers for both assignments until REVEAL; then the outcome.
    if (guess.asked && live && (! revealed || guess.answer != 0))
    {
        screen.guessShown = true;
        screen.guessOpen = ! revealed;
        screen.guessAnswer = guess.answer;
        screen.guessQuestion = current.candidate ? "Which one is B?" : "Which one is POST?";
        screen.guessRight = revealed && guess.answer == sourceOfB (current.trial);
        if (revealed) screen.guessOutcome = juce::String::fromUTF8 (screen.guessRight ? u8"— right" : u8"— not this time");
    }
    screen.saveImageShown = revealed;
    return screen;
}

Component::Component() : blind_ui::ScreenComponent ("live-blind"), preferences (blind_image::preferenceFile()),
                         sheet (std::make_unique<blind_image::Sheet> ("live-blind"))
{
    addChildComponent (*sheet);
    onGuess = [this] (int source)
    {
        if (! screen().guessOpen || (source != 1 && source != 2)) return;  // the answer is fixed at REVEAL
        answer = source;
        answerGeneration = current.generation;
        answerEpoch = current.trial.epoch;
        show();
    };
    onSaveImage = [this] { openImageSheet(); };
    sheet->onSave = [this] { chooseImageFile(); };
    sheet->onBack = [this] { closeImageSheet(); };
}

Component::~Component()
{
    chooser.reset();
    if (sheet->isVisible()) rememberNames();
}

bool Component::imageSheetOpen() const noexcept { return sheet->isVisible(); }

void Component::setState (const live_compare::LiveBlindStatus& next, bool hostPlaying, float postActual, bool repeatingAudio)
{
    // A new trial starts without an answer and without the last trial's image.
    if (next.generation != current.generation || next.trial.epoch != current.trial.epoch) closeImageSheet();
    current = next;
    playing = hostPlaying;
    actualPost = postActual;
    repeating = repeatingAudio;
    show();
}

void Component::setGuessing (bool on)
{
    guessOn = on;
    show();
}

void Component::show()
{
    if (current.generation != answerGeneration || current.trial.epoch != answerEpoch) answer = 0;
    setScreen (screenFor (current, playing, actualPost, repeating, { guessOn, answer }));
}

blind_image::Facts Component::resultFacts() const
{
    blind_image::Facts facts;
    facts.candidate = current.candidate;
    facts.firstPre = current.trial.firstPre;
    facts.selected = current.trial.selected;
    facts.guess = guessOn && current.trial.revealed ? answer : 0;
    facts.loweredDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    facts.note = resultNote (current);
    facts.nameA = blind_image::rememberedName (preferences, current.candidate, false);
    facts.nameB = blind_image::rememberedName (preferences, current.candidate, true);
    return facts;
}

void Component::openImageSheet()
{
    if (! screen().saveImageShown) return;
    sheet->open (resultFacts());
    sheet->setBounds (getLocalBounds());
    sheet->setVisible (true);
    sheet->toFront (false);
    setCovered (true);
    if (sheet->isShowing()) sheet->nameField (false).grabKeyboardFocus();
}

bool Component::rememberNames()
{
    const auto facts = sheet->facts();
    const auto a = blind_image::rememberName (preferences, facts.candidate, false, sheet->nameField (false).getText());
    const auto b = blind_image::rememberName (preferences, facts.candidate, true, sheet->nameField (true).getText());
    return a && b;
}

void Component::closeImageSheet()
{
    if (! sheet->isVisible()) return;
    namesRemembered = rememberNames();
    chooser.reset();
    sheet->setVisible (false);
    setCovered (false);
    if (! namesRemembered) setActionNotice ("Names could not be remembered");
    if (isShowing()) grabKeyboardFocus();
}

void Component::chooseImageFile()
{
    const auto name = "Hypha-BLIND-RESULT-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".png";
    chooser = std::make_unique<juce::FileChooser> (i18n::tr ("Save the BLIND RESULT image"),
        juce::File::getSpecialLocation (juce::File::userPicturesDirectory).getChildFile (name), "*.png", true);
    juce::Component::SafePointer<Component> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             #if ! JUCE_WINDOWS
                              | juce::FileBrowserComponent::warnAboutOverwriting
                             #endif
                              ,
                          [safeThis] (const juce::FileChooser& chosen)
    {
        if (safeThis == nullptr) return;
        const auto file = chosen.getResult();
       #if JUCE_WINDOWS
        capture::choosePngTarget (file, [safeThis] (const juce::File& target, std::function<void (bool)> reply)
        {
            if (safeThis == nullptr) return;
            const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
                juce::MessageBoxIconType::WarningIcon, i18n::tr ("Replace existing image?"),
                i18n::tr ("A file with this name already exists. Replace it?") + "\n\n" + target.getFileName(),
                i18n::tr ("Cancel"), i18n::tr ("Replace"), safeThis.getComponent());
            juce::NativeMessageBox::showAsync (options, [deliver = std::move (reply)] (int result)
            { deliver (capture::replacePngApproved (result)); });
        }, [safeThis] (const juce::File& target) { if (safeThis != nullptr) safeThis->saveImageTo (target); });
       #else
        if (file != juce::File {}) safeThis->saveImageTo (file.withFileExtension (".png"));
       #endif
    });
}

bool Component::saveImageTo (const juce::File& file)
{
    // Made on the message thread from the frozen RESULT; the audio is never touched.
    const auto saved = capture::writePng (blind_image::render (sheet->facts()), file);
    if (saved != capture::PngSave::saved)
    {
        sheet->setReason (saved == capture::PngSave::notMade ? "The image could not be made; try again"
            : saved == capture::PngSave::folderLocked ? "This folder cannot be written; choose another"
            : saved == capture::PngSave::notReplaced ? "The file could not be replaced; choose another name"
            : "The file could not be written; check free space");
        return false;
    }
    closeImageSheet();
    setActionNotice (namesRemembered ? "Image saved" : "Image saved; names could not be remembered");
    return true;
}

void Component::resized()
{
    ScreenComponent::resized();
    sheet->setBounds (getLocalBounds());
}
}
