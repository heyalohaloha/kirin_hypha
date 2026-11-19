#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ReferenceRuntimeV2Source.h"
namespace hypha::reference_audition::revision_detail::windows_detail
{
static juce::String number (std::uint64_t n) { return juce::String (std::to_string (n)); }
void* open (const juce::File& file)
{
    const auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}
void close (void* handle) noexcept { if (handle != nullptr) CloseHandle (handle); }
bool revision (void* handle, RuntimeFileRevision& r)
{
    BY_HANDLE_FILE_INFORMATION i {}; FILE_BASIC_INFO b {};
    if (handle == nullptr || !GetFileInformationByHandle (handle, &i)
        || !GetFileInformationByHandleEx (handle, FileBasicInfo, &b, sizeof (b))
        || (i.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0
        || b.LastWriteTime.QuadPart < 116444736000000000LL || b.ChangeTime.QuadPart < 116444736000000000LL)
        return false;
    const auto ticks = [] (LONGLONG t) -> juce::String {
        const auto n = static_cast<std::uint64_t> (t - 116444736000000000LL);
        return n <= std::numeric_limits<std::uint64_t>::max() / 100 ? number (n * 100) : juce::String {};
    };
    r.deviceId = number (i.dwVolumeSerialNumber);
    r.fileId = number ((std::uint64_t (i.nFileIndexHigh) << 32) | i.nFileIndexLow);
    r.sizeBytes = number ((std::uint64_t (i.nFileSizeHigh) << 32) | i.nFileSizeLow);
    r.modifiedTimeNs = ticks (b.LastWriteTime.QuadPart); r.changedTimeNs = ticks (b.ChangeTime.QuadPart);
    if (r.modifiedTimeNs.isEmpty() || r.changedTimeNs.isEmpty()) return false;
    return true;
}
bool seek (void* handle, juce::int64 p)
{
    LARGE_INTEGER v {}; v.QuadPart = p;
    if (handle == nullptr || !SetFilePointerEx (handle, v, nullptr, FILE_BEGIN)) return false;
    return true;
}
int read (void* handle, void* data, int n)
{
    DWORD got = 0;
    return handle != nullptr && ReadFile (handle, data, DWORD (n), &got, nullptr) ? int (got) : -1;
}
}
#endif
