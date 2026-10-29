#include "HyphaReferenceStages.h"

#include "HyphaReferenceComponent.h"
#include "HyphaReferencePreparationWatch.h"

#include <array>

namespace hypha::reference_ui
{
namespace
{
using K = StatusKind;
using W = WaitBudget;

// SourceStep の順。足した段階は表にも足す（数が合わなければ build で止まる）。
const std::array<Stage, stageCount> stages { {
    // ready
    { K::ready, W::none, "READY / A REMAINS LIVE", "", "Ready", "", "" },
    // waitingForKirinOs
    { K::waiting, W::kirinOsResponse, "WAITING FOR KIRIN OS REFERENCE / KEEP KIRIN OS OPEN",
      "CHECKING", "Waiting for Kirin OS", "Receiving from Kirin OS",
      "Versions and References registered in Kirin OS arrive here automatically." },
    // registerVersion
    { K::unable, W::none, "NO VERSION FOR V / REGISTER A VERSION IN KIRIN OS", "", "Register a Version in Kirin OS",
      "Register a Version of this song in Kirin OS",
      "V plays another Version of the song you are playing, registered in Kirin OS." },
    // chooseVersion
    { K::unable, W::none, "NO VERSION CHOSEN / CHOOSE VERSION V", "", "Choose a Version", "Choose a Version for V",
      "Choose it in V / VERSION above. V plays another Version of the song you are playing." },
    // enableCheck
    { K::unable, W::none, "NO CHECK FOR C / ENABLE A CHECK IN KIRIN OS", "", "Enable a Check in Kirin OS",
      "No Check is enabled in Kirin OS", "C plays the source a Check compares your mix with." },
    // chooseSource
    { K::unable, W::none, "NO SOURCE FOR %1 / CHOOSE A SOURCE IN KIRIN OS", "", "Choose a source in Kirin OS",
      "Choose a source for %1 in Kirin OS", "C plays the source a Check compares your mix with." },
    // playDaw
    { K::waiting, W::none, "%1 WAITS FOR THE DAW / PLAY A TO AUDITION", "", "Ready when the DAW plays",
      "Play the song in your DAW", "B: Reference song / C: Check source / V: another Version of this song" },
    // aligning
    { K::waiting, W::alignmentPlay, "ALIGNING VERSION V / KEEP PLAYING", "CHECKING",
      "Aligning with A. Keep playing", "Aligning V with A", "Keep playing. V must be a Version of the song you are playing." },
    // noMatchingPassage
    { K::unable, W::none, "NO VERIFIED MATCH / CHECK VERSION V", "", "No verified match here; check Version",
      "V did not match this passage", "Check that V is a Version of A at this POST, or play a different matching passage." },
    // playAnotherPassage
    { K::waiting, W::none, "THIS PASSAGE REPEATS IN V / PLAY ANOTHER PASSAGE TO ALIGN V", "", "Play another passage",
      "Play another passage to align V", "This passage repeats in V. Play a part that occurs only once." },
    // verifyingSource
    { K::waiting, W::preparation, "VERIFYING SOURCE / WAIT A MOMENT", "CHECKING",
      "Verifying source", "Verifying %1 source", "The source is being checked. A stays live." },
    // loadingAudio
    { K::waiting, W::pages, "LOADING %1 AT PLAYHEAD / KEEP PLAYING", "CHECKING",
      "Loading audio here; keep playing", "Loading %1 at the playhead", "Keep playing while audio loads." },
    // outsideCue
    { K::unable, W::none, "OUTSIDE %1 CUE / MOVE OR CHOOSE LONGER CUE", "", "Outside Cue; move or choose longer Cue",
      "This playhead is outside %1's Cue",
      "Move to the comparison passage, or choose a longer or looping Cue in Kirin OS. A stays live." },
    // preparing
    { K::waiting, W::preparation, "PREPARING %1 / WAIT A MOMENT", "CHECKING",
      "Preparing", "Preparing %1", "This takes a moment." },
    // openKirinOs
    { K::unable, W::none, "KIRIN OS IS CLOSED / OPEN KIRIN OS", "", "Open Kirin OS", "Open Kirin OS",
      "Versions and References registered in Kirin OS arrive here automatically." },
    // rankSet
    { K::unable, W::none, "NO REFERENCE SET FOR HYPHA / RANK A REFERENCE SET FOR HYPHA IN KIRIN OS", "", "Rank a Reference set for Hypha in Kirin OS",
      "Rank a Reference set for Hypha in Kirin OS", "B plays the Reference songs you rank for Hypha in Kirin OS." },
    // setsNotRead
    { K::unable, W::none, "REFERENCE SETS NOT READ / UPDATE KIRIN OS AND HYPHA", "", "Update Kirin OS and Hypha",
      "Kirin OS's Reference sets were not read", "Update Kirin OS and Hypha to the same release." },
    // sourceChanged
    { K::unable, W::none, "SOURCE CHANGED / PREPARE AGAIN IN KIRIN OS", "", "Prepare the source again in Kirin OS",
      "The source of %1 changed", "Prepare it again in Kirin OS. A stays live." },
    // sourceFormatChanged
    { K::unable, W::none, "SOURCE FORMAT CHANGED / VERIFY IN KIRIN OS", "", "Verify the source in Kirin OS",
      "The format of %1's source changed", "Verify it in Kirin OS. A stays live." },
    // sourceUnopenable
    { K::unable, W::none, "SOURCE COULD NOT BE OPENED / CHECK THE SOURCE IN KIRIN OS", "", "Check the source in Kirin OS",
      "%1's source could not be opened", "Check the source file in Kirin OS. A stays live." },
    // sourceUnavailable
    { K::unable, W::none, "SOURCE UNAVAILABLE / CHECK THE SOURCE IN KIRIN OS", "", "Check the source in Kirin OS", "%1's source is unavailable",
      "Check the source file in Kirin OS. A stays live." },
    // savedChoiceUnavailable
    { K::unable, W::none, "SAVED CHOICE UNAVAILABLE / CHOOSE AGAIN", "", "Choose again", "The saved choice for %1 is gone",
      "Choose the source again. A stays live." },
    // channelsDiffer（2026-10-19：mono の音源は stereo の track でも左右に写して鳴らす。stereo の音源を mono の
    // track では混ぜずに断る）
    { K::unable, W::none, "STEREO SOURCE ON A MONO TRACK / USE A STEREO TRACK", "",
      "Stereo source on a mono track", "%1's source is stereo and this track is mono",
      "A stereo source plays on a stereo track. A mono source plays on either. A stays live." },
    // sourceRecordMismatch：Kirin OS の音源の記録がファイルと合わない（Kirin OS が書き直せば直る）
    { K::unable, W::none, "SOURCE RECORD DOES NOT MATCH / PREPARE AGAIN IN KIRIN OS", "", "Source record does not match",
      "%1's source record does not match its file", "Prepare it again in Kirin OS. A stays live." },
    // sourceRecordUnsupported：この Hypha が読めない形の記録
    { K::unable, W::none, "SOURCE RECORD NOT READ / UPDATE KIRIN OS AND HYPHA", "", "Source record not read",
      "%1's source record was not read", "Update Kirin OS and Hypha to the same release. A stays live." },
    // publicationInvalid：Kirin OS の Reference の公開（Preset・Work・catalog）を読めない。状態の行と同じ直し方
    { K::unable, W::none, "REFERENCE PUBLICATION INVALID / PUBLISH AGAIN IN KIRIN OS", "", "Publication not read",
      "%1's Reference publication was not read", "Publish it again in Kirin OS. A stays live." },
    // attention：Hypha に文の無い理由。直し方を推測せず、案内に理由のコードを残す（2026-10-18）
    { K::unable, W::none, "SOURCE NOT USABLE IN HYPHA / SEE THE REASON CODE", "", "Not usable in Hypha",
      "%1's source is not usable in Hypha", "Hypha has no message for this reason. A stays live." },
} };
}

const Stage& stageOf (SourceStep step) noexcept
{
    const auto index = static_cast<std::size_t> (step);
    return stages[index < stages.size() ? index : stages.size() - 1];
}

juce::String stageLine (SourceStep step, int slot)
{
    return juce::String (stageOf (step).status).replace ("%1", roleLetter (slot));
}

juce::String stageHeading (SourceStep step, int slot)
{
    return juce::String (stageOf (step).heading).replace ("%1", roleLetter (slot));
}

double budgetSeconds (WaitBudget budget) noexcept
{
    switch (budget)
    {
        case WaitBudget::kirinOsResponse: return PreparationBudget::kirinOsResponseSeconds;
        case WaitBudget::pages: return PreparationBudget::pageReadSeconds;
        case WaitBudget::preparation: return PreparationBudget::preparationSeconds;
        case WaitBudget::alignmentPlay: return PreparationBudget::alignmentPlaySeconds;
        case WaitBudget::none: break;
    }
    return 0.0;
}

juce::String cueSubstituteLine (CueSubstitute substitute)
{
    switch (substitute)
    {
        case CueSubstitute::wholeSong: return "C COMPARED OVER THE WHOLE SONG / MEASURE ITS CUE IN KIRIN OS";
        case CueSubstitute::noSpectrum: return "C SPECTRUM WAITS FOR ITS CUE VALUES / MEASURE ITS CUE IN KIRIN OS";
        case CueSubstitute::none: break;
    }
    return {};
}

juce::String stepText (SourceStep step)
{
    return stageOf (step).brief;
}

StatusKind kindOf (SourceStep step) noexcept
{
    return stageOf (step).kind;
}
}
