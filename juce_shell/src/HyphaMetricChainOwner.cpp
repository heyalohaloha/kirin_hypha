#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "HyphaMetricChainOwner.h"
namespace hypha::metric_needle::windows_detail
{
void* openOwner (const juce::File& file, bool& held)
{
    auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    BY_HANDLE_FILE_INFORMATION info {};
    held = GetFileInformationByHandle (handle, &info)
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    return handle;
}
void closeOwner (void* handle) noexcept { if (handle != nullptr) CloseHandle (handle); }
}
#endif
