#pragma once

#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
 #include <atomic>
namespace hypha::reference_audition
{
using ReferenceObservationTestHook = void (*) (const void*);
inline std::atomic<ReferenceObservationTestHook> referenceObservationTestHook { nullptr };
inline void afterReferencePositionForTest (const void* owner)
{
    if (const auto hook = referenceObservationTestHook.load (std::memory_order_acquire)) hook (owner);
}
}
 #define KIRIN_REFERENCE_POSITION_FOR_TEST(owner) ::hypha::reference_audition::afterReferencePositionForTest(owner)
#else
 #define KIRIN_REFERENCE_POSITION_FOR_TEST(owner) ((void) 0)
#endif
