#pragma once
#include <juce_core/juce_core.h>
#if ! JUCE_WINDOWS
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::metric_needle
{
#if JUCE_WINDOWS
namespace windows_detail { void* openOwner (const juce::File&, bool&); void closeOwner (void*) noexcept; }
#endif
// Nonblocking stable-inode claim, including between plugin images in one process.
// Workers retain this shared lease until their cancelled work is really finished.
// Do not unlink a live/stale lock file: replacing its inode would split ownership.
class ChainOwner final
{
public:
    explicit ChainOwner (const juce::File& file)
    {
        const auto parent = file.getParentDirectory();
        if (parent.isSymbolicLink() || parent.getParentDirectory().isSymbolicLink()
            || parent.createDirectory().failed() || parent.isSymbolicLink()
            || parent.getParentDirectory().isSymbolicLink() || file.isSymbolicLink()) return;
       #if JUCE_WINDOWS
        handle = windows_detail::openOwner (file, held);
       #else
        handle = ::open (file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0) return;
        struct stat info {};
        held = ::fstat (handle, &info) == 0 && S_ISREG (info.st_mode) && info.st_uid == ::geteuid()
            && ::flock (handle, LOCK_EX | LOCK_NB) == 0;
       #endif
        if (!held) close();
    }
    ~ChainOwner() { close(); }
    bool acquired() const noexcept { return held; }
private:
    void close() noexcept
    {
       #if JUCE_WINDOWS
        windows_detail::closeOwner (handle); handle = nullptr;
       #else
        if (handle >= 0) { ::close (handle); handle = -1; }
       #endif
        held = false;
    }
    bool held = false;
   #if JUCE_WINDOWS
    void* handle = nullptr;
   #else
    int handle = -1;
   #endif
    JUCE_DECLARE_NON_COPYABLE (ChainOwner)
};
}
