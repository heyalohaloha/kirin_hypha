#pragma once
#include <juce_core/juce_core.h>
#include <filesystem>
namespace hypha::tests {
// Both endpoints belong to the caller's disposable fixture. No real folder relocation.
inline bool directoryAliasCommand(const juce::String& arguments) {
    juce::ChildProcess child;
    if(!child.start("cmd.exe /d /c " + arguments)) return false;
    if(!child.waitForProcessToFinish(3000)) { child.kill(); return false; }
    return child.getExitCode()==0;
}
inline bool createDisposableDirectoryAlias(const juce::File& target,const juce::File& alias) {
#if JUCE_WINDOWS
    return directoryAliasCommand("mklink /J "+alias.getFullPathName().quoted()+" "+target.getFullPathName().quoted());
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target.getFullPathName().toStdString(),alias.getFullPathName().toStdString(),error);
    return !error;
#endif
}
inline bool removeDisposableDirectoryAlias(const juce::File& alias) {
#if JUCE_WINDOWS
    // Deliberately no /S: only remove the junction, never traverse its target.
    return directoryAliasCommand("rmdir "+alias.getFullPathName().quoted());
#else
    std::error_code error;
    return std::filesystem::remove(alias.getFullPathName().toStdString(),error) && !error;
#endif
}
}
