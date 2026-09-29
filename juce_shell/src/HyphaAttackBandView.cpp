#include "HyphaAttackComponent.h"

#include "HyphaAttackBandModel.h"
#include "HyphaAttackSnapshotEquality.h"

// DRUM's band state (B-1097, B-1098): the chosen band, the engine's outcome for the lanes' own
// hits, and the selected hit's envelopes. Nothing here measures or guesses; it keeps what the
// engine stated aligned with the hits DRUM already shows, and keeps LIVE on a hit worth showing.
namespace hypha
{
void AttackComponent::setBand (std::uint8_t band)
{
    if (band > attack_band::bandCount || band == chosenBand) return;
    chosenBand = band;
    rebuildBandModel();
    if (followLatest)
        selectBoundaryEvent (true);
    if (onBandChange) onBandChange (band);
    refreshBandEnvelope();
    repaint();
}

attack_band::PreBand AttackComponent::preBand() const noexcept
{
    return pairedObservation() && chosenBand != 0 ? attack_band::preBandOf (bandBatch, chosenBand)
                                                  : attack_band::PreBand::off;
}

void AttackComponent::rebuildBandModel() noexcept
{
    attack_band::build (bandModel, laneModel, bandBatch, chosenBand, currentGeneration, rate);
}

bool AttackComponent::setBandSnapshot (const KirinAttackBandBatch& batch)
{
    const auto any = [] (const auto&) { return true; };
    if (bandBatch.status == batch.status && bandBatch.band == batch.band
        && bandBatch.pre_band == batch.pre_band && bandBatch.generation == batch.generation
        && bandBatch.sample_rate == batch.sample_rate
        && bandBatch.resolution_micros == batch.resolution_micros
        && attack_equality::retained (bandBatch.hits, bandBatch.count, batch.hits, batch.count, any))
        return false;
    bandBatch = batch;
    rebuildBandModel();
    if (followLatest)
        selectBoundaryEvent (true);
    refreshBandEnvelope();
    repaint();
    return true;
}

const attack_lanes::Hit* AttackComponent::bandSelection (const attack_lanes::Hit* selected) const noexcept
{
    return selected != nullptr && chosenBand != 0 ? attack_lanes::find (bandModel, selected->sample)
                                                  : nullptr;
}

const KirinAttackBandHitEnvelope* AttackComponent::selectedBandEnvelope (
    const attack_lanes::Hit* selected) const noexcept
{
    return selected != nullptr && bandEnvelopeValid && bandEnvelope.band == chosenBand
                && bandEnvelope.hit.event_sample == selected->sample
        ? &bandEnvelope : nullptr;
}

// Only while the panes are shown: at 150% and below nothing here is drawn, so nothing is asked.
void AttackComponent::refreshBandEnvelope()
{
    const auto* selected = visibleSelection();
    if (chosenBand == 0 || selected == nullptr || bandEnvelopeSource == nullptr
        || ! attack_band::panesShown (layout()))
    {
        bandEnvelopeValid = false;
        return;
    }
    KirinAttackBandHitEnvelope next {};
    bandEnvelopeValid = bandEnvelopeSource (selected->sample, next)
                     && next.band == chosenBand && next.hit.event_sample == selected->sample;
    if (bandEnvelopeValid)
        bandEnvelope = next;
}

int AttackComponent::followRank (std::uint32_t item) const noexcept
{
    const auto& hit = laneModel.hits[item];
    if (! hit.selectable || ! attack_ui::eventIsVisible (hit.sample, latest, rate))
        return -1;
    if (chosenBand == 0 || item >= bandModel.count)
        return 2;
    // With a band: a hit with its band stated first; while none is, the hit still measuring
    // (MEASURING) before one that was never measured (PLAY TO MEASURE).
    switch (attack_band::readiness (bandModel.hits[item]))
    {
        case attack_band::Readiness::stated:      return 2;
        case attack_band::Readiness::pending:     return 1;
        case attack_band::Readiness::notMeasured: return 0;
    }
    return 0;
}

bool AttackComponent::bandNeedsPlay() const noexcept
{
    if (chosenBand == 0)
        return false;
    for (std::uint32_t item = 0; item < bandModel.count; ++item)
    {
        const auto& hit = bandModel.hits[item];
        if (attack_ui::eventIsVisible (hit.sample, latest, rate)
            && attack_band::readiness (hit) != attack_band::Readiness::notMeasured)
            return false;
    }
    return true;
}
}
