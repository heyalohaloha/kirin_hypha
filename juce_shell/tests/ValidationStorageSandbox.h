#pragma once

#include <juce_core/juce_core.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

class ValidationStorageSandbox
{
public:
    ValidationStorageSandbox()
    {
       #if JUCE_WINDOWS
        const auto processId = ::_getpid();
       #else
        const auto processId = ::getpid();
       #endif
        root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("kirin-hypha-audio-transparency-"
                                             + juce::String (processId), {}, false);
        const auto result = root.createDirectory();
        if (result.failed())
            throw std::runtime_error ("could not create isolated validation storage");
       #if JUCE_WINDOWS
        // Windows Watch snapshots use TEMP, identity uses APPDATA, Record uses
        // LOCALAPPDATA. Isolating only the latter leaves discovery in the user's tree.
        for (const auto* name : { "LOCALAPPDATA", "APPDATA", "TEMP", "TMP" })
        {
            SavedVariable saved { name, {}, false };
            char* current = nullptr;
            size_t length = 0;
            if (::_dupenv_s (&current, &length, name) == 0 && current != nullptr)
            {
                saved.value = current;
                saved.present = true;
                std::free (current);
            }
            variables.push_back (std::move (saved));
            const auto destination = root.getChildFile (
                juce::String (name) == "TMP" ? "TEMP" : name);
            if (destination.createDirectory().failed()
                || ::_putenv_s (name, destination.getFullPathName().toRawUTF8()) != 0)
                throw std::runtime_error ("could not redirect validation storage");
        }
       #else
        // The Rust storage adapter uses HOME and TMPDIR on macOS. Redirect both before loading
        // either bundle so validation cannot discover or modify the user's Kirin OS state.
        for (const auto* name : { "HOME", "TMPDIR" })
        {
            const auto* current = std::getenv (name);
            SavedVariable saved { name, current != nullptr ? current : "", current != nullptr };
            variables.push_back (std::move (saved));
            const auto destination = root.getChildFile (juce::String (name));
            if (destination.createDirectory().failed()
                || ::setenv (name, destination.getFullPathName().toRawUTF8(), 1) != 0)
                throw std::runtime_error ("could not redirect validation storage");
        }
       #endif
    }

    ~ValidationStorageSandbox()
    {
        for (const auto& saved : variables)
       #if JUCE_WINDOWS
            ::_putenv_s (saved.name.c_str(), saved.present ? saved.value.c_str() : "");
       #else
            if (saved.present)
                ::setenv (saved.name.c_str(), saved.value.c_str(), 1);
            else
                ::unsetenv (saved.name.c_str());
       #endif
        root.deleteRecursively();
    }

private:
    struct SavedVariable { std::string name, value; bool present; };
    juce::File root;
    std::vector<SavedVariable> variables;
};
