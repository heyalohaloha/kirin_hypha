#pragma once
#include <cstdlib>
#include <new>

// One test executable owns these operators. The thread-local guard covers only the real
// processor callback; fixture setup, service, workers and oracle calculations are outside it.
namespace hypha::tests::candidate_allocations
{
inline thread_local bool realtime = false;
inline thread_local unsigned allocations = 0, deletions = 0;
struct Callback
{
    Callback() { realtime = true; }
    ~Callback() { realtime = false; }
};
}
void* operator new (std::size_t bytes)
{
    if (hypha::tests::candidate_allocations::realtime) ++hypha::tests::candidate_allocations::allocations;
    if (auto* memory = std::malloc (bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t bytes) { return ::operator new (bytes); }
void operator delete (void* memory) noexcept
{
    if (memory && hypha::tests::candidate_allocations::realtime) ++hypha::tests::candidate_allocations::deletions;
    std::free (memory);
}
void operator delete[] (void* memory) noexcept { ::operator delete (memory); }
void operator delete (void* memory, std::size_t) noexcept { ::operator delete (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { ::operator delete (memory); }
