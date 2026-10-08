#pragma once
#include "HyphaAttackV2Clock.h"
#include "HyphaAttackV2Selection.h"
#include "kirin_hypha_navigation_snapshot_ffi.h"
#include <functional>

namespace hypha::attack_v2
{
class State
{
public:
    using Request = std::function<std::uint8_t (const KirinAttackSingleV2Request&, std::uint64_t&)>;
    using Cancel = std::function<void (std::uint64_t)>;
    Presentation adopted;
    // The visible BAND selection pool belongs to the adopted cohort cutoff, even during LOCK.
    std::shared_ptr<const KirinAttackBandSummaryV2> cohort;
    Selection selection;
    ViewportClock clock;
    std::vector<KirinSnapshotEventKey> events;
    KirinAttackWaveformBatch waveform {}, preWaveform {};
    Request request;
    Cancel cancel;
    std::uint64_t token = 0;
    bool enabled = false, overlay = true;
    std::uint8_t band = 0, target = KIRIN_TARGET_POST;
    bool navigate (const KirinSnapshotHeader&, const std::vector<KirinSnapshotEventKey>&,
                   const KirinAttackWaveformBatch&, const KirinAttackWaveformBatch&,
                   double nowMs, bool realtime);
    bool summary (const KirinAttackBandSummaryV2&, double nowMs);
    bool single (const KirinAttackSingleSnapshotV2&, double nowMs);
    void tick (double nowMs);
    void advanceClock (double nowMs);
    void changedSelection();
    void changeBand (std::uint8_t);
    void begin();
    void retire();
    void goLive();
    void observeInput (bool active, double nowMs);
    bool needsSingle() const noexcept { return band == 0 || ! selection.live; }
    const KirinSnapshotHeader& navigationHeader() const noexcept { return navHeader; }
private:
    KirinSnapshotHeader navHeader {};
    std::shared_ptr<const KirinAttackBandSummaryV2> pending;
    std::optional<KirinSnapshotEventKey> requested;
    double appliedMs = -1;
    std::uint64_t nextRevision = 1;
    bool awaitingBandNavigation = false, realtimeInput = true, activeInput = false;
    void requestSelected();
    void apply (Presentation);
    void stamp();
    Presentation placeholder (std::uint8_t reason, bool selectedSingle = false) const;
};
}
