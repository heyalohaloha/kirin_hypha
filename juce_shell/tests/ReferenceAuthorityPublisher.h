#pragma once
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <thread>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

// One rename is the OS-equivalent default. Retry is an explicit comparison only.
inline std::atomic<int> referenceAuthorityRenameAttempts { 1 };
inline bool publishReferenceBindingByRename (const juce::File& file, const juce::var& value)
{
    if (!file.getParentDirectory().createDirectory().wasOk()) return false;
    juce::TemporaryFile temporary (file, hypha::storage::temporarySibling (file));
    {
        auto stream = temporary.getFile().createOutputStream();
        if (!stream || !stream->openedOk() || !stream->writeText (juce::JSON::toString (value, true) + "\n", false, false, "\n"))
        { std::cerr << "AUTHORITY_PUBLISH_FAIL stage=write" << std::endl; return false; }
        stream->flush(); // JUCE flush includes FlushFileBuffers / fsync.
        if (stream->getStatus().failed())
        { std::cerr << "AUTHORITY_PUBLISH_FAIL stage=sync" << std::endl; return false; }
    } // close before rename; never unlink the destination.
    const int attempts = referenceAuthorityRenameAttempts.load();
    for (int attempt = 1; attempt <= attempts; ++attempt)
    {
       #if JUCE_WINDOWS
        const bool ok = MoveFileExW (temporary.getFile().getFullPathName().toWideCharPointer(),
            file.getFullPathName().toWideCharPointer(), MOVEFILE_REPLACE_EXISTING) != 0;
        const auto error = ok ? 0UL : GetLastError();
       #else
        const bool ok = ::rename (temporary.getFile().getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) == 0;
        const auto error = ok ? 0UL : static_cast<unsigned long> (errno);
       #endif
        if (ok) return true;
        std::cerr << "AUTHORITY_RENAME_FAIL attempt=" << attempt << " max=" << attempts << " error=" << error << std::endl;
        if (attempt < attempts) std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    return false;
}
