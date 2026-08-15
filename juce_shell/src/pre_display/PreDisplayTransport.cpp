#include "PreDisplayController.h"

namespace hypha::pre_display
{
    juce::File Controller::transportRoot()
    {
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
