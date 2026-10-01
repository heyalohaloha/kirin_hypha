#include "ProcessorHeapProbe.h"
#include <atomic>
#include <cstdlib>
#include <malloc/malloc.h>
#include <pthread.h>

// Separate dylib: dyld does not interpose calls from the interposer's own image. Keeping the
// probe outside the executable makes its static Rust FFI's System allocator observable too.
// Apple ABI: https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld-interposing.h
// Covers standard C/Rust allocator entry points and direct public zone calls. This is not a
// VM/syscall audit or a claim about a different/custom allocator. No logging from the hooks.
namespace
{
pthread_key_t probeKey;
std::atomic<bool> ready { false };
static_assert (std::atomic<bool>::is_always_lock_free);
__attribute__((always_inline)) inline void note (bool allocate, bool release) noexcept
{
    if (ready.load (std::memory_order_acquire))
        if (auto* counts = static_cast<ProcessorHeapCounts*> (pthread_getspecific (probeKey)))
        {
            counts->allocations += allocate; counts->frees += release;
            if (allocate && counts->firstAllocation == nullptr) counts->firstAllocation = __builtin_return_address (0);
            if (release && counts->firstFree == nullptr) counts->firstFree = __builtin_return_address (0);
        }
}
void* probeMalloc (std::size_t n) { note (true, false); return std::malloc (n); }
void* probeCalloc (std::size_t n, std::size_t size) { note (true, false); return std::calloc (n, size); }
void* probeRealloc (void* p, std::size_t n) { note (true, p != nullptr); return std::realloc (p, n); }
void probeFree (void* p) { note (false, p != nullptr); std::free (p); }
int probeMemalign (void** p, std::size_t alignment, std::size_t n)
{ note (true, false); return ::posix_memalign (p, alignment, n); }
void* probeAligned (std::size_t alignment, std::size_t n)
{ note (true, false); return ::aligned_alloc (alignment, n); }
void* probeValloc (std::size_t n) { note (true, false); return ::valloc (n); }
void* probeZoneMalloc (malloc_zone_t* zone, std::size_t n)
{ note (true, false); return ::malloc_zone_malloc (zone, n); }
void* probeZoneCalloc (malloc_zone_t* zone, std::size_t n, std::size_t size)
{ note (true, false); return ::malloc_zone_calloc (zone, n, size); }
void* probeZoneRealloc (malloc_zone_t* zone, void* p, std::size_t n)
{ note (true, p != nullptr); return ::malloc_zone_realloc (zone, p, n); }
void probeZoneFree (malloc_zone_t* zone, void* p)
{ note (false, p != nullptr); ::malloc_zone_free (zone, p); }
void* probeZoneMemalign (malloc_zone_t* zone, std::size_t alignment, std::size_t n)
{ note (true, false); return ::malloc_zone_memalign (zone, alignment, n); }
void* probeZoneValloc (malloc_zone_t* zone, std::size_t n)
{ note (true, false); return ::malloc_zone_valloc (zone, n); }
unsigned probeZoneBatchMalloc (malloc_zone_t* zone, std::size_t n, void** results, unsigned requested)
{ note (requested != 0, false); return ::malloc_zone_batch_malloc (zone, n, results, requested); }
void probeZoneBatchFree (malloc_zone_t* zone, void** pointers, unsigned n)
{ note (false, n != 0); ::malloc_zone_batch_free (zone, pointers, n); }

// Original/replacement pairs use the public Mach-O interposing section ABI. Own declarations,
// not a vendored SDK header. Calls made by this dylib itself reach the original functions.
struct Entry { const void* replacement; const void* original; };
__attribute__((used, section("__DATA,__interpose,interposing"))) const Entry entries[] {
    { reinterpret_cast<const void*> (&probeMalloc), reinterpret_cast<const void*> (&::malloc) },
    { reinterpret_cast<const void*> (&probeCalloc), reinterpret_cast<const void*> (&::calloc) },
    { reinterpret_cast<const void*> (&probeRealloc), reinterpret_cast<const void*> (&::realloc) },
    { reinterpret_cast<const void*> (&probeFree), reinterpret_cast<const void*> (&::free) },
    { reinterpret_cast<const void*> (&probeMemalign), reinterpret_cast<const void*> (&::posix_memalign) },
    { reinterpret_cast<const void*> (&probeAligned), reinterpret_cast<const void*> (&::aligned_alloc) },
    { reinterpret_cast<const void*> (&probeValloc), reinterpret_cast<const void*> (&::valloc) },
    { reinterpret_cast<const void*> (&probeZoneMalloc), reinterpret_cast<const void*> (&::malloc_zone_malloc) },
    { reinterpret_cast<const void*> (&probeZoneCalloc), reinterpret_cast<const void*> (&::malloc_zone_calloc) },
    { reinterpret_cast<const void*> (&probeZoneRealloc), reinterpret_cast<const void*> (&::malloc_zone_realloc) },
    { reinterpret_cast<const void*> (&probeZoneFree), reinterpret_cast<const void*> (&::malloc_zone_free) },
    { reinterpret_cast<const void*> (&probeZoneMemalign), reinterpret_cast<const void*> (&::malloc_zone_memalign) },
    { reinterpret_cast<const void*> (&probeZoneValloc), reinterpret_cast<const void*> (&::malloc_zone_valloc) },
    { reinterpret_cast<const void*> (&probeZoneBatchMalloc), reinterpret_cast<const void*> (&::malloc_zone_batch_malloc) },
    { reinterpret_cast<const void*> (&probeZoneBatchFree), reinterpret_cast<const void*> (&::malloc_zone_batch_free) }
};
}

bool initialiseProcessorHeapProbe() noexcept
{
    if (ready.load (std::memory_order_acquire)) return true;
    if (pthread_key_create (&probeKey, nullptr) != 0) return false;
    ready.store (true, std::memory_order_release);
    return true;
}
bool beginProcessorHeapProbe (ProcessorHeapCounts* counts) noexcept
{
    // Initialisation is required on the control thread before any audio starts. The stack
    // pointer avoids allocating dynamic C++ TLS while inside the allocator interposer.
    return counts != nullptr && ready.load (std::memory_order_acquire)
        && pthread_setspecific (probeKey, counts) == 0;
}
bool endProcessorHeapProbe() noexcept
{
    return ready.load (std::memory_order_acquire) && pthread_setspecific (probeKey, nullptr) == 0;
}
