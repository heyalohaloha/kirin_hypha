#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include "StorageWritePathPolicy.h"
namespace hypha::storage::write_guard::detail
{
bool windowsProtectedRoots (std::vector<fs::path>& roots)
{
    const auto known = [](REFKNOWNFOLDERID id, fs::path& out) {
        PWSTR raw=nullptr; const auto hr=SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&raw);
        if (FAILED(hr) || raw==nullptr) { if(raw) CoTaskMemFree(raw); return false; }
        out=raw; CoTaskMemFree(raw); return true;
    };
    fs::path home,local,roaming,documents;
    if (!known(FOLDERID_Profile,home)||!known(FOLDERID_LocalAppData,local)||!known(FOLDERID_RoamingAppData,roaming)
        ||!known(FOLDERID_Documents,documents)) return false;
    roots={home/L"Documents"/L"Kirin OS",documents/L"Kirin OS",
           home/L"AppData"/L"Local"/L"Kirin OS",home/L"AppData"/L"Roaming"/L"Kirin OS",
           local/L"Kirin OS",roaming/L"Kirin OS"};
    for (const auto* name : {"APPDATA", "LOCALAPPDATA"}) {
        const auto value=juce::SystemStats::getEnvironmentVariable(name,{});
        if(value.isNotEmpty()) {
            if(!juce::File::isAbsolutePath(value)) return false;
            roots.push_back(pathOf(juce::File(value).getChildFile("Kirin OS")));
        }
    }
    return true;
}
bool windowsReparsePoint (const fs::path& path)
{
    const auto attributes = GetFileAttributesW (path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
}
#endif
