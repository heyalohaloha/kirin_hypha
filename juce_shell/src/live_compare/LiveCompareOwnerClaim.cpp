#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "LiveCompareOwnerClaim.h"
namespace hypha::live_compare::windows_detail
{
void* acquire (const std::wstring& name)
{
    auto candidate = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1, name.c_str());
    if (candidate == nullptr) return nullptr;
    const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
    if (! fresh) { CloseHandle (candidate); return nullptr; }
    return candidate;
}
void close (void* handle) noexcept { if (handle != nullptr) CloseHandle (handle); }
}
#endif
