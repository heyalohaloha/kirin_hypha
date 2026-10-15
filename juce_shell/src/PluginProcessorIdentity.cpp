#include "PluginProcessor.h"
#include "kirin_hypha_identity_v2.h"

void KirinHyphaProcessorBase::enableWritesNow()
{
    // B-070 + Logic stopped-state fix: message-thread one-shot. Applies the FFI contract order create -> set_license
    // (done in prepareToPlay) -> set_identity -> enable_*_writes. processBlock still takes the
    // fast path; when the host is stopped, prepareToPlay enables after a short state-restore grace
    // period so PRE/POST discovery does not depend on audible playback.
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || writesEnabled.load (std::memory_order_acquire))
        return;

    // set_identity BEFORE enable: empty keys -> the FFI generates fresh UUIDs and writes
    // them back; restored keys -> reused. A set_identity after enable would not propagate
    // (the io_thread snapshots identity at enable), hence this single ordered point.
    kirin_hypha_set_identity (hyphaHandle,
                              persistInstanceId.toRawUTF8(),
                              persistProjectUuid.toRawUTF8(),
                              persistDawSessionUuid.toRawUTF8(),
                              persistName.toRawUTF8());

    // Role selects the plugin_data role: PRE (Watch pre.json + Record) vs POST (post.json
    // Δ via select_target_pre). enable_pre_writes / enable_post_writes are mutually
    // exclusive and idempotent in the FFI.
    if (role == Role::Post)
    {
        kirin_hypha_enable_post_writes (hyphaHandle);
    }
    else
    {
        kirin_hypha_enable_pre_writes (hyphaHandle);
    }

    // Read back the final (restored or freshly generated) identity so getStateInformation
    // persists it across DAW save/load.
    KirinIdentityV2 id {};
    if (! kirin_hypha_get_identity_v2 (hyphaHandle, &id))
        return; // No complete, claimed identity: do not publish shell transports.
    const auto resolvedInstance = juce::String::fromUTF8 (id.instance_id);
    if (resolvedInstance.isEmpty())
        return; // Identity IO failed: do not publish an unclaimed Watch/live transport.
    if (role == Role::Post && persistInstanceId.isNotEmpty()
        && persistInstanceId != resolvedInstance)
    {
        persistPairName.clear();
        persistPairProjectHash.clear();
        persistPairInstanceId.clear();
    }
    persistInstanceId     = juce::String::fromUTF8 (id.instance_id);
    persistProjectUuid    = juce::String::fromUTF8 (id.project_uuid);
    persistDawSessionUuid = juce::String::fromUTF8 (id.daw_session_uuid);
    persistName           = juce::String::fromUTF8 (id.name);

    if (role == Role::Post)
        restorePersistedPairUnderHandleLock();

    configureWorkTransports();
    configureReferenceAudition();

    writesEnabled.store (true, std::memory_order_release);
    analysisApplication.engineReady();
    serviceRequestedAnalysisUnderHandleLock();
    startPreparedFormatServices();
}
