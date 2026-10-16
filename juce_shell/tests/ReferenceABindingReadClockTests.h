#pragma once
#include "TestTemporaryDirectory.h"
#include "TestPathBudget.h"
#include "reference_runtime_test_support.h"
#include "ReferenceCaptureLeaseFacts.h"
#include <stdexcept>

namespace
{
void verifyABindingReadClockContract()
{
    const auto root = hypha::tests::temporaryParent().getNonexistentChildFile ("binding-read-clock", {}, false);
    require (root.createDirectory(), "read-clock fixture directory exists");
    const ref::RuntimeIdentity identity { "read-clock-runtime", workId, 42 };
    const juce::String recording = "55555555-5555-4555-8555-555555555555";
    constexpr std::int64_t beforeRead = 1'000'000, afterRead = beforeRead + 1;
    unsigned clockCalls = 0;
    ref::RuntimeABindingRepository repository (root, [&] { ++clockCalls; return afterRead; });
    const auto file = repository.bindingFile (identity.runtimeInstanceId);
    const auto publish = [&] (std::int64_t issued, std::int64_t expiry) {
        require (writeJson (file, makeRuntimeABinding (identity, recording, issued, expiry)),
                 "the disposable publisher atomically writes an actual binding");
    };
    publish (afterRead, afterRead + 9'000);
    require (! repository.load (identity, beforeRead),
             "the fixed-time boundary rejects a receipt issued after the caller's old T0");
    require (repository.loadCurrent (identity).has_value() && clockCalls == 1,
             "a normal T1 update after T0 is accepted using the post-read clock without a tolerance");
    publish (afterRead + 1, afterRead + 9'001);
    require (! repository.loadCurrent (identity), "even one millisecond in the real post-read future is rejected");
    publish (afterRead - 9'001, afterRead - 1);
    require (! repository.loadCurrent (identity), "a binding expired at the read boundary is rejected");
    publish (afterRead - 9'000, afterRead);
    require (repository.loadCurrent (identity).has_value(), "the exact inclusive expiry boundary is retained");
    publish (afterRead, afterRead + 10'001);
    require (! repository.loadCurrent (identity), "the existing ten-second maximum lease is not extended");

    publish (afterRead, afterRead + 9'000);
    unsigned snapshotClockCalls = 0;
    ref::RuntimeABindingRepository snapshotReader (root, [&] {
        ++snapshotClockCalls;
        require (hypha::storage::writeText (file, "invalid replacement after parsing"), "the test changes the file at the clock boundary");
        return afterRead;
    });
    const auto held = snapshotReader.loadCurrent (identity);
    require (held && held->issuedAtMs == afterRead && snapshotClockCalls == 1,
             "the clock runs once after parsing and validation consumes that same captured JSON snapshot");
    const auto callsBeforeInvalid = clockCalls;
    require (! repository.loadCurrent (identity) && clockCalls == callsBeforeInvalid,
             "invalid JSON is rejected before sampling the read clock");
    require (file.deleteFile() && ! repository.loadCurrent (identity) && clockCalls == callsBeforeInvalid,
             "a missing file never reuses a previous receipt or samples the clock");
    publish (afterRead, afterRead + 9'000);
    require (repository.loadCurrent (identity).has_value(), "a valid publication recovers after missing or malformed files");
    ref::RuntimeABindingRepository negativeClock (root, [] { return std::int64_t { -1 }; });
    require (! negativeClock.loadCurrent (identity), "an invalid clock does not accept a lease");
    bool emptyClockRejected = false;
    try { ref::RuntimeABindingRepository invalid (root, {}); }
    catch (const std::invalid_argument&) { emptyClockRejected = true; }
    require (emptyClockRejected, "an empty injected read clock is rejected explicitly");

    const auto captureFile = root.getChildFile ("capture.json");
    const auto publishCapture = [&] (std::int64_t captured, std::int64_t expiry) {
        auto* object = new juce::DynamicObject();
        object->setProperty ("binding_id", "99999999-9999-4999-8999-999999999999");
        object->setProperty ("recording_id", recording);
        object->setProperty ("captured_at_ms", captured);
        object->setProperty ("lease_expires_at_ms", expiry);
        object->setProperty ("daw_revision_id", "unchanged-real-pcm-revision");
        object->setProperty ("cue_pcm_sha256", "unchanged-real-pcm-hash");
        require (writeJson (captureFile, juce::var (object)), "a disposable capture receipt is published");
    };
    const auto captureFacts = [&] { return readLiveBlindCaptureFacts (root, captureFile, identity, [=] { return afterRead; }); };
    publishCapture (afterRead, afterRead + 1'500);
    require (captureFacts().current(), "both snapshots use their common post-read time, including a capture newer than old T0");
    publishCapture (afterRead - 1'500, afterRead - 1);
    require (! captureFacts().current(), "an expired capture lease is not hidden by a current authority lease");
    publishCapture (afterRead + 1, afterRead + 1'500);
    require (! captureFacts().current(), "a genuinely future capture is rejected at the shared read time");
    publishCapture (afterRead - 10'001, afterRead + 1'500);
    require (! captureFacts().current(), "the ten-second capture age remains strict");
    publishCapture (afterRead - 10'000, afterRead);
    require (captureFacts().current(), "exact capture age and lease expiry boundaries remain inclusive");
    publish (afterRead + 1, afterRead + 9'001);
    require (! captureFacts().current(), "capture checking never accepts a future authority");
    publish (afterRead - 9'001, afterRead - 1);
    require (! captureFacts().current(), "capture checking never accepts an expired authority");
    publish (afterRead, afterRead + 9'000);
    const auto capturedSnapshot = readLiveBlindCaptureFacts (root, captureFile, identity, [&] {
        require (hypha::storage::writeText (captureFile, "invalid after both snapshots"), "both reads precede the fixture clock callback");
        return afterRead;
    });
    require (capturedSnapshot.current() && capturedSnapshot.revision == "unchanged-real-pcm-revision",
             "capture and authority snapshots are retained without rereading after the shared clock sample");
    require (! captureFacts().current(), "a malformed capture cannot reuse the prior snapshot");

    const auto issued = juce::Time::currentTimeMillis();
    publish (issued, issued + 9'000);
    ref::RuntimeABindingRepository actualClock (root);
    require (! actualClock.load (identity, issued - 1) && actualClock.loadCurrent (identity).has_value(),
             "actual file bytes reproduce the old-before-publication rejection and the current-clock acceptance");
    require (hypha::tests::removeFixtureDirectory (root), "read-clock fixtures are removed");
    std::cout << "A binding read clock: post-read snapshot, legitimate renewal, strict future/expiry and recovery PASS\n";
}
}
