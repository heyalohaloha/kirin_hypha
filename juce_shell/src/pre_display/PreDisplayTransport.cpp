#include "PreDisplayController.h"

#include <mutex>

namespace hypha::pre_display
{
    namespace
    {
        std::mutex testRootLock;
        juce::File testRoot;  // placeUnderForTest が置いた試験の home（空なら本物の場所）
    }

    void Controller::placeUnderForTest (const juce::File& root)
    {
        const std::lock_guard<std::mutex> guard (testRootLock);
        testRoot = root;
    }

    juce::File Controller::transportRoot()
    {
        {
            const std::lock_guard<std::mutex> guard (testRootLock);
            if (testRoot != juce::File())
                return testRoot.getChildFile ("Kirin OS").getChildFile ("plugin_data")
                               .getChildFile ("pre_display").getChildFile ("v1");
        }
       #if JUCE_WINDOWS
        auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        if (local.isEmpty())
            local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                        .getFullPathName();
        if (local.isEmpty())
        {
            const auto profile = juce::SystemStats::getEnvironmentVariable ("USERPROFILE", {});
            if (profile.isNotEmpty())
                local = juce::File (profile).getChildFile ("AppData").getChildFile ("Local")
                                            .getFullPathName();
        }
        if (local.isEmpty())
            return {};
        return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data")
                                 .getChildFile ("pre_display").getChildFile ("v1");
       #else
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        if (home.getFullPathName().isEmpty())
            return {};
        return home
            .getChildFile ("Library").getChildFile ("Application Support")
            .getChildFile ("Kirin OS").getChildFile ("plugin_data")
            .getChildFile ("pre_display").getChildFile ("v1");
       #endif
    }
}
