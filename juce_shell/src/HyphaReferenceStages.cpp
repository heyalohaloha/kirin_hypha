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
      "KIRIN OS IS NOT RESPONDING / OPEN KIRIN OS", "Waiting for Kirin OS", "Receiving from Kirin OS",
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
      "Play the song in your DAW", "V follows the song; C uses its Cue." },
    // aligning
    { K::waiting, W::alignmentPlay, "ALIGNING VERSION V / KEEP PLAYING", "NO MATCH IN 30 S OF PLAY / CHOOSE THE VERSION AGAIN",
      "Aligning with A. Keep playing", "Aligning V with A", "Keep playing. V must be a Version of the song you are playing." },
    // noMatchingPassage
    { K::unable, W::none, "NO VERIFIED MATCH / CHECK VERSION V", "", "No verified match here; check Version",
      "V did not match this passage", "Check that V is a Version of A at this POST, or play a different matching passage." },
    // playAnotherPassage
    { K::waiting, W::none, "THIS PASSAGE REPEATS IN V / PLAY ANOTHER PASSAGE TO ALIGN V", "", "Play another passage",
      "Play another passage to align V", "This passage repeats in V. Play a part that occurs only once." },
    // verifyingSource
    { K::waiting, W::preparation, "VERIFYING SOURCE / WAIT A MOMENT", "SOURCE NOT VERIFIED IN 10 S / CHECK THE SOURCE IN KIRIN OS",
      "Verifying source", "Verifying %1 source", "The source is being checked. A stays live." },
    // loadingAudio
    { K::waiting, W::preparation, "LOADING %1 AT PLAYHEAD / KEEP PLAYING", "AUDIO NOT LOADED IN 10 S / PLAY FROM ANOTHER POSITION",
      "Loading audio here; keep playing", "Loading %1 at the playhead", "Keep playing while audio loads." },
    // outsideCue
    { K::unable, W::none, "OUTSIDE %1 CUE / MOVE OR CHOOSE LONGER CUE", "", "Outside Cue; move or choose longer Cue",
      "This playhead is outside %1's Cue",
      "Move to the comparison passage, or choose a longer or looping Cue in Kirin OS. A stays live." },
    // preparing
    { K::waiting, W::preparation, "PREPARING %1 / WAIT A MOMENT", "NOT PREPARED IN 10 S / OPEN THE SOURCE IN KIRIN OS",
      "Preparing", "Preparing %1", "This takes a moment." },
    // openKirinOs
    { K::unable, W::none, "KIRIN OS IS CLOSED / OPEN KIRIN OS", "", "Open Kirin OS", "Open Kirin OS",
      "Versions and References registered in Kirin OS arrive here automatically." },
    // rankSet
    { K::unable, W::none, "NO B SET FOR HYPHA / RANK A B SET FOR HYPHA IN KIRIN OS", "", "Rank a B set for Hypha in Kirin OS",
      "Rank a B set for Hypha in Kirin OS", "B plays the songs of the B sets you rank for Hypha in Kirin OS." },
    // setsNotRead
    { K::unable, W::none, "B SETS NOT READ / UPDATE KIRIN OS AND HYPHA", "", "Update Kirin OS and Hypha",
      "Kirin OS's B sets were not read", "Update Kirin OS and Hypha to the same release." },
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
    { K::unable, W::none, "SOURCE UNAVAILABLE / OPEN KIRIN OS", "", "Open Kirin OS", "%1's source is unavailable",
      "Open Kirin OS to make the source available. A stays live." },
    // savedChoiceUnavailable
    { K::unable, W::none, "SAVED CHOICE UNAVAILABLE / CHOOSE AGAIN", "", "Choose again", "The saved choice for %1 is gone",
      "Choose the source again. A stays live." },
    // attention
    { K::unable, W::none, "SOURCE NOT READY / PREPARE AGAIN IN KIRIN OS", "", "Check the source in Kirin OS",
      "Open Kirin OS to check the source", "The source changed or could not be opened." },
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
        case WaitBudget::preparation: return PreparationBudget::preparationSeconds;
        case WaitBudget::alignmentPlay: return PreparationBudget::alignmentPlaySeconds;
        case WaitBudget::none: break;
    }
    return 0.0;
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
