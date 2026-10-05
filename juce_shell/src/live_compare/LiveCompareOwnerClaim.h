#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace hypha::live_compare
{
// PRE-only, non-RT lifetime ownership. Readers never hold this claim. Process death releases
// it, unlike a POSIX name or a Windows PCM section retained by a POST reader.
// A Windows section has no owning-thread constraint; BSD flock follows the open file, not
// the creating thread. Neither path adds a syscall, lock, heartbeat or poll to the Audio Thread.
class OwnerClaim final
{
public:
    ~OwnerClaim()
    {
       #if defined (_WIN32)
        if (handle != nullptr) CloseHandle (handle);
       #else
        if (fd >= 0) ::close (fd);
       #endif
    }
    OwnerClaim() = default;
    OwnerClaim (const OwnerClaim&) = delete;
    OwnerClaim& operator= (const OwnerClaim&) = delete;

    bool acquire (std::uint64_t key)
    {
        char suffix[24] {};
        std::snprintf (suffix, sizeof (suffix), "%016llx", static_cast<unsigned long long> (key));
       #if defined (_WIN32)
        const std::string narrow = std::string ("Local\\kh-lc7-owner-") + suffix;
        const std::wstring name (narrow.begin(), narrow.end());
        auto candidate = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1, name.c_str());
        if (candidate == nullptr) return false;
        const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
        if (! fresh) { CloseHandle (candidate); return false; }
        handle = candidate;
       #else
        // Darwin POSIX shm descriptors do not support flock (ENOTSUP). Use a zero-byte,
        // user-owned regular file instead. Keep its name: unlinking a lock file would let
        // another opener lock a different inode while a cooperating process still holds it.
        const std::string path = "/tmp/kh-lc7-owner-" + std::to_string (geteuid()) + "-" + suffix;
        const int candidate = ::open (path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (candidate < 0) return false;
        struct stat info {};
        const bool privateFile = fstat (candidate, &info) == 0 && S_ISREG (info.st_mode)
            && info.st_uid == geteuid() && (info.st_mode & 077) == 0;
        if (! privateFile || flock (candidate, LOCK_EX | LOCK_NB) != 0)
        { ::close (candidate); return false; }
        fd = candidate;
       #endif
        return true;
    }
private:
   #if defined (_WIN32)
    HANDLE handle = nullptr;
   #else
    int fd = -1;
   #endif
};
}
