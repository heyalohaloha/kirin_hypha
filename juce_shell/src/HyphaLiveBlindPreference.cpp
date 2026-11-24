#include "HyphaLiveBlindComponent.h"
#include "HyphaBlindPreferenceText.h"
#include <cmath>
#include <limits>
namespace hypha::live_blind_ui
{
void Component::resetPreference()
{
    // The plain listen's GUESS answer is bound to its own trial ids (show); the rest ends with the trial.
    const auto answer = memory->answer;
    const auto answerGeneration = memory->answerGeneration;
    const auto answerEpoch = memory->answerEpoch;
    *memory = {};
    memory->answer = answer; memory->answerGeneration = answerGeneration; memory->answerEpoch = answerEpoch;
}
bool Component::canChangePreferenceRounds() const noexcept
{
    return !preferenceBusy && memory->preference.answered() == 0
        && !(current.stage == live_compare::BlindStage::active && current.trial.revealed);
}
bool Component::setPreferenceRounds (int value)
{
    value = blind_preference::normaliseRounds (value);
    if (!canChangePreferenceRounds()) return false;
    rounds = value; guessOn = false; memory->answer = 0;
    if (current.stage == live_compare::BlindStage::active && !current.trial.invalidated) {
        memory->activeRounds = value;
        // Before any vote, the same assignment and gain remain
        // valid. Do not restart audio, redraw randomness or replace the captured gain.
        memory->preference.reset(); memory->awaitingEpoch = false;
        refreshPreference();
    }
    show(); return true;
}
void Component::refreshPreference()
{
    using Phase = blind_preference::Phase;
    if (memory->activeRounds == 0 || current.stage != live_compare::BlindStage::active || current.trial.invalidated) {
        resetPreference(); return;
    }
    if (memory->activeRounds == 1) return; // Ordinary listen has no preference votes.
    if (memory->preference.phase() != Phase::off && memory->preference.generation() != current.generation) {
        resetPreference(); return;
    }
    if (memory->preference.phase() == Phase::off) {
        // Do not fabricate a resumed trial from a restored/revealed result.
        if (!current.trial.revealed) {
            memory->preference.begin (memory->activeRounds, current.generation, current.trial.epoch);
        }
    } else if (memory->preference.epoch() != current.trial.epoch) {
        if (memory->awaitingEpoch && current.trial.epoch == memory->expectedEpoch && !current.trial.revealed
            && memory->preference.armNext (current.generation, current.trial.epoch)) memory->awaitingEpoch = false;
        else { resetPreference(); return; }
    }
    if (current.trial.revealed) {
        if (memory->preference.canReveal()) { retainPreferenceNotes(); memory->preference.reveal(); }
    } else memory->preference.observePlayed (current.generation, current.trial.epoch, current.trial.played);
}
void Component::retainPreferenceNotes()
{
    memory->preferenceFallback |= current.trial.usedPostFallback;
    memory->preferenceTimingFallback |= current.trial.fallbackForTiming;
    memory->preferenceCeilingFallback |= current.trial.fallbackForCeiling;
    // An estimate first measured after the round's own answer belongs to no answer (INV-LC19).
    if (current.contentNoticeMs != 0 && ! (current.trial.revealed && currentRoundAnswered()))
        memory->preferenceContentNoticeMs = current.contentNoticeMs;
}
bool Component::currentRoundAnswered() const noexcept
{
    const auto answered = memory->preference.answered();
    const auto* last = answered > 0 ? memory->preference.answerAt (answered - 1) : nullptr;
    return last != nullptr && last->epoch == current.trial.epoch && memory->preference.generation() == current.generation;
}
double Component::shownContentNoticeMs() const noexcept
{
    return current.contentNoticeMs == 0 || currentRoundAnswered() ? memory->preferenceContentNoticeMs : current.contentNoticeMs;
}
void Component::choosePreference (int source)
{
    if (preferenceBusy || !memory->preference.canAnswer() || (source != 1 && source != 2) || !onPreferenceAnswer) return;
    const auto generation = current.generation;
    const auto epoch = current.trial.epoch;
    preferenceBusy = true;
    const auto accepted = onPreferenceAnswer (source, generation, epoch);
    if (!accepted || current.generation != generation || current.trial.epoch != epoch
        || current.stage != live_compare::BlindStage::active || current.trial.invalidated
        || !memory->preference.answer (source, *accepted)) {
        preferenceBusy = false; refreshPreference(); show(); return;
    }
    retainPreferenceNotes();
    preferenceBusy = false;
    startNextAssignment();
    refreshPreference(); show();
}
// The accepted answer's next assignment, for its own round only. A gain receipt still on its way refuses it without
// a change, so each refresh asks again; the first that is granted moves the round and no later one is asked.
void Component::startNextAssignment()
{
    const auto generation = current.generation;
    const auto epoch = current.trial.epoch;
    if (preferenceBusy || memory->awaitingEpoch || current.trial.revealed
        || current.stage != live_compare::BlindStage::active || current.trial.invalidated
        || memory->preference.phase() != blind_preference::Phase::awaitingAssignment
        || memory->preference.generation() != generation || memory->preference.epoch() != epoch) return;
    // The existing session refuses epoch exhaustion rather than wrapping.
    memory->expectedEpoch = static_cast<std::uint32_t> (epoch + 1u);
    preferenceBusy = true;
    const bool started = epoch != std::numeric_limits<std::uint32_t>::max()
        && onPreferenceNext && onPreferenceNext (generation, epoch);
    memory->awaitingEpoch = started;
    if (!started && current.reason == live_compare::RecoveryReason::randomUnavailable) {
        memory->preference.randomnessFailed();
        setActionNotice ("Random assignment was unavailable");
    }
    // Other refusals (stale epoch, lost authority or pending gain receipt)
    // retain the processor's real recovery state, never claim an RNG failure.
    preferenceBusy = false;
}
void Component::setPreferenceApprovedGains (std::uint64_t generation,
    std::optional<live_compare::GainSnapshot> gain)
{
    memory->approvedGainsValid = false;
    const bool valid = generation == current.generation && memory->activeRounds > 0
        && gain && gain->coherent && gain->retained && !gain->limited
        && std::isfinite (gain->pre) && gain->pre > 0 && std::isfinite (gain->post) && gain->post > 0
        && std::isfinite (gain->ceiling) && gain->ceiling > 0;
    if (valid) {
        // Capture only before the first answer. A later approval must not rewrite
        // the matched level reported for an already answered listening trial.
        if (!memory->approvedGainAtStart && memory->preference.answered() == 0 && !current.trial.revealed) {
            memory->approvedGainAtStart = gain; memory->approvedGainGeneration = generation;
            memory->approvedPreDb = 20.0 * std::log10 (gain->pre);
            memory->approvedPostDb = 20.0 * std::log10 (gain->post);
        }
        if (memory->approvedGainAtStart && memory->approvedGainGeneration == generation) {
            const auto& first = *memory->approvedGainAtStart;
            memory->approvedGainsValid = gain->revision == first.revision && gain->identity.same (first.identity)
                && gain->pre == first.pre && gain->post == first.post && gain->ceiling == first.ceiling;
        }
    }
    show();
}
void Component::decoratePreference (blind_ui::Screen& screen) const
{
    const bool active = current.stage == live_compare::BlindStage::active && !current.trial.invalidated;
    if (!active) return;
    if (!guessOn && !current.trial.revealed) {
        screen.roundsShown = true; screen.roundsEnabled = canChangePreferenceRounds();
        screen.roundsValue = memory->activeRounds;
        if (memory->preference.answered() > 0)
            screen.roundsProgress = blind_preference::roundProgress(memory->preference.roundNumber(),memory->preference.planned());
    }
    const bool singleListen = memory->activeRounds == 1;
    if (!singleListen && memory->preference.planned() == 0) return;
    const bool revealed = current.trial.revealed
        && (singleListen || memory->preference.phase() == blind_preference::Phase::revealed);
    if (revealed) {
        const auto nameA = blind_preference::sideName(true,current.candidate,memory->preferenceNameA,memory->preferenceNameB);
        const auto nameB = blind_preference::sideName(false,current.candidate,memory->preferenceNameA,memory->preferenceNameB);
        if (!singleListen) {
            screen.guessShown = false; screen.guessOpen = false; screen.guessAnswer = 0; screen.guessRight = false;
            screen.guessQuestion.clear(); screen.guessOutcome.clear();
            screen.preferenceResultText = blind_preference::summaryLine (memory->preference.summary(), current.candidate,
                memory->preferenceNameA, memory->preferenceNameB);
            const auto chance = blind_preference::probabilityLine (memory->preference.summary());
            if (chance.isNotEmpty()) screen.preferenceResultText += "\n" + chance;
            screen.sourceOne = "1: " + (current.trial.firstPre ? nameA : nameB);
            screen.sourceTwo = "2: " + (current.trial.firstPre ? nameB : nameA);
        }
        if (memory->approvedGainsValid && memory->approvedGainGeneration == current.generation) {
            const auto signedGain=[](double value) {
                const auto rounded=std::round(value*10.0)/10.0;
                return rounded==0.0 ? juce::String("0.0") : blind_preference::signedDb(value);
            };
            screen.matchedGainText = i18n::tr("Matched level: %1 %2 dB; %3 %4 dB")
                .replace("%1",nameA).replace("%2",signedGain(memory->approvedPreDb))
                .replace("%3",nameB).replace("%4",signedGain(memory->approvedPostDb));
        }
        return;
    }
    if (singleListen) return; // Preserve the ordinary listen/reveal surface.
    screen.votingComplete = memory->preference.phase() == blind_preference::Phase::awaitingReveal;
    screen.revealEnabled = memory->preference.canReveal();
    screen.revealTitle = "Reveal the results after at least one answer";
    if (screen.revealEnabled) screen.alignOffShown = false;
    if (screen.votingComplete)
    {
        screen.roundsShown = false;
        screen.instruction = "All rounds answered; reveal when ready";
        return;
    }
    screen.guessShown = true; screen.guessOpen = memory->preference.canAnswer(); screen.guessAnswer = 0;
    screen.guessQuestion = "Which do you prefer?";
    screen.guessQuestionFixed = true;
    screen.guessOutcome = blind_preference::roundProgress(memory->preference.roundNumber(),memory->preference.planned());
}
}
