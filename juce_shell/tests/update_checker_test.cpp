#include "../src/update/UpdateChecker.h"
#include "../src/update/UpdateManifest.h"
#include "../src/update/UpdateServiceOwner.h"
#include <iostream>
#include <cstdlib>
#include <array>
#include <stdexcept>

namespace
{
using namespace hypha::update;
void require (bool value, const char* text)
{
    if (! value) { std::cerr << "FAIL: " << text << '\n'; std::exit (1); }
}
Snapshot settled (Checker& checker, std::uint64_t before)
{
    for (int i = 0; i < 500; ++i)
    {
        const auto state = checker.snapshot();
        if (! state.busy && state.revision > before) return state;
        juce::Thread::sleep (2);
    }
    require (false, "worker completed within fixture deadline");
    return {};
}
Snapshot request (Checker& checker, bool manual)
{
    const auto revision = checker.snapshot().revision;
    checker.request (manual);
    return settled (checker, revision);
}
Snapshot enable (Checker& checker, bool value)
{
    const auto token = checker.setEnabled (value);
    for (int i = 0; i < 500; ++i)
    {
        const auto state = checker.snapshot();
        if (! state.busy && state.preferenceCompletion >= token) return state;
        juce::Thread::sleep (2);
    }
    require (false, "own preference action completed");
    return {};
}
void seed (const juce::File& root, const Store::State& state)
{
    Store store (root);
    require (store.acquire() && store.save (state), "seed disposable cache");
}
}

