#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ReferenceReceiptModuleLifetime.h"
namespace hypha::reference_audition::receipt_detail::windows_detail
{
bool retainModuleAt (const void* anchor) noexcept
{
    HMODULE module = nullptr;
    return GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR> (anchor), &module) != 0;
}
}
#endif
