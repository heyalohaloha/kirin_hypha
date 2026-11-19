#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace hypha::reference_audition::metrics::windows_detail
{
double threadCpuSeconds()
{
    FILETIME created {}, exited {}, kernel {}, user {};
    if (!GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    ULARGE_INTEGER k {}, u {}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return static_cast<double> (k.QuadPart + u.QuadPart) * 1.0e-7;
}
}
#endif
