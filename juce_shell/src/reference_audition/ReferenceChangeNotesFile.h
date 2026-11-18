#pragma once
#include "ReferenceChangeNotesModel.h"
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hypha::reference_audition::change_notes_detail
{
// Pin both directories before opening the immutable leaf. Never follow links at these boundaries.
class NotesFile final
{
public:
    NotesFile (const juce::File& root, const juce::String& sha)
    {
#if JUCE_WINDOWS
        directory = openDirectory (root);
        if (directory == INVALID_HANDLE_VALUE) return;
        notes = openDirectory (root.getChildFile ("notes"));
        if (notes == INVALID_HANDLE_VALUE) return;
        file = CreateFileW (root.getChildFile ("notes/" + sha + ".json").getFullPathName().toWideCharPointer(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
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
        for (auto handle : { file, notes, directory }) if (handle != INVALID_HANDLE_VALUE) CloseHandle (handle);
#else
        for (auto handle : { file, notes, directory }) if (handle >= 0) ::close (handle);
#endif
    }
    NotesFile (const NotesFile&) = delete;
    NotesFile& operator= (const NotesFile&) = delete;
    std::int64_t length() const
    {
#if JUCE_WINDOWS
        BY_HANDLE_FILE_INFORMATION info {};
        if (file == INVALID_HANDLE_VALUE || ! GetFileInformationByHandle (file, &info)
            || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) return -1;
        const auto bytes = (std::uint64_t (info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        return bytes <= 1'048'576 ? static_cast<std::int64_t> (bytes) : -1;
#else
        struct stat info {};
        if (file < 0 || ::fstat (file, &info) != 0 || ! S_ISREG (info.st_mode)) return -1;
        return info.st_size;
#endif
    }
    int read (void* output, int bytes)
    {
#if JUCE_WINDOWS
        DWORD got = 0;
        return file != INVALID_HANDLE_VALUE && ReadFile (file, output, static_cast<DWORD> (bytes), &got, nullptr)
            ? static_cast<int> (got) : -1;
#else
        if (file < 0) return -1;
        ssize_t got;
        do { got = ::read (file, output, static_cast<size_t> (bytes)); } while (got < 0 && errno == EINTR);
        return static_cast<int> (got);
#endif
    }
private:
#if JUCE_WINDOWS
    static HANDLE openDirectory (const juce::File& path)
    {
        // Denying write/delete sharing prevents rename/reparse replacement while children are opened.
        const auto handle = CreateFileW (path.getFullPathName().toWideCharPointer(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info {};
        if (handle == INVALID_HANDLE_VALUE) return handle;
        if (! GetFileInformationByHandle (handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
            || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        { CloseHandle (handle); return INVALID_HANDLE_VALUE; }
        return handle;
    }
    HANDLE directory = INVALID_HANDLE_VALUE, notes = INVALID_HANDLE_VALUE, file = INVALID_HANDLE_VALUE;
#else
    int directory = -1, notes = -1, file = -1;
#endif
};
}
