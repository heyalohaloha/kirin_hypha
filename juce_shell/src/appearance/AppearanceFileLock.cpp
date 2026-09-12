#include "AppearanceFileLock.h"

#include <utility>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::appearance
{
namespace
{
constexpr std::int64_t staleLockMilliseconds = 30'000;

bool createLockDirectory (const juce::File& directory)
{
   #if JUCE_WINDOWS
    return ::CreateDirectoryW (directory.getFullPathName().toWideCharPointer(), nullptr) != FALSE;
   #else
    return ::mkdir (directory.getFullPathName().toRawUTF8(), 0700) == 0;
   #endif
}

void removeLockDirectory (const juce::File& directory)
{
   #if JUCE_WINDOWS
    ::RemoveDirectoryW (directory.getFullPathName().toWideCharPointer());
   #else
    ::rmdir (directory.getFullPathName().toRawUTF8());
   #endif
}
}

FileLock::FileLock (juce::File lockDirectory)
    : directory (std::move (lockDirectory))
{
}

FileLock::~FileLock()
{
    if (held)
        removeLockDirectory (directory);
}

bool FileLock::tryAcquire()
{
    if (held)
        return true;
    if (createLockDirectory (directory))
    {
        held = true;
        return true;
    }
    if (directory.isDirectory()
        && juce::Time::getCurrentTime().toMilliseconds()
             - directory.getLastModificationTime().toMilliseconds() > staleLockMilliseconds)
    {
        removeLockDirectory (directory);
        if (createLockDirectory (directory))
        {
            held = true;
            return true;
        }
    }
    return false;
}
}
