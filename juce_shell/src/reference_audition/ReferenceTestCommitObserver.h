#pragma once

// Only the Reference runtime test target observes commits or requests a one-shot
// worker refresh. Shipping has no test call, storage, lock, callback or class field.
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
 #include <cstdint>
namespace hypha::reference_audition
{
void notifyReferenceTestCommit() noexcept;
std::uint64_t requestReferenceTestWorkspaceRefresh (const void*) noexcept;
bool consumeReferenceTestWorkspaceRefresh (const void*) noexcept;
bool referenceTestWorkspaceRefreshConsumed (std::uint64_t) noexcept;
void cancelReferenceTestWorkspaceRefresh (const void*, std::uint64_t ticket = 0) noexcept;
struct ReferenceTestCaptureResetFacts { const char* reason = "none"; std::int64_t atMs = 0; std::uint64_t count = 0; };
void notifyReferenceTestCaptureReset (const void*, const char*, std::int64_t) noexcept;
ReferenceTestCaptureResetFacts referenceTestCaptureResetFacts (const void*) noexcept;
}
 #define KIRIN_REFERENCE_COMMIT_FOR_TEST() ::hypha::reference_audition::notifyReferenceTestCommit()
 #define KIRIN_REFERENCE_REFRESH_FOR_TEST(controller) ::hypha::reference_audition::consumeReferenceTestWorkspaceRefresh(controller)
 #define KIRIN_REFERENCE_CANCEL_REFRESH_FOR_TEST(controller) ::hypha::reference_audition::cancelReferenceTestWorkspaceRefresh(controller)
 #define KIRIN_REFERENCE_CAPTURE_RESET_FOR_TEST(capture, reason, atMs) ::hypha::reference_audition::notifyReferenceTestCaptureReset(capture, reason, atMs)
#else
 #define KIRIN_REFERENCE_COMMIT_FOR_TEST() ((void) 0)
 #define KIRIN_REFERENCE_REFRESH_FOR_TEST(controller) false
 #define KIRIN_REFERENCE_CANCEL_REFRESH_FOR_TEST(controller) ((void) 0)
 #define KIRIN_REFERENCE_CAPTURE_RESET_FOR_TEST(capture, reason, atMs) ((void) 0)
#endif
