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
bool lowerThreadPriority()
{
    return SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
}
}
#endif
