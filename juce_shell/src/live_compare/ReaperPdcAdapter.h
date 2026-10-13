#pragma once
#include "ReaperPdcMailbox.h"
#include "ReaperChainStatus.h"
#include <memory>
namespace Steinberg { class FUnknown; }

namespace hypha::live_compare
{
// Owns host interfaces only on the non-RT side. The wrapper hook retains/releases context;
// the existing processor message-thread timer reads the public REAPER APIs.
class ReaperPdcAdapter
{
public:
    ReaperPdcAdapter();
    ~ReaperPdcAdapter();
    void setHost (Steinberg::FUnknown*);
    void prepare (bool enabled) noexcept { active.store (enabled); mailbox.invalidate(); }
    bool needsService() const noexcept { return active.load(); }
    bool monitoring() const noexcept { return inspecting; } // message thread only
    ReaperChainRestriction restriction() const noexcept { return limitation.load(); }
    void service (bool interested);
    void observe (BlockClock& block, double rate, bool usable) noexcept
    { if (active.load (std::memory_order_relaxed)) mailbox.observe (block, rate, usable); }
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> active { false };
    ReaperPdcMailbox mailbox;
    bool inspecting = false;
    std::atomic<ReaperChainRestriction> limitation { ReaperChainRestriction::none };
};
}
