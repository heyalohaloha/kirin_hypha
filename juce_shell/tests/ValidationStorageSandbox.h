#pragma once

#include "TestTemporaryDirectory.h"
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
    // 試験用のフォルダ。ここへ書くものは、書く前にこの中かを確かめる。
    const juce::File& directory() const noexcept { return root; }
    ValidationStorageSandbox()
    {
       #if JUCE_WINDOWS
        const auto processId = ::_getpid();
       #else
        const auto processId = ::getpid();
       #endif
        root = hypha::tests::temporaryParent()
                   .getNonexistentChildFile ("kirin-hypha-audio-transparency-"
                                             + juce::String (processId), {}, false);
        const auto result = root.createDirectory();
        if (result.failed())
            throw std::runtime_error ("could not create isolated validation storage");
        // Keep the same layout as Rust PlatformPaths::under_test_root. Existing fixture
        // readers using HOME/APPDATA/TMPDIR and JUCE workers using the override agree.
        try
        {
           #if JUCE_WINDOWS
            redirect ("APPDATA", root.getChildFile ("home/AppData/Roaming"));
            redirect ("LOCALAPPDATA", root.getChildFile ("home/AppData/Local"));
            redirect ("TEMP", root.getChildFile ("tmp"));
            redirect ("TMP", root.getChildFile ("tmp"));
           #else
            redirect ("HOME", root.getChildFile ("home"));
            redirect ("TMPDIR", root.getChildFile ("tmp"));
           #endif
            redirect ("KIRIN_HYPHA_TEST_STORAGE_ROOT", root);
        }
        catch (...)
        {
            restore();
            root.deleteRecursively();
            throw;
        }
    }

    ~ValidationStorageSandbox()
    {
        restore();
        root.deleteRecursively();
    }

private:
    struct SavedVariable { std::string name, value; bool present; };

    void restore() noexcept
    {
        for (auto it = variables.rbegin(); it != variables.rend(); ++it)
        {
           #if JUCE_WINDOWS
            ::_putenv_s (it->name.c_str(), it->present ? it->value.c_str() : "");
           #else
            if (it->present)
                ::setenv (it->name.c_str(), it->value.c_str(), 1);
            else
                ::unsetenv (it->name.c_str());
           #endif
        }
    }

    void redirect (const char* name, const juce::File& destination)
    {
        SavedVariable saved { name, {}, false };
       #if JUCE_WINDOWS
        char* current = nullptr;
        size_t length = 0;
        if (::_dupenv_s (&current, &length, name) == 0 && current != nullptr)
        {
            saved.value = current;
            saved.present = true;
            std::free (current);
        }
       #else
        if (const auto* current = std::getenv (name))
        {
            saved.value = current;
            saved.present = true;
        }
       #endif
        variables.push_back (std::move (saved));
        if (destination.createDirectory().failed())
            throw std::runtime_error ("could not create isolated validation storage");
       #if JUCE_WINDOWS
        const auto error = ::_putenv_s (name, destination.getFullPathName().toRawUTF8());
       #else
        const auto error = ::setenv (name, destination.getFullPathName().toRawUTF8(), 1);
       #endif
        if (error != 0)
            throw std::runtime_error ("could not redirect validation storage");
    }

    juce::File root;
    std::vector<SavedVariable> variables;
};
