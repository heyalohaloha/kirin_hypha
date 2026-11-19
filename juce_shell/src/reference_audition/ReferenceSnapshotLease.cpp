#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <limits>
namespace hypha::reference_audition::revision_detail::windows_detail
{
void* openSnapshotLease (const juce::File& file, bool create)
{
    const auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, create ? CREATE_NEW : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    BY_HANDLE_FILE_INFORMATION info {};
    if (!GetFileInformationByHandle (handle, &info)
        || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
    { CloseHandle (handle); return nullptr; }
    return handle;
}
void closeSnapshotLease (void* handle) noexcept { if (handle != nullptr) CloseHandle (handle); }
bool setSnapshotLeaseTime (void* handle, juce::int64 ms)
{
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    if (ms < 0 || static_cast<std::uint64_t> (ms) > (std::numeric_limits<std::uint64_t>::max() - epoch) / 10000) return false;
    const auto ticks = static_cast<std::uint64_t> (ms) * 10000 + epoch;
    const FILETIME value { static_cast<DWORD> (ticks), static_cast<DWORD> (ticks >> 32) };
    return handle != nullptr && SetFileTime (handle, nullptr, nullptr, &value) != 0;
}
}
#endif
