#pragma once
#include <juce_core/juce_core.h>
#if ! JUCE_WINDOWS
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::ui_preferences
{
// Non-RT: keep a stable lock inode beside the preference file that is atomically replaced.
class FileLock final
{
public:
    explicit FileLock (const juce::File& file)
      #if JUCE_WINDOWS
        : lock ("KirinHyphaUiPreferences-" + juce::String::toHexString (file.getFullPathName().hashCode64()))
      #endif
    {
       #if JUCE_WINDOWS
        held = lock.enter (1000);
       #else
        const auto path = file.getSiblingFile (file.getFileName() + ".lock").getFullPathName();
        fd = ::open (path.toRawUTF8(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) return;
        struct stat info {};
        if (::fstat (fd, &info) != 0 || ! S_ISREG (info.st_mode) || info.st_uid != ::geteuid()) return;
        const auto deadline = juce::Time::getMillisecondCounter() + 1000u;
        do
        {
            if (::flock (fd, LOCK_EX | LOCK_NB) == 0) { held = true; break; }
            if (errno != EWOULDBLOCK && errno != EAGAIN) break;
            juce::Thread::sleep (10);
        } while (static_cast<juce::int32> (deadline - juce::Time::getMillisecondCounter()) > 0);
       #endif
    }
    ~FileLock()
    {
       #if JUCE_WINDOWS
        if (held) lock.exit();
       #else
        if (fd >= 0) ::close (fd);
       #endif
    }
    bool acquired() const noexcept { return held; }
private:
    bool held = false;
   #if JUCE_WINDOWS
    juce::InterProcessLock lock;
   #else
    int fd = -1;
   #endif
    JUCE_DECLARE_NON_COPYABLE (FileLock)
};
}
