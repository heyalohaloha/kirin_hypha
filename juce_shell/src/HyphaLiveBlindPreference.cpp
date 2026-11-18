#include "HyphaLiveBlindComponent.h"
#include "HyphaBlindPreferenceText.h"
#include <cmath>
#include <limits>
namespace hypha::live_blind_ui
{
void Component::resetPreference()
{
    detailsMeasurements.clear(); measuredDetails = {};
    preference.reset(); activeRounds = 0; awaitingEpoch = false; expectedEpoch = 0; detailsOpen = false;
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
        // Before any vote, the same assignment/gain/measurement interval remains
        // valid. Do not restart audio, redraw randomness or discard the interval.
        preference.reset(); awaitingEpoch = false; detailsOpen = false;
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
    if (activeRounds == 1) return; // legacy listen: no question, counts or voting gate
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
        if (preference.canReveal()) { retainPreferenceNotes(); detailsMeasurements.freeze(); measuredDetails = detailsMeasurements.result(); preference.reveal(); detailsOpen = false; }
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
    retainPreferenceNotes(); detailsOpen = false;
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
        if (!approvedGainAtStart && preference.answered() == 0) {
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
    if (detailsOpen) show();
}
void Component::observePreferenceDetails (const KirinReferenceLiveMetricFrame* level,
    const KirinPerceptualBatch* sharp, const KirinSpectrumBatch* spectrum, std::uint64_t continuity)
{
    if (!detailsMeasurements.active() || current.trial.revealed) return;
    if (level) detailsMeasurements.level (*level);
    if (sharp) detailsMeasurements.perceptual (*sharp);
    if (spectrum) detailsMeasurements.spectrum (*spectrum, continuity);
}
void Component::decoratePreference (blind_ui::Screen& screen) const
{
    const bool active = current.stage == live_compare::BlindStage::active && !current.trial.invalidated;
    if (!active) return;
    if (!guessOn && !current.trial.revealed) {
        screen.roundsShown = true; screen.roundsEnabled = canChangePreferenceRounds();
        screen.roundsValue = activeRounds;
        if (preference.answered() > 0)
            screen.roundsProgress = juce::String (preference.roundNumber()) + " / " + juce::String (preference.planned());
    }
    if (preference.planned() == 0) return;
    const bool revealed = current.trial.revealed && preference.phase() == blind_preference::Phase::revealed;
    if (revealed) {
        screen.guessShown = false; screen.guessOpen = false; screen.guessAnswer = 0; screen.guessRight = false;
        screen.guessQuestion.clear(); screen.guessOutcome.clear();
        screen.preferenceResultText = blind_preference::summaryLine (preference.summary(), current.candidate);
        const auto chance = blind_preference::probabilityLine (preference.summary());
        if (chance.isNotEmpty()) screen.preferenceResultText += "\n" + chance;
        screen.detailsShown = preference.answered() > 0; screen.detailsOpen = detailsOpen;
        if (detailsOpen) {
            screen.detailsText = blind_preference::historyText (preference, current.candidate);
            if (!current.candidate) {
                screen.detailsText += "\n\n" + i18n::tr ("Measured during this blind") + " (POST - PRE)";
                const auto line = [&] (const char* name, const blind_details::Summary& value, const char* unit) {
                    return juce::String ("\n") + name + ": " + (value.sufficient
                        ? juce::String (value.median, 2) + " " + unit
                        : i18n::tr (value.capacityExceeded ? "Measurement capacity reached; value not shown"
                            : "Not shown because the measured time is too short"));
                };
                screen.detailsText += line ("PSR", measuredDetails.psr, "dB")
                    + line ("CREST", measuredDetails.crest, "dB") + line ("SHARP", measuredDetails.sharp, "acum");
            }
            if (!current.candidate && approvedGainsValid && approvedGainGeneration == current.generation) {
                screen.detailsText += "\n" + i18n::tr ("Matched level: PRE %1 dB; POST %2 dB.")
                    .replace ("%1", juce::String (approvedPreDb,1)).replace ("%2", juce::String (approvedPostDb,1));
            }
        }
        return;
    }
    screen.guessShown = true; screen.guessOpen = preference.canAnswer(); screen.guessAnswer = 0;
    screen.guessQuestion = i18n::tr ("Which do you prefer?");
    screen.guessOutcome = juce::String (preference.roundNumber()) + " / " + juce::String (preference.planned());
    screen.revealEnabled = preference.canReveal();
    screen.revealTitle = "Reveal the results after at least one answer";
    if (screen.revealEnabled) screen.alignOffShown = false;
    if (preference.phase() == blind_preference::Phase::awaitingReveal && !screen.guidanceShown)
        screen.instruction = "All rounds answered; reveal when ready";
}
}
