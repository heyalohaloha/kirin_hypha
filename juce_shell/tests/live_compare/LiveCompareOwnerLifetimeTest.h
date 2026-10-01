#pragma once
#include <chrono>
#include <cstring>
#include <thread>
#if defined (_WIN32)
 #include <windows.h>
#else
 #include <sys/wait.h>
 #include <unistd.h>
#endif

// Standalone fixture only. _Exit deliberately skips all C++ cleanup: the OS must release
// PRE's owner claim while the parent still holds the unclosed PCM object.
inline int abandonPreOwner (std::uint64_t key)
{
    SharedRingMapping owner;
    if (! owner.create (key, 48000)) return 3;
    owner.ring()->header.run.store (0x11223344u);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (10);
    while (std::chrono::steady_clock::now() < end)
    {
        if (owner.ring()->header.demand.load() == 2) std::_Exit (0);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    std::_Exit (4);
}

inline void crashedPreCannotRestampOldReaders()
{
    constexpr std::uint64_t key = 0x202610019999;
   #if defined (_WIN32)
    wchar_t path[32768] {};
    const auto length = GetModuleFileNameW (nullptr, path, 32768);
    require (length > 0 && length < 32768, "fixture executable path");
    std::wstring command = std::wstring (L"\"") + path + L"\" --abandon-pre 202610019999";
    STARTUPINFOW startup {};
    startup.cb = static_cast<DWORD> (sizeof (startup));
    PROCESS_INFORMATION child {};
    require (CreateProcessW (path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &startup, &child) != FALSE, "spawn only this fixture's child");
    CloseHandle (child.hThread);
   #else
    const auto child = fork();
    require (child >= 0, "fork standalone fixture before any threads exist");
    if (child == 0) std::_Exit (abandonPreOwner (key));
   #endif
    SharedRingMapping reader, duplicate, replacement;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (! reader.open (key, 48000, false) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    require (reader.ring() != nullptr, "parent retains the child PRE's PCM section");
    while (reader.ring()->header.run.load() != 0x11223344u && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    require (reader.ring()->header.run.load() == 0x11223344u, "child has finished publishing its lifetime");
    const auto a = reader.ring()->header.timing.ownerA.load(), b = reader.ring()->header.timing.ownerB.load();
    require (! duplicate.create (key, 48000), "a live child cannot be displaced");
    reader.ring()->header.demand.store (2);
   #if defined (_WIN32)
    require (WaitForSingleObject (child.hProcess, 5000) == WAIT_OBJECT_0, "child exits without destructor cleanup");
    DWORD result = 1;
    require (GetExitCodeProcess (child.hProcess, &result) && result == 0, "child fixture exit");
    CloseHandle (child.hProcess);
   #else
    int result = 0;
    require (waitpid (child, &result, 0) == child && WIFEXITED (result) && WEXITSTATUS (result) == 0,
             "child exits without destructor cleanup");
   #endif
    require (reader.ring()->header.ownerClosed.load() == 0, "abrupt exit left no C++ close receipt");
    require (replacement.create (key, 48000), "process death automatically releases the PRE-only claim");
    require (reader.ring()->header.ownerClosed.load() != 0
        && reader.ring()->header.run.load() == 0x11223344u, "old reader is closed, never reinitialised");
    require (replacement.ring()->header.run.load() == 0
        && (replacement.ring()->header.timing.ownerA.load() != a
            || replacement.ring()->header.timing.ownerB.load() != b), "replacement has its own empty PCM and lifetime");
}