int main (int argc, char** argv)
{
    using namespace hypha::update;
    const auto fixture = juce::JSON::parse (juce::File (__FILE__)
        .getSiblingFile ("fixtures/update_manifest_fixture.json").loadFileAsString());
    const auto wire = fixture["cases"][0]["wire"].toString();
    const auto key = fixture["publicKey"].toString();
    const auto initialNow = static_cast<juce::int64> (fixture["now"]);
    require (wire.isNotEmpty() && key.isNotEmpty(), "signed test-only fixture exists");
    if (argc == 3 && juce::String (argv[1]) == "--manual-worker")
    {
        Configuration child;
        child.root = juce::File (juce::String (argv[2]));
        child.version = "1.1.50"; child.platform = "macos"; child.publicKey = key;
        child.now = [initialNow] { return initialNow; };
        child.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
        {
            require (child.root.getChildFile ("fixture-attempts.log").appendText ("one\n", false, false, "\n"),
                     "record the mock HTTP attempt in the disposable root");
            juce::Thread::sleep (100);
            return wire;
        };
        Checker checker (child); request (checker, true); return 0;
    }
   #if JUCE_WINDOWS
    const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory);
   #else
    const juce::File parent ("/private/tmp"); // Avoid macOS /var symlink; never touch real user data.
   #endif
    const auto root = parent.getChildFile ("hypha-update-checker-" + juce::Uuid().toString());
    require (root.createDirectory(), "create disposable root");
    const std::array<juce::File, 4> protectedFiles {
        root.getChildFile ("KirinOS/identity.json"),
        root.getChildFile ("KirinOS/plugin_data/reference/v2/current.json"),
        root.getChildFile ("KirinOS/plugin_data/reference/v2/lease.json"),
        root.getChildFile ("KirinOS/plugin_data/records/fixture.json") };
    constexpr auto protectedBytes = "{\"fixture\":true,\"license\":\"os\",\"keep\":\"unchanged\"}";
    for (const auto& file : protectedFiles)
        require (file.getParentDirectory().createDirectory() && file.replaceWithText (protectedBytes),
                 "create disposable entitlement, Reference lease and Record sentinels");
    std::atomic<int> calls { 0 };
    std::atomic<juce::int64> now { initialNow };
    Configuration config;
    config.root = root.getChildFile ("normal");
    config.version = "1.1.50"; config.sourceCommit = juce::String::repeatedString ("b", 40);
    config.platform = "macos"; config.publicKey = key;
    config.now = [&] { return now.load(); };
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
    { ++calls; return wire; };
    {
        Checker checker (config);
        require (! request (checker, false).enabled && calls == 0, "default OFF makes no HTTP request");
        const auto success = request (checker, true);
        require (success.status == Status::available && calls == 1
            && success.nextAttemptAt == initialNow + checkIntervalSeconds,
            "manual success reserves exactly one common daily budget");
        require (checker.claimNotification() && ! checker.claimNotification(), "one passive notification ticket");
        for (int i = 0; i < 30; ++i) request (checker, i % 2 == 0);
        require (calls == 1, "opening editors/manual clicks cannot repeat HTTP");
        require (enable (checker, true).enabled && calls == 1, "opt-in shares manual budget");
        now = initialNow + checkIntervalSeconds - 1;
        request (checker, true);
        require (calls == 1, "24-hour boundary minus one second is blocked");
    }
    {
        Checker restarted (config);
        request (restarted, true);
        require (calls == 1 && ! restarted.claimNotification(), "restart preserves budget and notification dismissal");
        now = initialNow - 10;
        request (restarted, true);
        require (calls == 1, "clock rollback does not refund budget");
        now = initialNow + checkIntervalSeconds;
        require (request (restarted, true).status == Status::failed && calls == 2,
                 "exact daily boundary allows one attempt; expired signed information fails");
    }
    now = initialNow;
    config.root = root.getChildFile ("failure");
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String> { ++calls; return {}; };
    {
        Checker checker (config);
        require (request (checker, true).status == Status::failed, "failed explicit check is reported");
    }
    const auto failureCount = calls.load();
    {
        Checker checker (config);
        require (request (checker, true).status == Status::failed && calls == failureCount,
                 "failure followed by restart does not retry");
    }
    config.root = root.getChildFile ("unsigned");
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String> { ++calls; return "{}"; };
    {
        Checker checker (config);
        require (request (checker, true).status == Status::failed && ! checker.claimNotification(),
                 "unsigned response never advertises a version");
    }
    config.root = root.getChildFile ("missing-key"); config.publicKey.clear();
    {
        Checker checker (config);
        const auto before = calls.load();
        require (request (checker, true).status == Status::unavailable && calls == before
            && ! config.root.exists(), "unprovisioned trust means zero HTTP and zero production storage");
    }
    config.publicKey = key;
    config.root = root.getChildFile ("invalid-key"); config.publicKey = "invalid";
    {
        Checker checker (config);
        const auto before = calls.load();
        require (request (checker, true).status == Status::unavailable && calls == before,
                 "nonempty invalid key never reaches HTTP");
    }
    config.publicKey = key;
    config.root = root.getChildFile ("cancelled");
    std::atomic<bool> fetching { false }, mayReturn { false };
    config.fetch = [&] (const std::atomic<bool>& cancel) -> std::optional<juce::String>
    {
        ++calls; fetching = true;
        while (! cancel.load() || ! mayReturn.load()) juce::Thread::sleep (1);
        return {};
    };
    {
        Checker checker (config);
        const auto manualToken = checker.request (true);
        for (int i = 0; i < 500 && ! fetching; ++i) juce::Thread::sleep (1);
        require (fetching, "cancellable mock communication began");
        const auto offToken = checker.setEnabled (false);
        const auto oldJob = checker.snapshot();
        require (oldJob.preferenceCompletion < offToken && oldJob.manualCompletion < manualToken,
                 "an active older job cannot acknowledge queued preference/manual actions");
        mayReturn = true;
        for (int i = 0; i < 500 && checker.snapshot().preferenceCompletion < offToken; ++i)
            juce::Thread::sleep (2);
        const auto completed = checker.snapshot();
        require (completed.preferenceCompletion == offToken && completed.preferenceSaved
            && ! completed.preferenceValue && ! completed.enabled
            && completed.manualCompletion >= manualToken,
                 "OFF is acknowledged only after its own persisted worker result");
    }
    const auto cancelCount = calls.load();
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String> { ++calls; return wire; };
    {
        Checker checker (config);
        request (checker, true);
        require (calls == cancelCount, "OFF cancellation retains reservation across restart");
    }
    for (const auto scenario : { "current", "development", "format", "rollback" })
    {
        config.root = root.getChildFile (scenario);
        config.version = juce::String (scenario) == "current" ? "1.1.51" : "1.1.52";
        config.sourceCommit = juce::String::repeatedString ("a", 40);
        config.format = juce::String (scenario) == "format" ? "AUv3" : "VST3";
        const auto expected = juce::String (scenario) == "current" ? Status::current
            : juce::String (scenario) == "format" ? Status::unavailable
            : juce::String (scenario) == "rollback" ? Status::failed : Status::development;
        if (juce::String (scenario) == "rollback")
        { Store::State state; state.lastSequence = 6; seed (config.root, state); }
        Checker checker (config);
        require (request (checker, true).status == expected && ! checker.claimNotification(),
                 "same/newer build, unavailable format and sequence rollback are not updates");
    }
    config.root = root.getChildFile ("damaged");
    require (config.root.createDirectory() && config.root.getChildFile ("state.json").replaceWithText ("{"),
             "make damaged disposable state");
    {
        Checker checker (config);
        const auto before = calls.load();
        require (request (checker, true).status == Status::unavailable && calls == before,
                 "corrupt cache fails closed instead of repeating HTTP");
    }
    config.root = root.getChildFile ("same-sequence"); config.version = "1.1.50"; config.format = "VST3";
    Store::State prior;
    prior.wire = wire; prior.lastSequence = 5;
    prior.lastAttempt = prior.lastSuccess = initialNow - checkIntervalSeconds;
    seed (config.root, prior);
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
    { ++calls; return fixture["cases"][3]["wire"].toString(); }; // signed withdrawal, same sequence
    {
        Checker checker (config);
        require (request (checker, true).status == Status::failed,
                 "a signed sequence cannot change its payload bytes");
        Store store (config.root); Store::State state;
        require (store.acquire() && store.read (state) && state.wire == wire,
                 "same sequence rejection retains the original signed wire");
    }
    config.root = root.getChildFile ("exception");
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
    { ++calls; throw std::runtime_error ("fixture transport failure"); };
    {
        Checker checker (config);
        require (request (checker, true).status == Status::failed, "worker exceptions cannot terminate a host");
        const auto before = calls.load(); request (checker, true);
        require (calls == before, "transport exception does not refund a daily reservation");
    }
    config.root = root.getChildFile ("preference-exception");
    {
        Checker checker (config);
        const auto result = enable (checker, true);
        require (result.enabled && result.preferenceSaved && result.preferenceValue
            && result.status == Status::failed,
            "a saved ON remains acknowledged ON even when subsequent HTTP throws");
        Store persisted (config.root); Store::State state;
        require (persisted.acquire() && persisted.read (state) && state.enabled
            && state.lastAttempt == initialNow,
            "exception view agrees with persisted ON and consumed daily reservation");
    }
    {
        const auto before = calls.load();
        Checker restarted (config);
        require (request (restarted, false).enabled && calls == before,
                 "saved ON is consistent after restart without another HTTP attempt");
        const auto result = enable (restarted, false);
        require (result.preferenceSaved && ! result.preferenceValue && ! result.enabled,
                 "OFF remains independently saved after transport exception");
    }
    for (const auto scenario : { "expired-available", "expired-current", "expired-withdrawn", "expired-development" })
    {
        now = initialNow;
        const bool withdrawn = juce::String (scenario).endsWith ("withdrawn");
        const auto signedWire = withdrawn ? fixture["cases"][3]["wire"].toString() : wire;
        const auto manifest = verifyManifest (signedWire, key, now.load(), 0);
        require (manifest.has_value(), "expiry fixture begins with a valid signed manifest");
        config.root = root.getChildFile (scenario);
        config.version = juce::String (scenario).endsWith ("current") ? "1.1.51"
            : juce::String (scenario).endsWith ("development") ? "1.1.52" : "1.1.50";
        config.sourceCommit = juce::String::repeatedString ("a", 40);
        config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
        { ++calls; return signedWire; };
        Checker checker (config);
        request (checker, true);
        const auto before = calls.load();
        now = manifest->expiresAt - 1;
        require (checker.snapshot().version.isNotEmpty(), "information is valid until expiry boundary");
        now = manifest->expiresAt;
        const auto expired = checker.snapshot();
        require (expired.status == Status::unavailable && expired.version.isEmpty()
            && ! checker.claimNotification() && calls == before,
            "expiry invalidates all facts and delayed notification WITHOUT HTTP");
        require (expired.nextAttemptAt == initialNow + checkIntervalSeconds,
                 "expiry never shortens the persisted daily communication budget");
    }
    now = initialNow;
    config.version = "1.1.50";
    {
        std::weak_ptr<Checker> weak;
        { auto service = Checker::shared ("fixture-registry-only"); weak = service; }
        require (weak.expired(), "module static registry retains no worker for DLL detach");
        auto first = Checker::shared ("fixture-registry-only");
        auto second = Checker::shared ("fixture-registry-only");
        require (first == second, "expired weak entry is replaced so recreated owners still share");
    }
    config.root = root.getChildFile ("lifecycle");
    std::atomic<bool> lifetimeEntered { false }, lifetimeDrained { false };
    config.fetch = [&] (const std::atomic<bool>& cancel) -> std::optional<juce::String>
    {
        ++calls; lifetimeEntered = true;
        while (! cancel.load()) juce::Thread::sleep (1);
        lifetimeDrained = true; return {};
    };
    std::weak_ptr<Checker> weakService;
    {
        ServiceOwner processor ([&] (const juce::String&) {
            auto service = std::make_shared<Checker> (config); weakService = service; return service;
        });
        {
            auto editor = processor.get ("VST3"); editor->request (true);
            for (int i = 0; i < 500 && ! lifetimeEntered; ++i) juce::Thread::sleep (1);
            require (lifetimeEntered, "processor-owned communication fixture began");
        }
        require (! weakService.expired() && ! lifetimeDrained,
                 "closing the last editor neither joins nor destroys the processor worker");
    }
    require (weakService.expired() && lifetimeDrained,
             "processor teardown cancels and fully drains worker BEFORE module unload");
    config.root = root.getChildFile ("multiple-services");
    std::atomic<bool> entered { false };
    config.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
    { ++calls; entered = true; juce::Thread::sleep (100); return wire; };
    {
        Checker first (config), second (config);
        const auto before = calls.load(); const auto revision = first.snapshot().revision;
        first.request (true);
        for (int i = 0; i < 500 && ! entered; ++i) juce::Thread::sleep (1);
        require (entered, "first worker started a disposable mock request");
        request (second, true); settled (first, revision); request (second, true);
        require (calls == before + 1, "multiple module services share one attempt budget");
        require (static_cast<int> (first.claimNotification()) + static_cast<int> (second.claimNotification()) == 1,
                 "multiple services reserve only one notification ticket");
    }
    config.root = root.getChildFile ("contention-preserves-on");
    entered = false;
    std::atomic<bool> finishOwner { false };
    config.fetch = [&] (const std::atomic<bool>& cancelled) -> std::optional<juce::String>
    { ++calls; entered = true; while (! finishOwner && ! cancelled) juce::Thread::sleep (1); return wire; };
    {
        Checker first (config), second (config);
        first.setEnabled (true);
        for (int i = 0; i < 500 && ! entered; ++i) juce::Thread::sleep (1);
        require (entered, "ON owner holds one reserved fixture communication");
        const auto before = calls.load();
        const auto contender = request (second, false);
        require (contender.enabled && contender.nextAttemptAt >= initialNow + checkIntervalSeconds
            && calls == before,
            "contending new module observes saved ON and schedules daily wake WITHOUT retry/HTTP");
        finishOwner = true;
        for (int i = 0; i < 500 && first.snapshot().busy; ++i) juce::Thread::sleep (1);
        require (second.snapshot().enabled,
                 "contending service remains ON without a second request after owner completes");
    }
    config.root = root.getChildFile ("contention-before-preference-commit");
    {
        Store owner (config.root);
        require (owner.acquire(), "fixture owner enters before committing initial ON");
        std::atomic<int> workerClocks { 0 };
        const auto testThread = std::this_thread::get_id();
        auto deferred = config;
        deferred.now = [&] {
            // Jump only after the contention job's three clock reads. Its daily
            // wake becomes one real second, without adding a production time override.
            return initialNow + (std::this_thread::get_id() != testThread && ++workerClocks >= 4
                ? checkIntervalSeconds : 0);
        };
        std::atomic<int> deferredCalls { 0 };
        deferred.fetch = [&] (const std::atomic<bool>&) -> std::optional<juce::String>
        { ++deferredCalls; return wire; };
        Checker contender (deferred);
        require (! request (contender, false).enabled && deferredCalls == 0,
                 "initial contention may observe OFF but cannot make an HTTP request");
        Store::State committed;
        committed.enabled = true; committed.lastAttempt = initialNow + checkIntervalSeconds;
        require (owner.save (committed), "owner commits ON after contender's first observation");
        owner.release();
        for (int i = 0; i < 1500 && ! contender.snapshot().enabled; ++i) juce::Thread::sleep (2);
        require (contender.snapshot().enabled && deferredCalls == 0,
                 "daily state refresh recovers delayed ON without replaying an action or HTTP");
        require (contender.snapshot().preferenceCompletion == 0,
                 "state refresh does not invent a preference-action acknowledgement");
    }
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        config.root = root.getChildFile ("dispatch-close-" + juce::String (attempt));
        config.fetch = [&] (const std::atomic<bool>& cancelled) -> std::optional<juce::String>
        {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            while (! cancelled && juce::Time::getMillisecondCounterHiRes() < deadline) juce::Thread::sleep (1);
            require (cancelled, "teardown cancellation cannot be reset by queued job dispatch");
            return {};
        };
        Checker closing (config);
        closing.request (true);
        if (attempt % 2 == 0) std::this_thread::yield();
    }
    const auto shared = root.getChildFile ("multiple-processes");
    std::array<juce::ChildProcess, 4> children;
    for (auto& child : children)
        require (child.start (juce::StringArray {
            juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
            "--manual-worker", shared.getFullPathName() }), "start isolated mock-check process");
    for (auto& child : children)
        require (child.waitForProcessToFinish (10000) && child.getExitCode() == 0,
                 "isolated mock-check processes finish without duplicate attempts");
    require (shared.getChildFile ("fixture-attempts.log").loadFileAsString() == "one\n",
             "four processes issue exactly one mock HTTP attempt");
    for (const auto& file : protectedFiles)
        require (file.loadFileAsString() == protectedBytes,
                 "update success/failure/cancel/restart never alter OS integration files");
    require (root.deleteRecursively(), "remove only this disposable fixture");
    std::cout << "Update checker daily budget, OFF, failure, restart, cancellation, signature and cache PASS\n";
    return 0;
}
