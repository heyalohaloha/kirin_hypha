#pragma once
#include <juce_core/juce_core.h>
#include <filesystem>
#include <stdexcept>

namespace hypha::tests
{
// Record before fixture teardown, so Rust-created files are still visible to CTest's
// final path audit. std::filesystem also enumerates paths beyond JUCE's Windows limit.
inline void recordPathsBeforeRemoval (const juce::File& directory)
{
    const auto ledger = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_PATH_LEDGER", {});
    if (ledger.isEmpty()) return;
    const auto root = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_PATH_BUDGET_ROOT", {});
    if (root.isEmpty()) throw std::runtime_error ("path audit root is missing");
    const auto base = std::filesystem::u8path (root.toStdString());
    const auto target = std::filesystem::u8path (directory.getFullPathName().toStdString());
    juce::Array<juce::var> entries;
    const auto add = [&] (const std::filesystem::path& file)
    {
        const auto relative = file.lexically_relative (base).generic_u8string();
        if (relative.empty() || relative == "." || relative == ".." || relative.rfind ("../", 0) == 0)
            throw std::runtime_error ("fixture removal is outside the path audit root");
        entries.add (juce::String::fromUTF8 (relative.c_str()));
    };
    if (std::filesystem::exists (std::filesystem::symlink_status (target)))
    {
        add (target);
        if (std::filesystem::is_directory (target) && ! std::filesystem::is_symlink (target))
            for (const auto& entry : std::filesystem::recursive_directory_iterator (target)) add (entry.path());
    }
    const auto report = juce::File (ledger).getChildFile (juce::Uuid().toString() + ".json");
    juce::FileOutputStream stream (report);
    if (! stream.openedOk() || ! stream.writeText (juce::JSON::toString (entries), false, false, "\n"))
        throw std::runtime_error ("could not record fixture paths");
    stream.flush();
    if (stream.getStatus().failed()) throw std::runtime_error ("could not flush fixture paths");
}

inline bool removeFixtureDirectory (const juce::File& directory)
{
    recordPathsBeforeRemoval (directory);
    return directory.deleteRecursively();
}
}
