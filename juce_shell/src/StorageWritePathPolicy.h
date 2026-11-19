#pragma once
#include <juce_core/juce_core.h>
#include <filesystem>
#include <vector>
#if ! JUCE_WINDOWS
#include <pwd.h>
#include <unistd.h>
#endif
namespace hypha::storage::write_guard {
struct FixturePolicy { juce::String allowedRoot; std::vector<juce::File> additionalProtectedRoots; };
namespace detail {
namespace fs = std::filesystem;
#if JUCE_WINDOWS
bool windowsProtectedRoots (std::vector<fs::path>&);
bool windowsReparsePoint (const fs::path&);
#endif
inline fs::path pathOf (const juce::File& file)
{
#if JUCE_WINDOWS
    return fs::path(file.getFullPathName().toWideCharPointer());
#else
    return fs::path(file.getFullPathName().toRawUTF8());
#endif
}
inline bool canonical (const fs::path& input, fs::path& output)
{
    std::error_code error; output = fs::weakly_canonical(input,error);
    return !error && output.is_absolute();
}
inline bool contains (const fs::path& parent, const fs::path& child)
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
inline bool realProtectedRoots (std::vector<fs::path>& roots)
{
#if JUCE_WINDOWS
    return windowsProtectedRoots (roots);
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
inline bool safeChildren (const fs::path& root, const fs::path& candidate)
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
        if (windowsReparsePoint (walk)) return false;
#endif
    }
    return true;
}
inline bool allowed (const juce::File& destination, const FixturePolicy& policy)
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
}
