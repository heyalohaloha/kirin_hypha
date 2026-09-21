#include "reference_rt_probe.h"
#include <cstdlib>
#include <new>

// Ordinary C++ heap operations on the callback thread. Direct JUCE allocation,
// locks and I/O are also audited at the bounded buffer/view call sites.
static thread_local bool referenceRt = false;
static thread_local unsigned referenceHeapOperations = 0;
void beginReferenceRtProbe() noexcept { referenceHeapOperations = 0; referenceRt = true; }
unsigned endReferenceRtProbe() noexcept { referenceRt = false; return referenceHeapOperations; }
void* operator new (std::size_t bytes)
{
    if (referenceRt) ++referenceHeapOperations;
    if (auto* pointer = std::malloc (bytes == 0 ? 1 : bytes)) return pointer;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* pointer) noexcept { if (referenceRt) ++referenceHeapOperations; std::free (pointer); }
void operator delete[] (void* pointer) noexcept { ::operator delete (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { ::operator delete (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept { ::operator delete (pointer); }
