#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ReferenceChangeNotesFile.h"
namespace hypha::reference_audition::change_notes_detail::windows_detail
{
void* openDirectory (const juce::File& path)
{
    // Denying write/delete sharing prevents rename/reparse replacement while children are opened.
    const auto handle = CreateFileW (path.getFullPathName().toWideCharPointer(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info {};
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    if (! GetFileInformationByHandle (handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    { CloseHandle (handle); return nullptr; }
    return handle;
}
void* openFile (const juce::File& path)
{
    const auto handle = CreateFileW (path.getFullPathName().toWideCharPointer(), GENERIC_READ,
    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}
void close (void* handle) noexcept { if (handle != nullptr) CloseHandle (handle); }
std::int64_t length (void* file)
{
    BY_HANDLE_FILE_INFORMATION info {};
    if (file == nullptr || ! GetFileInformationByHandle (file, &info)
        || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) return -1;
    const auto bytes = (std::uint64_t (info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return bytes <= 1'048'576 ? static_cast<std::int64_t> (bytes) : -1;
}
int read (void* file, void* output, int bytes)
{
    DWORD got = 0;
    return file != nullptr && ReadFile (file, output, static_cast<DWORD> (bytes), &got, nullptr)
        ? static_cast<int> (got) : -1;
}
}
#endif
