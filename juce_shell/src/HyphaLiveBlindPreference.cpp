#include "HyphaLiveBlindComponent.h"
#include "HyphaBlindPreferenceText.h"
#include <cmath>
#include <limits>
namespace hypha::live_blind_ui
{
void Component::resetPreference()
{
    preferenceNameA.clear(); preferenceNameB.clear();
    preference.reset(); activeRounds = 0; awaitingEpoch = false; expectedEpoch = 0;
    preferenceFallback = preferenceTimingFallback = preferenceCeilingFallback = false;
    preferenceContentNoticeMs = 0; approvedGainsValid = false; approvedGainGeneration = 0;
    approvedGainAtStart.reset(); approvedPreDb = approvedPostDb = 0;
}
bool Component::canChangePreferenceRounds() const noexcept
{
    return !preferenceBusy && preference.answered() == 0
        && !(current.stage == live_compare::BlindStage::active && current.trial.revealed);
}
bool Component::setPreferenceRounds (int value)
{
    value = blind_preference::normaliseRounds (value);
    if (!canChangePreferenceRounds()) return false;
    rounds = value; guessOn = false; answer = 0;
    if (current.stage == live_compare::BlindStage::active && !current.trial.invalidated) {
        activeRounds = value;
        // Before any vote, the same assignment and gain remain
        // valid. Do not restart audio, redraw randomness or replace the captured gain.
        preference.reset(); awaitingEpoch = false;
        refreshPreference();
    }
    show(); return true;
}
void Component::refreshPreference()
{
    using Phase = blind_preference::Phase;
    if (activeRounds == 0 || current.stage != live_compare::BlindStage::active || current.trial.invalidated) {
        resetPreference(); return;
    }
    if (activeRounds == 1) return; // Ordinary listen has no preference votes.
    if (preference.phase() != Phase::off && preference.generation() != current.generation) {
        resetPreference(); return;
    }
    if (preference.phase() == Phase::off) {
        // Do not fabricate a resumed trial from a restored/revealed result.
        if (!current.trial.revealed) {
            preference.begin (activeRounds, current.generation, current.trial.epoch);
        }
    } else if (preference.epoch() != current.trial.epoch) {
        if (awaitingEpoch && current.trial.epoch == expectedEpoch && !current.trial.revealed
            && preference.armNext (current.generation, current.trial.epoch)) awaitingEpoch = false;
        else { resetPreference(); return; }
    }
    if (current.trial.revealed) {
        if (preference.canReveal()) { retainPreferenceNotes(); preference.reveal(); }
    } else preference.observePlayed (current.generation, current.trial.epoch, current.trial.played);
}
void Component::retainPreferenceNotes()
{
    preferenceFallback |= current.trial.usedPostFallback;
    preferenceTimingFallback |= current.trial.fallbackForTiming;
    preferenceCeilingFallback |= current.trial.fallbackForCeiling;
    if (current.contentNoticeMs != 0) preferenceContentNoticeMs = current.contentNoticeMs;
}
void Component::choosePreference (int source)
{
    if (preferenceBusy || !preference.canAnswer() || (source != 1 && source != 2) || !onPreferenceAnswer) return;
    const auto generation = current.generation;
    const auto epoch = current.trial.epoch;
    preferenceBusy = true;
    const auto accepted = onPreferenceAnswer (source, generation, epoch);
    if (!accepted || current.generation != generation || current.trial.epoch != epoch
        || current.stage != live_compare::BlindStage::active || current.trial.invalidated
        || !preference.answer (source, *accepted)) {
        preferenceBusy = false; refreshPreference(); show(); return;
    }
    retainPreferenceNotes();
    if (preference.phase() == blind_preference::Phase::awaitingAssignment) {
        // The existing session refuses epoch exhaustion rather than wrapping.
        expectedEpoch = static_cast<std::uint32_t> (epoch + 1u);
        const bool started = epoch != std::numeric_limits<std::uint32_t>::max()
            && onPreferenceNext && onPreferenceNext (generation, epoch);
        awaitingEpoch = started;
        if (!started && current.reason == live_compare::RecoveryReason::randomUnavailable) {
            preference.randomnessFailed();
            setActionNotice ("Random assignment was unavailable");
        }
        // Other refusals (stale epoch, lost authority or pending gain receipt)
        // retain the processor's real recovery state, never claim an RNG failure.
    }
    preferenceBusy = false;
    refreshPreference(); show();
}
void Component::setPreferenceApprovedGains (std::uint64_t generation,
    std::optional<live_compare::GainSnapshot> gain)
{
    approvedGainsValid = false;
    const bool valid = generation == current.generation && activeRounds > 0
        && gain && gain->coherent && gain->retained && !gain->limited
        && std::isfinite (gain->pre) && gain->pre > 0 && std::isfinite (gain->post) && gain->post > 0
        && std::isfinite (gain->ceiling) && gain->ceiling > 0;
    if (valid) {
        // Capture only before the first answer. A later approval must not rewrite
        // the matched level reported for an already answered listening trial.
        if (!approvedGainAtStart && preference.answered() == 0 && !current.trial.revealed) {
            approvedGainAtStart = gain; approvedGainGeneration = generation;
            approvedPreDb = 20.0 * std::log10 (gain->pre);
            approvedPostDb = 20.0 * std::log10 (gain->post);
        }
        if (approvedGainAtStart && approvedGainGeneration == generation) {
            const auto& first = *approvedGainAtStart;
            approvedGainsValid = gain->revision == first.revision && gain->identity.same (first.identity)
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
        screen.roundsValue = activeRounds;
        if (preference.answered() > 0)
            screen.roundsProgress = blind_preference::roundProgress(preference.roundNumber(),preference.planned());
    }
    const bool singleListen = activeRounds == 1;
    if (!singleListen && preference.planned() == 0) return;
    const bool revealed = current.trial.revealed
        && (singleListen || preference.phase() == blind_preference::Phase::revealed);
    if (revealed) {
        const auto nameA = blind_preference::sideName(true,current.candidate,preferenceNameA,preferenceNameB);
        const auto nameB = blind_preference::sideName(false,current.candidate,preferenceNameA,preferenceNameB);
        if (!singleListen) {
            screen.guessShown = false; screen.guessOpen = false; screen.guessAnswer = 0; screen.guessRight = false;
            screen.guessQuestion.clear(); screen.guessOutcome.clear();
            screen.preferenceResultText = blind_preference::summaryLine (preference.summary(), current.candidate, preferenceNameA, preferenceNameB);
            const auto chance = blind_preference::probabilityLine (preference.summary());
            if (chance.isNotEmpty()) screen.preferenceResultText += "\n" + chance;
            screen.sourceOne = "1: " + (current.trial.firstPre ? nameA : nameB);
            screen.sourceTwo = "2: " + (current.trial.firstPre ? nameB : nameA);
            screen.preferenceResultNote = text_style::shownText(screen.instruction) + "\n" + text_style::shownText(screen.detail);
        }
        if (approvedGainsValid && approvedGainGeneration == current.generation) {
            const auto signedGain=[](double value) {
                const auto rounded=std::round(value*10.0)/10.0;
                return rounded==0.0 ? juce::String("0.0") : blind_preference::signedDb(value);
            };
            screen.matchedGainText = i18n::tr("Matched level: %1 %2 dB; %3 %4 dB")
                .replace("%1",nameA).replace("%2",signedGain(approvedPreDb))
                .replace("%3",nameB).replace("%4",signedGain(approvedPostDb));
        }
        return;
    }
    if (singleListen) return; // Preserve the ordinary listen/reveal surface.
    screen.guessShown = true; screen.guessOpen = preference.canAnswer(); screen.guessAnswer = 0;
    screen.guessQuestion = "Which do you prefer?";
    screen.guessQuestionFixed = true;
    screen.guessOutcome = blind_preference::roundProgress(preference.roundNumber(),preference.planned());
    screen.revealEnabled = preference.canReveal();
    screen.revealTitle = "Reveal the results after at least one answer";
    if (screen.revealEnabled) screen.alignOffShown = false;
    if (preference.phase() == blind_preference::Phase::awaitingReveal && !screen.guidanceShown)
        screen.instruction = "All rounds answered; reveal when ready";
}
}
