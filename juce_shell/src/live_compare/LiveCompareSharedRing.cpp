#include "LiveCompareSharedRing.h"

#include <cstdio>
#include <new>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
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

namespace
{
// The allocation granularity, 64 KiB, covers every page size.
constexpr std::size_t mappingBytes() noexcept
{
    constexpr std::size_t granularity = 65536;
    return (sizeof (Ring) + granularity - 1) / granularity * granularity;
}

// A section keeps its name while any process holds it, so a PRE cannot remove a ring that a POST
// still maps. PRE stamps the first of these slots that no POST holds closed; POST opens the first
// slot stamped for its pair and rate that its PRE has not closed.
constexpr int ringSlots = 4;

// "Local\kh-lc-" + 16 hex digits + "-" + slot: this logon session's namespace with the creator's
// default access, like the Analysis exchange (INV-LC11).
std::wstring slotName (std::uint64_t pairKey, int slot)
{
    const auto posix = sharedRingName (pairKey);
    std::wstring wide = L"Local\\";
    for (std::size_t i = 1; i < posix.size(); ++i)
        wide.push_back (static_cast<wchar_t> (posix[i]));
    wide.push_back (L'-');
    wide.push_back (static_cast<wchar_t> (L'0' + slot));
    return wide;
}

void* mapSection (HANDLE section) noexcept
{
    void* memory = MapViewOfFile (section, FILE_MAP_ALL_ACCESS, 0, 0, mappingBytes());
    if (memory == nullptr)
        return nullptr;
    // Touch every page here so the Audio Thread's first publish or read does not fault.
    SYSTEM_INFO info {};
    GetSystemInfo (&info);
    const auto page = static_cast<std::size_t> (info.dwPageSize > 0 ? info.dwPageSize : 4096);
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
    const auto bytes = static_cast<std::uint64_t> (mappingBytes());
    for (int slot = 0; slot < ringSlots; ++slot)
    {
        const auto name = slotName (pairKey, slot);
        HANDLE handle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                            static_cast<DWORD> (bytes >> 32),
                                            static_cast<DWORD> (bytes & 0xffffffffu), name.c_str());
        if (handle == nullptr)
            continue;
        const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS; // read before anything else runs
        void* memory = mapSection (handle); // a smaller stale section cannot be mapped at this size
        auto* existing = memory != nullptr ? std::launder (reinterpret_cast<Ring*> (memory)) : nullptr;
        if (existing == nullptr || (! fresh && existing->header.ownerClosed.load (std::memory_order_acquire) != 0))
        {
            if (memory != nullptr)
                UnmapViewOfFile (memory);
            CloseHandle (handle);
            continue;
        }
        mapped = fresh ? ::new (memory) Ring() : existing;
        mapped->initialise (pairKey, sampleRate);
        section = handle;
        owner = true;
        pairKeyValue = pairKey;
        sampleRateValue = sampleRate;
        return true;
    }
    return false;
}

bool SharedRingMapping::open (std::uint64_t pairKey, std::uint32_t sampleRate)
{
    close();
    for (int slot = 0; slot < ringSlots; ++slot)
    {
        const auto name = slotName (pairKey, slot);
        HANDLE handle = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (handle == nullptr)
            continue;
        void* memory = mapSection (handle);
        auto* candidate = memory != nullptr ? std::launder (reinterpret_cast<Ring*> (memory)) : nullptr;
        if (candidate == nullptr || ! candidate->matches (pairKey, sampleRate)
            || candidate->header.ownerClosed.load (std::memory_order_acquire) != 0)
        {
            if (memory != nullptr)
                UnmapViewOfFile (memory);
            CloseHandle (handle);
            continue;
        }
        mapped = candidate;
        section = handle;
        owner = false;
        pairKeyValue = pairKey;
        sampleRateValue = sampleRate;
        return true;
    }
    return false;
}

void SharedRingMapping::close() noexcept
{
    if (mapped != nullptr)
    {
        // As on macOS: a POST still holding this ring learns that its owner has gone.
        if (owner)
            mapped->header.ownerClosed.store (1, std::memory_order_release);
        else
            mapped->header.demand.store (0, std::memory_order_release);
        UnmapViewOfFile (mapped);
    }
    if (section != nullptr)
        CloseHandle (static_cast<HANDLE> (section));
    mapped = nullptr;
    section = nullptr;
    owner = false;
}

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
        // A re-prepared PRE maps a new object under the same name; a POST still holding this one
        // would wait for writes that never come, so it learns that the owner has gone.
        if (owner)
            mapped->header.ownerClosed.store (1, std::memory_order_release);
        else
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
