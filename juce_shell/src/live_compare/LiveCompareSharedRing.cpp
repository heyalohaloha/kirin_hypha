#include "LiveCompareSharedRing.h"

#include <cstdio>
#include <new>

#if ! defined (_WIN32)
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::live_compare
{
std::uint64_t pairKeyForPreInstance (const std::string& preInstanceId) noexcept
{
    std::uint64_t hash = 1469598103934665603ull; // FNV-1a 64
    for (const char c : preInstanceId)
    {
        hash ^= static_cast<unsigned char> (c);
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

std::string sharedRingName (std::uint64_t pairKey)
{
    char text[32] {};
    std::snprintf (text, sizeof (text), "/kh-lc-%016llx", static_cast<unsigned long long> (pairKey));
    return text;
}

SharedRingMapping::~SharedRingMapping()
{
    close();
}

#if defined (_WIN32)

bool SharedRingMapping::create (std::uint64_t, std::uint32_t) { return false; }
bool SharedRingMapping::open (std::uint64_t, std::uint32_t) { return false; }
void SharedRingMapping::close() noexcept {}

#else

namespace
{
constexpr std::size_t mappingBytes() noexcept
{
    constexpr std::size_t page = 16384; // a multiple of both the 4 KiB and the 16 KiB page
    return (sizeof (Ring) + page - 1) / page * page;
}

void* mapNamed (const std::string& name, bool create, bool& fresh) noexcept
{
    fresh = false;
    const int fd = create ? shm_open (name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)
                          : shm_open (name.c_str(), O_RDWR, 0);
    if (fd < 0)
        return nullptr;
    struct stat info {};
    bool usable = fstat (fd, &info) == 0;
    if (usable && info.st_size == 0 && create)
    {
        // macOS sizes a shared-memory object once; a stale object of the right size is reused.
        usable = ftruncate (fd, static_cast<off_t> (mappingBytes())) == 0;
        fresh = usable;
    }
    else if (usable)
    {
        usable = info.st_size >= static_cast<off_t> (mappingBytes());
    }
    void* memory = usable ? mmap (nullptr, mappingBytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                          : MAP_FAILED;
    ::close (fd);
    if (memory == MAP_FAILED)
        return nullptr;
    // Touch every page here so the Audio Thread's first publish or read does not fault.
    const auto page = static_cast<std::size_t> (getpagesize());
    volatile unsigned char sink = 0;
    for (std::size_t offset = 0; offset < mappingBytes(); offset += page)
        sink = static_cast<unsigned char> (sink + static_cast<volatile unsigned char*> (memory)[offset]);
    (void) sink;
    return memory;
}
}

bool SharedRingMapping::create (std::uint64_t pairKey, std::uint32_t sampleRate)
{
    close();
    name = sharedRingName (pairKey);
    bool fresh = false;
    void* memory = mapNamed (name, true, fresh);
    if (memory == nullptr)
        return false;
    mapped = fresh ? ::new (memory) Ring() : std::launder (reinterpret_cast<Ring*> (memory));
    mapped->initialise (pairKey, sampleRate);
    owner = true;
    pairKeyValue = pairKey;
    sampleRateValue = sampleRate;
    return true;
}

bool SharedRingMapping::open (std::uint64_t pairKey, std::uint32_t sampleRate)
{
    close();
    name = sharedRingName (pairKey);
    bool fresh = false;
    void* memory = mapNamed (name, false, fresh);
    if (memory == nullptr)
        return false;
    auto* candidate = std::launder (reinterpret_cast<Ring*> (memory));
    if (! candidate->matches (pairKey, sampleRate))
    {
        munmap (memory, mappingBytes());
        return false;
    }
    mapped = candidate;
    owner = false;
    pairKeyValue = pairKey;
    sampleRateValue = sampleRate;
    return true;
}

void SharedRingMapping::close() noexcept
{
    if (mapped != nullptr)
    {
        if (! owner)
            mapped->header.demand.store (0, std::memory_order_release);
        munmap (mapped, mappingBytes());
        if (owner)
            shm_unlink (name.c_str());
    }
    mapped = nullptr;
    owner = false;
}

#endif
}
