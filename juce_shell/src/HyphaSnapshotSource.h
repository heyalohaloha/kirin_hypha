#pragma once
#include "kirin_hypha_navigation_snapshot_ffi.h"
#include "kirin_hypha_attack_snapshot_ffi.h"
#include "kirin_hypha_attack_summary_v2_ffi.h"
#include "kirin_hypha_time_snapshot_ffi.h"
#include <vector>
class KirinHyphaProcessorBase;
namespace hypha::snapshots
{
// The lifecycle guard protects the handle; FFI data locks are bounded try-reads.
class Source
{
public:
    explicit Source (const KirinHyphaProcessorBase& p) : processor (p) {}
    uint8_t time (const KirinTimeSnapshotRequestV2&, KirinTimeSnapshotV2&,
                  std::vector<KirinTimeHistoryEntryV2>& main,
                  std::vector<KirinTimeHistoryEntryV2>& psr, unsigned capacity, bool showPsr) const;
    uint8_t navigation (uint8_t target, KirinAttackNavigationV2&) const;
    uint8_t summary (uint8_t target, uint8_t band, KirinAttackBandSummaryV2&) const;
    uint8_t single (uint64_t token, KirinAttackSingleSnapshotV2&) const;
    uint8_t requestSingle (const KirinAttackSingleV2Request&, uint64_t&) const;
    uint8_t cancelSingle (uint64_t) const;
    uint8_t session (KirinMeterSessionV2&) const;
private:
    const KirinHyphaProcessorBase& processor;
};
}
