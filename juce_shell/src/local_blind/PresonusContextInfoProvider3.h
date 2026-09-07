#pragma once

#include <pluginterfaces/base/funknown.h>
#include <juce_audio_processors/format_types/pslextensions/ipslcontextinfo.h>

namespace hypha::local_blind
{
// Read-compatible declaration of the public-domain PreSonus/Fender extension added in 2019:
// https://github.com/fenderdigital/presonus-plugin-extensions/blob/master/ipslcontextinfo.h
// The JUCE snapshot bundled with this project provides v1/v2 only. Keep this adapter local so a
// future JUCE update can replace it without modifying the submodule or the audio-thread boundary.
struct ContextInfoProvider3 : Presonus::IContextInfoProvider2
{
    virtual Steinberg::tresult PLUGIN_API beginEditContextInfoValue (Steinberg::FIDString id) = 0;
    virtual Steinberg::tresult PLUGIN_API endEditContextInfoValue (Steinberg::FIDString id) = 0;
};

DECLARE_UID (ContextInfoProvider3_iid, 0x4e31fdf8, 0x6f4448d4, 0xb4ec1461, 0x68a4150f)
}
