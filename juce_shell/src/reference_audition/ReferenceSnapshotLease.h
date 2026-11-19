#pragma once
#include "ReferenceSourceFile.h"
#if ! JUCE_WINDOWS
#include <sys/file.h>
#endif

namespace hypha::reference_audition::revision_detail
{
#if JUCE_WINDOWS
namespace windows_detail {
void* openSnapshotLease (const juce::File&, bool);
void closeSnapshotLease (void*) noexcept;
bool setSnapshotLeaseTime (void*, juce::int64);
}
#endif
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
inline std::atomic<const juce::File*> snapshotRootForTest { nullptr };
#endif
// A kernel-held lease survives no process: a crashed owner's files can be reclaimed,
// while another host or plug-in instance keeps its own directory protected.
class SnapshotLease final
{
public:
    static juce::File root()
    {
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (const auto* path = snapshotRootForTest.load()) return *path;
#endif
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kirin-hypha-reference-cache-v1");
    }
    explicit SnapshotLease (const juce::File& parent = root())
    {
        if (parent.isSymbolicLink() || parent.createDirectory().failed()) return;
        collect (parent);
        directory = parent.getChildFile (juce::Uuid().toString());
        if (directory.createDirectory().failed()) return;
        lease = directory.getChildFile ("owner.lock");
        held = lock (lease, true);
        if (!held) directory.deleteFile();
    }
    ~SnapshotLease()
    {
        if (held) { payload().deleteFile(); unlock(); lease.deleteFile(); directory.deleteFile(); }
        else unlock();
    }
    SnapshotLease (const SnapshotLease&) = delete;
    SnapshotLease& operator= (const SnapshotLease&) = delete;
    bool available() const { return held; }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    bool setModificationTimeForTest (juce::Time when)
    {
#if JUCE_WINDOWS
        return held && windows_detail::setSnapshotLeaseTime (handle, when.toMilliseconds());
#else
        return held && lease.setLastModificationTime (when);
#endif
    }
#endif
    juce::File payload() const { return directory == juce::File {} ? juce::File {} : directory.getChildFile ("audio.snapshot"); }
    static void collect (const juce::File& parent, std::int64_t now = juce::Time::currentTimeMillis())
    {
        if (parent.isSymbolicLink()) return;
        int reclaimed = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (parent, false, "*", juce::File::findDirectories))
        {
            if (reclaimed >= 64) break;
            const auto dir = entry.getFile();
            const auto name = dir.getFileName();
            if (dir.isSymbolicLink() || name.length() != 32 || !name.containsOnly ("0123456789abcdef")) continue;
            const auto owner = dir.getChildFile ("owner.lock"), audio = dir.getChildFile ("audio.snapshot");
            if (owner.isSymbolicLink() || !owner.existsAsFile() || audio.isSymbolicLink()
                || now - owner.getLastModificationTime().toMilliseconds() < 24 * 60 * 60 * 1000LL) continue;
            SnapshotLease orphan (0);
            if (!orphan.lock (owner, false)) continue;
            // Delete only our known files; unrelated contents keep their directory intact.
            audio.deleteFile(); orphan.unlock(); owner.deleteFile(); dir.deleteFile(); ++reclaimed;
        }
    }
private:
    // An empty lease is used only to claim an existing orphan.
    explicit SnapshotLease (int) {}
    bool lock (const juce::File& file, bool create)
    {
#if JUCE_WINDOWS
        handle = windows_detail::openSnapshotLease (file, create);
        if (handle == nullptr) return false;
#else
        handle = ::open (file.getFullPathName().toRawUTF8(), O_RDWR | O_NOFOLLOW | O_CLOEXEC
            | (create ? O_CREAT | O_EXCL : 0), 0600);
        struct stat info {};
        if (handle < 0) return false;
        if (::fstat (handle, &info) != 0 || !S_ISREG (info.st_mode) || ::flock (handle, LOCK_EX | LOCK_NB) != 0)
        { unlock(); return false; }
#endif
        return true;
    }
    void unlock()
    {
#if JUCE_WINDOWS
        windows_detail::closeSnapshotLease (handle); handle = nullptr;
#else
        if (handle >= 0) { ::close (handle); handle = -1; }
#endif
    }
    juce::File directory, lease;
    bool held = false;
#if JUCE_WINDOWS
    void* handle = nullptr;
#else
    int handle = -1;
#endif
};
}
