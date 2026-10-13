#include "ReaperPdcAdapter.h"
#include "ReaperChainReader.h"
#include "CallbackWallClock.h"
#include <pluginterfaces/base/funknown.h>
#include <juce_events/juce_events.h>
#include <mutex>
#if defined (_WIN32)
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::live_compare
{
namespace
{
// Public IReaperHostApplication ABI, REAPER SDK sdk/reaper_vst3_interfaces.h.
// Selector 6 is NOT a VST3 slot index. No fxdsp private structure is dereferenced.
class ReaperHost : public Steinberg::FUnknown
{
public:
    virtual void* PLUGIN_API getReaperApi (const char*) = 0;
    virtual void* PLUGIN_API getReaperParent (Steinberg::uint32) = 0;
    virtual void* PLUGIN_API reaperExtended (Steinberg::uint32, void*, void*, void*) = 0;
};
constexpr Steinberg::TUID reaperHostId = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
template <typename T> void resolve (ReaperHost& host, T& target, const char* name)
{ target = reinterpret_cast<T> (host.getReaperApi (name)); }
std::uint64_t processId() noexcept
{
   #if defined (_WIN32)
    return static_cast<std::uint64_t> (GetCurrentProcessId());
   #else
    return static_cast<std::uint64_t> (getpid());
   #endif
}
}
struct ReaperPdcAdapter::Impl
{
    std::mutex mutex; // non-RT setHost/service only
    Steinberg::FUnknown* host = nullptr;
    ~Impl() { if (host) host->release(); }
};
ReaperPdcAdapter::ReaperPdcAdapter() : impl (std::make_unique<Impl>()) {}
ReaperPdcAdapter::~ReaperPdcAdapter() = default;
void ReaperPdcAdapter::setHost (Steinberg::FUnknown* host)
{
    std::lock_guard<std::mutex> lock (impl->mutex);
    mailbox.invalidate();
    if (host) host->addRef();
    if (impl->host) impl->host->release();
    impl->host = host;
}
void ReaperPdcAdapter::service (bool interested)
{
    inspecting = false;
    if (!needsService()) return;
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (!manager || !manager->isThisTheMessageThread()) return;
    std::lock_guard<std::mutex> lock (impl->mutex);
    ReaperCallbackWitness before, after;
    ReaperPdcObservation reading;
    bool playing = false;
    mailbox.witness (before);
    inspecting = reaperMonitorRequired (needsService(), interested, before.playing);
    if (!inspecting) return;
    ReaperChainRestriction reason = ReaperChainRestriction::unavailable;
    void* raw = nullptr;
    if (impl->host && impl->host->queryInterface (reaperHostId, &raw) == Steinberg::kResultOk && raw)
    {
        auto* host = static_cast<ReaperHost*> (raw);
        ReaperChainApi api;
        resolve (*host, api.valid, "ValidatePtr2"); resolve (*host, api.count, "TrackFX_GetCount");
        resolve (*host, api.inputCount, "TrackFX_GetRecCount");
        resolve (*host, api.named, "TrackFX_GetNamedConfigParm"); resolve (*host, api.fxGuid, "TrackFX_GetFXGUID");
        resolve (*host, api.trackGuid, "GetTrackGUID"); resolve (*host, api.enabled, "TrackFX_GetEnabled");
        resolve (*host, api.offline, "TrackFX_GetOffline"); resolve (*host, api.playing, "GetPlayStateEx");
        resolve (*host, api.changes, "GetProjectStateChangeCount");
        resolve (*host, api.trackInfo, "GetMediaTrackInfo_Value"); resolve (*host, api.playRate, "Master_GetPlayRate");
        auto* project = host->getReaperParent (3);
        auto* track = host->getReaperParent (1);
        auto* take = host->getReaperParent (2);
        // Same process API address plus PID distinguishes process incarnations in these opaque
        // fingerprints. PRE owner/generation separately prevents any old mapping from re-entry.
        const auto process = processId()
            ^ static_cast<std::uint64_t> (reinterpret_cast<std::uintptr_t> (api.named));
        const auto first = readReaperChain (api, project, track, take, process);
        const auto second = readReaperChain (api, project, track, take, process, &reason);
        if (first.same (second) && project == host->getReaperParent (3)
            && track == host->getReaperParent (1) && take == host->getReaperParent (2))
        { reading = second; playing = true; }
        host->release();
    }
    limitation.store (reason);
    mailbox.witness (after);
    mailbox.publish (reading, before, after, playing, callbackWallNanos());
}
}
