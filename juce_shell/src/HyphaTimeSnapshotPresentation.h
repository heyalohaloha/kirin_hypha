#pragma once

#include "kirin_hypha_time_snapshot_ffi.h"

#include <cstdint>
#include <vector>

namespace hypha::time_snapshot
{
enum class Metric : unsigned { momentary, shortTerm, truePeak, psr, plr, correlation };

struct Component
{
    KirinTimeComponentV2 facts {};
    std::vector<KirinTimeHistoryEntryV2> history;
    double deadlineMs = 0.0;

    bool currentAvailable (Metric) const noexcept;
    double value (Metric) const noexcept;
};

// An accepted packet owns both components. No scalar is reconstructed from history, and no
// acquisition or measurement occurs while advancing, painting or freezing this presentation.
class Presentation
{
public:
    bool apply (const KirinTimeSnapshotV2&,
                std::vector<KirinTimeHistoryEntryV2> mainHistory,
                std::vector<KirinTimeHistoryEntryV2> psrHistory,
                double pollStartedMs, double nowMs, bool showPsr);
    bool advance (double nowMs) noexcept;
    bool observeInput (bool active) noexcept;
    void selectMainTarget (std::uint8_t target) noexcept;
    void retire (bool localSourceChanged) noexcept;

    bool available() const noexcept { return havePacket; }
    bool psrVisible() const noexcept { return showPsrLane; }
    const KirinTimeSnapshotV2& packet() const noexcept { return accepted; }
    const Component& main() const noexcept { return mainComponent; }
    const Component& psr() const noexcept { return psrComponent; }
    std::uint64_t revision() const noexcept { return presentationRevision; }
    double normalizedX (std::uint64_t observed) const noexcept;

private:
    KirinTimeSnapshotV2 accepted {};
    Component mainComponent, psrComponent;
    std::uint64_t presentationRevision = 0;
    bool havePacket = false, showPsrLane = false;
    bool knownInactive = false, haveStopFence = false;
    KirinTimeSourceSpanV2 stoppedSource {};
    std::uint64_t stoppedCutoff = 0;
};

bool sameSpan (const KirinTimeSourceSpanV2&, const KirinTimeSourceSpanV2&) noexcept;
bool connects (const KirinTimeHistoryEntryV2& previous,
               const KirinTimeHistoryEntryV2& current, unsigned rangeIndex) noexcept;
unsigned rangeIndex (Metric) noexcept;
}
