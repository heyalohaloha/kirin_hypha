#include "AppearanceTestWriteGuard.h"
#include <filesystem>
#include <mutex>
#if JUCE_WINDOWS
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif
namespace hypha::appearance::test_write_guard
{
namespace
{
std::mutex mutex;
std::optional<FixturePolicy> fixture;
std::uint64_t permitted = 0;
namespace fs = std::filesystem;
fs::path pathOf (const juce::File& file)
{
#if JUCE_WINDOWS
    return fs::path(file.getFullPathName().toWideCharPointer());
#else
    return fs::path(file.getFullPathName().toRawUTF8());
#endif
}
bool canonical (const fs::path& input, fs::path& output)
{
    std::error_code error; output = fs::weakly_canonical(input,error);
    return !error && output.is_absolute();
}
bool contains (const fs::path& parent, const fs::path& child)
{
    auto a=parent.begin(), b=child.begin();
    for (;a!=parent.end();++a,++b)
    {
        if (b==child.end()) return false;
#if JUCE_WINDOWS || JUCE_MAC
        // Conservative on case-sensitive Mac volumes too: test-only denial.
        if (!juce::String(a->wstring().c_str()).equalsIgnoreCase(juce::String(b->wstring().c_str()))) return false;
#else
        if (*a != *b) return false;
#endif
    }
    return true;
}
bool realProtectedRoots (std::vector<fs::path>& roots)
{
#if JUCE_WINDOWS
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
#else
    passwd user {}, *result=nullptr;
    std::vector<char> buffer(65536);
    if (getpwuid_r(getuid(),&user,buffer.data(),buffer.size(),&result)!=0 || result==nullptr
        || user.pw_dir==nullptr || *user.pw_dir==0) return false;
    fs::path home(user.pw_dir);
    roots={home/"Documents"/"Kirin OS"};
#if JUCE_MAC
    roots.push_back(home/"Library"/"Application Support"/"Kirin OS");
#else
    roots.push_back(home/".config"/"Kirin OS");
    roots.push_back(home/"Library"/"Application Support"/"Kirin OS");
#endif
#endif
    return true;
}
bool safeChildren (const fs::path& root, const fs::path& candidate)
{
    // Root may itself be an OS canonical alias (/var -> /private/var). Reject
    // child links/junctions rather than following them. This is a preflight,
    // NOT protection against a concurrent replacement after the check.
    auto walk=root;
    const auto relative=candidate.lexically_relative(root);
    for (const auto& component:relative)
    {
        if(component=="..") return false;
        walk/=component;
        std::error_code error;
        const auto status=fs::symlink_status(walk,error);
        if (error && error!=std::errc::no_such_file_or_directory) return false;
        if (fs::is_symlink(status)) return false;
#if JUCE_WINDOWS
        const auto attributes=GetFileAttributesW(walk.c_str());
        if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0) return false;
#endif
    }
    return true;
}
bool allowed (const juce::File& destination, const FixturePolicy& policy)
{
    if(policy.allowedRoot.isEmpty() || !juce::File::isAbsolutePath(policy.allowedRoot)) return false;
    const auto rawRoot=pathOf(juce::File(policy.allowedRoot));
    std::error_code error;
    if(!fs::is_directory(rawRoot,error) || error) return false;
    fs::path root,target;
    if(!canonical(rawRoot,root)||!canonical(pathOf(destination),target)) return false;
    if(root==root.root_path() || target==root || !contains(root,target)) return false;
    std::vector<fs::path> protectedRoots;
    if(!realProtectedRoots(protectedRoots)) return false;
    for(const auto& p:policy.additionalProtectedRoots) protectedRoots.push_back(pathOf(p));
    for(const auto& p:protectedRoots)
    {
        fs::path protectedPath;
        if(!canonical(p,protectedPath)) return false;
        if(contains(root,protectedPath)||contains(protectedPath,root)
            ||contains(protectedPath,target)) return false;
    }
    // Inspect original lexical descendants as well as canonical containment.
    const auto raw=pathOf(destination).lexically_normal();
    if(contains(rawRoot.lexically_normal(),raw)) return safeChildren(rawRoot.lexically_normal(),raw);
    if(contains(root,raw)) return safeChildren(root,raw);
    return false;
}
}
bool permitMutation (const juce::File& destination)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto policy=fixture.value_or(FixturePolicy{
        juce::SystemStats::getEnvironmentVariable("KIRIN_HYPHA_TEST_STORAGE_ROOT",{}),{}});
    try { if(!allowed(destination,policy)) return false; }
    catch (...) { return false; }
    ++permitted; return true;
}
ScopedFixturePolicy::ScopedFixturePolicy (FixturePolicy policy)
{
    const std::lock_guard<std::mutex> lock(mutex);
    previous=fixture; fixture=std::move(policy);
}
ScopedFixturePolicy::~ScopedFixturePolicy()
{
    const std::lock_guard<std::mutex> lock(mutex); fixture=std::move(previous);
}
std::uint64_t permittedMutationEntries()
{
    const std::lock_guard<std::mutex> lock(mutex); return permitted;
}
}
