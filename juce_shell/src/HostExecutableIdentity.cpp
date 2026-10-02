#include "HostExecutableIdentity.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <winver.h>
#endif

namespace hypha::host_identity
{
namespace
{
#if JUCE_WINDOWS
juce::String fileVersionString (const juce::File& executable)
{
    const auto path = executable.getFullPathName();
    DWORD unused = 0;
    const auto bytes = GetFileVersionInfoSizeW (path.toWideCharPointer(), &unused);
    // Absent, conflicting or truncated metadata must fail closed. No fixed
    // version fallback: e.g. 8.1.2.0 cannot identify Studio Pro build 113407.
    if (bytes == 0 || bytes > 1024 * 1024) return {};
    juce::HeapBlock<char> data (bytes);
    if (! GetFileVersionInfoW (path.toWideCharPointer(), 0, bytes, data.get())) return {};
    struct Translation { WORD language, codepage; };
    void* raw = nullptr;
    UINT length = 0;
    if (! VerQueryValueW (data.get(), L"\\VarFileInfo\\Translation", &raw, &length)
        || raw == nullptr || length == 0 || length % sizeof (Translation) != 0
        || length / sizeof (Translation) > 128) return {};
    const auto* translations = static_cast<const Translation*> (raw);
    juce::String result;
    for (UINT i = 0; i < length / sizeof (Translation); ++i)
    {
        const auto key = juce::String::formatted ("\\StringFileInfo\\%04x%04x\\FileVersion",
            static_cast<unsigned> (translations[i].language),
            static_cast<unsigned> (translations[i].codepage));
        void* text = nullptr;
        UINT characters = 0;
        if (! VerQueryValueW (data.get(), key.toWideCharPointer(), &text, &characters)
            || text == nullptr || characters < 2 || characters > 128) return {};
        const auto* wide = static_cast<const wchar_t*> (text);
        if (wide[characters - 1] != L'\0') return {};
        const juce::String version (wide, static_cast<int> (characters - 1));
        if (version.isEmpty() || version.length() != static_cast<int> (characters - 1)
            || (result.isNotEmpty() && result != version)) return {};
        result = version;
    }
    return result;
}
#endif
}

Identity read (const juce::File& executable)
{
    if (! executable.existsAsFile()) return {};
    Identity identity { executable.getFileNameWithoutExtension(), {}, {} };
   #if JUCE_MAC
    // hostApplicationPath is Contents/MacOS/<executable>, not the .app path.
    // Keep the executable name: app folders may be renamed ("Studio Pro 8.app").
    const auto macos = executable.getParentDirectory();
    const auto contents = macos.getParentDirectory();
    const auto app = contents.getParentDirectory();
    if (macos.getFileName() == "MacOS" && contents.getFileName() == "Contents"
        && app.hasFileExtension ("app") && app.isDirectory())
        identity.version = app.getVersion();
    identity.fileVersion = identity.version;
   #elif JUCE_WINDOWS
    identity.version = fileVersionString (executable);
    // Pro Tools' textual resource has a trailing quote in both observed builds.
    // Do not trim it or reinterpret either fact. Its AAX certificate uses the
    // exact fixed version; Studio Pro's VST3 certificate uses the complete text.
    identity.fileVersion = executable.getVersion();
   #endif
    return identity;
}

Identity current()
{
    return read (juce::File::getSpecialLocation (juce::File::hostApplicationPath));
}
}
