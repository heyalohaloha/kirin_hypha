#pragma once

// Only the Reference runtime test target observes commits or requests a one-shot
// worker refresh. Shipping has no test call, storage, lock, callback or class field.
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
 #include <cstdint>
namespace hypha::reference_audition
{
struct ReferenceTestBindingReadFacts
{
    const char* stage = "none";
    const char* lastRejectedStage = "none";
    std::int64_t nowMs = 0, issuedMs = 0, expiresMs = 0, lastRejectedAtMs = 0;
    std::int64_t lastRejectedNowMs = 0, lastRejectedIssuedMs = 0, lastRejectedExpiresMs = 0;
    unsigned long osError = 0, lastRejectedOsError = 0;
    std::uint64_t count = 0, rejected = 0;
    bool accepted = false;
};
struct ReferenceTestBindingReadScope
{
    explicit ReferenceTestBindingReadScope (const void*);
    ~ReferenceTestBindingReadScope();
    const void* owner;
    ReferenceTestBindingReadScope* previous;
    ReferenceTestBindingReadFacts facts;
};
void stageReferenceTestBindingRead (const char*) noexcept;
void errorReferenceTestBindingRead (unsigned long) noexcept;
void valuesReferenceTestBindingRead (std::int64_t, std::int64_t, std::int64_t) noexcept;
void acceptReferenceTestBindingRead() noexcept;
ReferenceTestBindingReadFacts referenceTestBindingReadFacts (const void*) noexcept;
void stageReferenceTestWorker (const void*, const char*) noexcept;
void stageCurrentReferenceTestWorker (const char*) noexcept;
const char* referenceTestWorkerStage (const void*) noexcept;
void notifyReferenceTestCommit() noexcept;
bool pauseReferenceTestCaptureWorker (const void*, int timeoutMs);
void beforeReferenceTestCaptureService (const void*);
void resumeReferenceTestCaptureWorker (const void*) noexcept;
std::uint64_t requestReferenceTestWorkspaceRefresh (const void*) noexcept;
bool consumeReferenceTestWorkspaceRefresh (const void*) noexcept;
bool referenceTestWorkspaceRefreshConsumed (std::uint64_t) noexcept;
void cancelReferenceTestWorkspaceRefresh (const void*, std::uint64_t ticket = 0) noexcept;
struct ReferenceTestCaptureResetFacts { const char* reason = "none"; std::int64_t atMs = 0; std::uint64_t count = 0;
    const char* previousReason = "none"; std::int64_t previousAtMs = 0; };
struct ReferenceTestAuthorityFacts
{
    std::int64_t nowMs = 0, issuedMs = 0, expiresMs = 0, maxGapMs = 0;
    std::int64_t lastInvalidMs = 0, lastInvalidIssuedMs = 0, lastInvalidExpiresMs = 0;
    std::uint64_t count = 0, invalidCount = 0;
    bool present = false, valid = false, local = false;
};
void observeReferenceTestAuthority (const void*, std::int64_t, std::int64_t, std::int64_t, bool, bool, bool) noexcept;
ReferenceTestAuthorityFacts referenceTestAuthorityFacts (const void*) noexcept;
void notifyReferenceTestCaptureReset (const void*, const char*, std::int64_t) noexcept;
ReferenceTestCaptureResetFacts referenceTestCaptureResetFacts (const void*) noexcept;
}
 #define KIRIN_REFERENCE_STAGE_CURRENT_FOR_TEST(stage) ::hypha::reference_audition::stageCurrentReferenceTestWorker(stage)
 #define KIRIN_REFERENCE_STAGE_FOR_TEST(owner, stage) ::hypha::reference_audition::stageReferenceTestWorker(owner, stage)
 #define KIRIN_REFERENCE_BINDING_READ_FOR_TEST(owner) ::hypha::reference_audition::ReferenceTestBindingReadScope bindingReadProbe (owner)
 #define KIRIN_REFERENCE_BINDING_ERROR_FOR_TEST(error) ::hypha::reference_audition::errorReferenceTestBindingRead(error)
 #define KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST(stage) ::hypha::reference_audition::stageReferenceTestBindingRead(stage)
 #define KIRIN_REFERENCE_BINDING_VALUES_FOR_TEST(...) ::hypha::reference_audition::valuesReferenceTestBindingRead(__VA_ARGS__)
 #define KIRIN_REFERENCE_BINDING_ACCEPT_FOR_TEST() ::hypha::reference_audition::acceptReferenceTestBindingRead()
 #define KIRIN_REFERENCE_BEFORE_CAPTURE_FOR_TEST(controller) (::hypha::reference_audition::stageReferenceTestWorker(controller, "before_capture_service"), ::hypha::reference_audition::beforeReferenceTestCaptureService(controller))
 #define KIRIN_REFERENCE_COMMIT_FOR_TEST() ::hypha::reference_audition::notifyReferenceTestCommit()
 #define KIRIN_REFERENCE_REFRESH_FOR_TEST(controller) (::hypha::reference_audition::stageReferenceTestWorker(controller, "configuration_applied_before_pages"), ::hypha::reference_audition::consumeReferenceTestWorkspaceRefresh(controller))
 #define KIRIN_REFERENCE_CANCEL_REFRESH_FOR_TEST(controller) ::hypha::reference_audition::cancelReferenceTestWorkspaceRefresh(controller)
 #define KIRIN_REFERENCE_AUTHORITY_FOR_TEST(...) ::hypha::reference_audition::observeReferenceTestAuthority(__VA_ARGS__)
 #define KIRIN_REFERENCE_CAPTURE_RESET_FOR_TEST(capture, reason, atMs) ::hypha::reference_audition::notifyReferenceTestCaptureReset(capture, reason, atMs)
#else
 #define KIRIN_REFERENCE_STAGE_CURRENT_FOR_TEST(stage) ((void) 0)
 #define KIRIN_REFERENCE_STAGE_FOR_TEST(owner, stage) ((void) 0)
 #define KIRIN_REFERENCE_BINDING_READ_FOR_TEST(owner) ((void) 0)
 #define KIRIN_REFERENCE_BINDING_ERROR_FOR_TEST(error) ((void) 0)
 #define KIRIN_REFERENCE_BINDING_STAGE_FOR_TEST(stage) ((void) 0)
 #define KIRIN_REFERENCE_BINDING_VALUES_FOR_TEST(...) ((void) 0)
 #define KIRIN_REFERENCE_BINDING_ACCEPT_FOR_TEST() ((void) 0)
 #define KIRIN_REFERENCE_BEFORE_CAPTURE_FOR_TEST(controller) ((void) 0)
 #define KIRIN_REFERENCE_COMMIT_FOR_TEST() ((void) 0)
 #define KIRIN_REFERENCE_REFRESH_FOR_TEST(controller) false
 #define KIRIN_REFERENCE_CANCEL_REFRESH_FOR_TEST(controller) ((void) 0)
 #define KIRIN_REFERENCE_AUTHORITY_FOR_TEST(...) ((void) 0)
 #define KIRIN_REFERENCE_CAPTURE_RESET_FOR_TEST(capture, reason, atMs) ((void) 0)
#endif
