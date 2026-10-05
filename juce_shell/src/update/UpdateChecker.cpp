#include "UpdateChecker.h"
#include "UpdateManifest.h"
#include "UpdateTransport.h"
#include "UpdateBuildTrust.h"
#include "HyphaBuildIdentity.h"
#include <map>
#if JUCE_MAC
 #include <pwd.h>
 #include <unistd.h>
 #include <array>
#endif

#ifndef HYPHA_UPDATE_LOADED_VERSION
 #define HYPHA_UPDATE_LOADED_VERSION JucePlugin_VersionString
#endif

namespace hypha::update
{
namespace
{
Configuration shippingConfiguration()
{
    Configuration value;
    value.version = HYPHA_UPDATE_LOADED_VERSION;
    value.sourceCommit = juce::String (HYPHA_SOURCE_STATE) == "clean source"
        ? HYPHA_SOURCE_COMMIT : "unverified";
    value.publicKey = productionPublicKey();
   #if JUCE_WINDOWS
    value.platform = "windows";
   #else
    value.platform = "macos";
   #endif
    value.fetch = fetchOfficialManifest;
    value.now = [] { return juce::Time::currentTimeMillis() / 1000; };
    return value;
}

std::optional<juce::File> shippingRoot()
{
   #if JUCE_MAC
    // Resolve the actual OS account home, not a per-host sandbox HOME/container.
    // If sandbox permissions forbid this ONE shared location, skip HTTP; never fall
    // back to a second root that could give another DAW a fresh communication budget.
    std::array<char, 16384> buffer {};
    struct passwd account {};
    struct passwd* result = nullptr;
    if (getpwuid_r (getuid(), &account, buffer.data(), buffer.size(), &result) != 0
        || result == nullptr || account.pw_dir == nullptr
        || ! juce::File::isAbsolutePath (account.pw_dir)) return {};
    return juce::File (account.pw_dir).getChildFile ("Library/Application Support/Kirin Hypha/UpdateCheck/v1");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kirin Hypha/UpdateCheck/v1");
   #endif
}

Status classify (const Manifest& manifest, const Configuration& config)
{
    if (manifest.withdrawn) return Status::withdrawn;
    if (! manifest.platforms.contains (config.platform) || ! manifest.formats.contains (config.format))
        return Status::unavailable;
    if (! validSemVer (config.version)) return Status::development;
    const int order = compareVersions (manifest.version, config.version);
    if (order > 0) return Status::available;
    // Equal version numbers are not proof that an untagged/development build is official.
    if (order < 0 || manifest.sourceCommit != config.sourceCommit) return Status::development;
    return Status::current;
}
}

Checker::Checker (Configuration config) : configuration (std::move (config))
{
    worker = std::thread ([this] { run(); });
}

Checker::~Checker()
{
    // Serialize with dispatch's cancellation reset. Publishing cancellation
    // before acquiring this lock lets a queued job overwrite it with false.
    { std::lock_guard<std::mutex> lock (mutex); stop = true; cancelled.store (true); }
    wake.notify_one();
    if (worker.joinable()) worker.join();
}

std::shared_ptr<Checker> Checker::shared (const juce::String& format)
{
    static std::mutex factoryMutex;
    // A live processor retains the service across editor closes. Static destruction
    // must NEVER own a worker: DLL detach holds the Windows loader lock.
    static std::map<juce::String, std::weak_ptr<Checker>> instances;
    const std::lock_guard<std::mutex> lock (factoryMutex);
    if (auto existing = instances.find (format); existing != instances.end())
        if (auto live = existing->second.lock()) return live;
    auto config = shippingConfiguration();
    config.format = format;
    auto result = std::make_shared<Checker> (std::move (config));
    instances.insert_or_assign (format, result);
    return result;
}

Snapshot Checker::snapshot() const
{
    const auto now = readClock();
    const std::lock_guard<std::mutex> lock (mutex);
    auto result = view;
    // Expiry changes only presentation, never the persisted communication budget.
    // No disk/HTTP/cryptography is needed on a timer or a delayed notification.
    if (result.expiresAt > 0 && (now < result.publishedAt || now >= result.expiresAt))
    {
        result.status = Status::unavailable;
        result.version.clear();
    }
    return result;
}

std::int64_t Checker::readClock() const noexcept
{
    try { return configuration.now ? configuration.now() : 0; }
    catch (...) { return 0; }
}

void Checker::publish (const Snapshot& state)
{
    const std::lock_guard<std::mutex> lock (mutex);
    view = state;
    view.revision++;
}

std::uint64_t Checker::request (bool manual)
{
    std::uint64_t token = 0;
    { const std::lock_guard<std::mutex> lock (mutex);
      pending = true; manualPending = manualPending || manual;
      if (manual) pendingManualToken = token = ++nextAction; }
    wake.notify_one();
    return token;
}

std::uint64_t Checker::setEnabled (bool enabled)
{
    std::uint64_t token = 0;
    { const std::lock_guard<std::mutex> lock (mutex);
      preferencePending = enabled; pending = true;
      pendingPreferenceToken = token = ++nextAction;
      if (! enabled) manualPending = false; }
    // An OFF action cancels an in-flight check without refunding its reserved budget.
    if (! enabled) { cancelled.store (true); notification.store (false); }
    wake.notify_one();
    return token;
}

bool Checker::claimNotification()
{
    const bool fresh = snapshot().status == Status::available;
    return notification.exchange (false) && fresh;
}

void Checker::run()
{
    std::unique_lock<std::mutex> lock (mutex);
    while (! stop)
    {
        // No periodic polling. One scheduled wake per permitted automatic attempt.
        if (! pending)
        {
            if ((view.enabled || refreshRequired) && view.nextAttemptAt > 0 && configuration.now)
            {
                const auto delay = std::clamp<std::int64_t> (
                    view.nextAttemptAt - readClock(), 1, checkIntervalSeconds);
                if (! wake.wait_for (lock, std::chrono::seconds (delay), [this] { return stop || pending; }))
                    pending = true;
            }
            else wake.wait (lock, [this] { return stop || pending; });
        }
        if (stop) break;
        const bool manual = manualPending;
        const auto preference = preferencePending;
        const auto manualToken = pendingManualToken, preferenceToken = pendingPreferenceToken;
        pending = manualPending = false;
        preferencePending.reset();
        pendingManualToken = pendingPreferenceToken = 0;
        cancelled.store (false);
        lock.unlock();
        try { perform (manual, preference, manualToken, preferenceToken); }
        catch (...)
        {
            // A failed updater must not terminate the host. Never refund an HTTP
            // reservation and never let automatic failures interrupt normal operation.
            const std::lock_guard<std::mutex> failedLock (mutex);
            const bool preferenceCommitted = preferenceToken != 0
                && view.preferenceCompletion >= preferenceToken;
            view.busy = false; view.status = Status::failed; ++view.revision;
            view.manualCompletion = std::max (view.manualCompletion, manualToken);
            view.preferenceCompletion = std::max (view.preferenceCompletion, preferenceToken);
            // perform publishes a successful setting save before it can start HTTP.
            // Preserve that acknowledgement if a later boundary throws.
            if (preferenceToken != 0 && ! preferenceCommitted)
            { view.preferenceSaved = false; view.preferenceValue = preference.value_or (false); }
            view.nextAttemptAt = readClock() + checkIntervalSeconds;
        }
        lock.lock();
    }
}

void Checker::perform (bool manual, std::optional<bool> preference,
                       std::uint64_t manualToken, std::uint64_t preferenceToken)
{
    auto result = snapshot();
    bool preferenceSaved = false;
    result.busy = true;
    publish (result);
    const auto finish = [&]
    {
        const auto current = snapshot();
        result.busy = false; result.revision = current.revision;
        // An older HTTP job must never acknowledge a later queued UI action.
        // Coalesced actions finish together only when their own worker job has completed.
        result.manualCompletion = std::max (current.manualCompletion, manualToken);
        result.preferenceCompletion = std::max (current.preferenceCompletion, preferenceToken);
        result.preferenceSaved = preferenceToken != 0 ? preferenceSaved : current.preferenceSaved;
        result.preferenceValue = preferenceToken != 0 ? preference.value_or (false) : current.preferenceValue;
        publish (result);
    };
    // Production trust must be deliberately provisioned. Missing key means NO HTTP and
    // no production files are touched just by opening an editor/test fixture.
    if (! validPublicKey (configuration.publicKey) || ! configuration.now || ! configuration.fetch)
    {
        result.status = Status::unavailable;
        finish();
        return;
    }
    if (configuration.root == juce::File())
    {
        const auto root = shippingRoot();
        if (! root) { result.status = Status::unavailable; finish(); return; }
        configuration.root = *root;
    }
    Store store (configuration.root);
    if (! store.acquire())
    {
        refreshRequired = true;
        // Another module/host may own the check. No waiting, retries or extra request.
        // Read its already-committed atomic setting/budget once. Otherwise a new
        // service would falsely report OFF and never schedule its next daily wake.
        Store::State observed;
        const auto now = readClock();
        if (store.observe (observed))
        {
            result.enabled = observed.enabled;
            result.checkedAt = observed.lastAttempt;
            const auto cached = verifyManifest (observed.wire, configuration.publicKey, now, observed.lastSequence);
            result.status = cached && observed.lastSuccess == observed.lastAttempt
                ? classify (*cached, configuration) : Status::unavailable;
            result.version = cached ? cached->version : juce::String();
            result.publishedAt = cached ? cached->publishedAt : 0;
            result.expiresAt = cached ? cached->expiresAt : 0;
            result.nextAttemptAt = std::max (now + checkIntervalSeconds,
                                            observed.lastAttempt + checkIntervalSeconds);
        }
        else
        {
            result.status = Status::unavailable;
            result.nextAttemptAt = now + checkIntervalSeconds;
        }
        if (manual) result.status = Status::unavailable;
        finish();
        return;
    }
    Store::State state;
    if (! store.read (state))
    {
        refreshRequired = true;
        result.status = Status::unavailable;
        result.nextAttemptAt = readClock() + checkIntervalSeconds;
        finish();
        return;
    }
    refreshRequired = false; // Only the exclusion owner's successful read is authoritative.
    if (preference)
    {
        if (! *preference) notification.store (false);
        state.enabled = *preference;
        if (! store.save (state)) { result.status = Status::unavailable; finish(); return; }
        preferenceSaved = true;
        result.enabled = state.enabled;
        result.preferenceSaved = true;
        result.preferenceValue = *preference;
        result.preferenceCompletion = preferenceToken;
        // Commit the known setting result independently of the optional HTTP result.
        // A transport exception cannot turn a persisted ON into a reported OFF.
        publish (result);
    }
    result.enabled = state.enabled;
    const auto now = readClock();
    if (now <= 0) { result.status = Status::unavailable; finish(); return; }
    result.checkedAt = state.lastAttempt;
    result.nextAttemptAt = state.lastAttempt == 0 ? 0 : state.lastAttempt + checkIntervalSeconds;
    auto cached = verifyManifest (state.wire, configuration.publicKey, now, state.lastSequence);
    if (cached)
    {
        result.status = state.lastSuccess == state.lastAttempt
            ? classify (*cached, configuration) : Status::failed;
        result.version = cached->version;
        result.publishedAt = cached->publishedAt;
        result.expiresAt = cached->expiresAt;
    }
    else
    {
        result.status = state.lastAttempt == 0 ? Status::unchecked : Status::failed;
        result.version.clear();
        result.publishedAt = result.expiresAt = 0;
    }
    if ((manual || state.enabled) && now >= result.nextAttemptAt && ! cancelled.load())
    {
        // Commit the lease timestamp BEFORE starting HTTP. Failed/cancelled checks,
        // worker crashes and DAW restarts all consume this identical 24-hour budget.
        state.lastAttempt = now;
        if (! store.save (state)) { result.status = Status::unavailable; finish(); return; }
        result.checkedAt = now;
        result.nextAttemptAt = now + checkIntervalSeconds;
        auto response = configuration.fetch (cancelled);
        auto verified = response && ! cancelled.load()
            ? verifyManifest (*response, configuration.publicKey, now, state.lastSequence)
            : std::optional<Manifest>();
        // A sequence is immutable; changing signed content under the same sequence is
        // rejected even if the signature is valid. Old cached facts are not called current.
        if (verified && (verified->sequence > state.lastSequence || state.wire.isEmpty()
                        || *response == state.wire))
        {
            state.wire = *response;
            state.lastSequence = verified->sequence;
            state.lastSuccess = now;
            if (store.save (state))
            {
                result.version = verified->version;
                result.status = classify (*verified, configuration);
                result.publishedAt = verified->publishedAt;
                result.expiresAt = verified->expiresAt;
            }
            else result.status = Status::unavailable;
        }
        else result.status = Status::failed;
    }
    if ((manual || state.enabled) && result.status == Status::available
        && state.dismissedVersion != result.version)
    {
        // Persist before offering the UI ticket: PRE/POST, different formats, DAWs and
        // restarts cannot each toast the same release. The info menu always retains it.
        state.dismissedVersion = result.version;
        if (store.save (state)) notification.store (true);
    }
    finish();
}
}
