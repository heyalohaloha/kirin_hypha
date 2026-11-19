#pragma once
#include "ReferenceChangeNotesModel.h"
#if ! JUCE_WINDOWS
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hypha::reference_audition::change_notes_detail
{
#if JUCE_WINDOWS
namespace windows_detail {
void* openDirectory (const juce::File&); void* openFile (const juce::File&);
void close (void*) noexcept; std::int64_t length (void*); int read (void*, void*, int);
}
#endif
// Pin both directories before opening the immutable leaf. Never follow links at these boundaries.
class NotesFile final
{
public:
    NotesFile (const juce::File& root, const juce::String& sha)
    {
#if JUCE_WINDOWS
        directory = windows_detail::openDirectory (root);
        if (directory == nullptr) return;
        notes = windows_detail::openDirectory (root.getChildFile ("notes"));
        if (notes == nullptr) return;
        file = windows_detail::openFile (root.getChildFile ("notes/" + sha + ".json"));
#else
        directory = ::open (root.getFullPathName().toRawUTF8(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (directory < 0) return;
        notes = ::openat (directory, "notes", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (notes < 0) return;
        file = ::openat (notes, (sha + ".json").toRawUTF8(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
#endif
    }
    ~NotesFile()
    {
#if JUCE_WINDOWS
        for (auto handle : { file, notes, directory }) windows_detail::close (handle);
#else
        for (auto handle : { file, notes, directory }) if (handle >= 0) ::close (handle);
#endif
    }
    NotesFile (const NotesFile&) = delete;
    NotesFile& operator= (const NotesFile&) = delete;
    std::int64_t length() const
    {
#if JUCE_WINDOWS
        return windows_detail::length (file);
#else
        struct stat info {};
        if (file < 0 || ::fstat (file, &info) != 0 || ! S_ISREG (info.st_mode)) return -1;
        return info.st_size;
#endif
    }
    int read (void* output, int bytes)
    {
#if JUCE_WINDOWS
        return windows_detail::read (file, output, bytes);
#else
        if (file < 0) return -1;
        ssize_t got;
        do { got = ::read (file, output, static_cast<size_t> (bytes)); } while (got < 0 && errno == EINTR);
        return static_cast<int> (got);
#endif
    }
private:
#if JUCE_WINDOWS
    void* directory = nullptr; void* notes = nullptr; void* file = nullptr;
#else
    int directory = -1, notes = -1, file = -1;
#endif
};
}
