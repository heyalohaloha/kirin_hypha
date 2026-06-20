#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static OSType fourcc(const char* s)
{
    return ((UInt32) s[0] << 24) | ((UInt32) s[1] << 16) | ((UInt32) s[2] << 8) | (UInt32) s[3];
}

static void printStatus(const char* label, OSStatus status)
{
    if (status == noErr)
        return;

    std::fprintf(stderr, "%s: OSStatus %d\n", label, (int) status);
}

static void printChannelInfo(AudioUnit unit)
{
    UInt32 size = 0;
    Boolean writable = false;
    OSStatus status = AudioUnitGetPropertyInfo(unit,
                                               kAudioUnitProperty_SupportedNumChannels,
                                               kAudioUnitScope_Global,
                                               0,
                                               &size,
                                               &writable);
    if (status != noErr)
    {
        printStatus("SupportedNumChannels info", status);
        return;
    }

    const auto count = size / sizeof(AUChannelInfo);
    auto* infos = (AUChannelInfo*) std::calloc(count, sizeof(AUChannelInfo));
    status = AudioUnitGetProperty(unit,
                                  kAudioUnitProperty_SupportedNumChannels,
                                  kAudioUnitScope_Global,
                                  0,
                                  infos,
                                  &size);
    if (status != noErr)
    {
        printStatus("SupportedNumChannels get", status);
        std::free(infos);
        return;
    }

    std::printf("SupportedNumChannels:");
    for (UInt32 i = 0; i < count; ++i)
        std::printf(" [%d,%d]", infos[i].inChannels, infos[i].outChannels);
    std::printf("\n");
    std::free(infos);
}

static void printLayoutTags(AudioUnit unit, AudioUnitScope scope, const char* label)
{
    UInt32 size = 0;
    Boolean writable = false;
    OSStatus status = AudioUnitGetPropertyInfo(unit,
                                               kAudioUnitProperty_SupportedChannelLayoutTags,
                                               scope,
                                               0,
                                               &size,
                                               &writable);
    if (status != noErr)
    {
        printStatus(label, status);
        return;
    }

    const auto count = size / sizeof(AudioChannelLayoutTag);
    auto* tags = (AudioChannelLayoutTag*) std::calloc(count, sizeof(AudioChannelLayoutTag));
    status = AudioUnitGetProperty(unit,
                                  kAudioUnitProperty_SupportedChannelLayoutTags,
                                  scope,
                                  0,
                                  tags,
                                  &size);
    if (status != noErr)
    {
        printStatus(label, status);
        std::free(tags);
        return;
    }

    std::printf("%s:", label);
    for (UInt32 i = 0; i < count; ++i)
        std::printf(" 0x%06x", tags[i]);
    std::printf("\n");
    std::free(tags);
}

static AudioStreamBasicDescription makeFloatFormat(UInt32 channels)
{
    AudioStreamBasicDescription d {};
    d.mSampleRate = 44100.0;
    d.mFormatID = kAudioFormatLinearPCM;
    d.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    d.mBytesPerPacket = sizeof(float);
    d.mFramesPerPacket = 1;
    d.mBytesPerFrame = sizeof(float);
    d.mChannelsPerFrame = channels;
    d.mBitsPerChannel = 32;
    return d;
}

static void setStream(AudioUnit unit, AudioUnitScope scope, UInt32 channels)
{
    auto desc = makeFloatFormat(channels);
    OSStatus status = AudioUnitSetProperty(unit,
                                           kAudioUnitProperty_StreamFormat,
                                           scope,
                                           0,
                                           &desc,
                                           sizeof(desc));
    printStatus(scope == kAudioUnitScope_Input ? "set input stream" : "set output stream", status);
}

static void dump(AudioUnit unit, const char* label)
{
    std::printf("\n== %s ==\n", label);
    printChannelInfo(unit);
    printLayoutTags(unit, kAudioUnitScope_Input, "InputLayoutTags");
    printLayoutTags(unit, kAudioUnitScope_Output, "OutputLayoutTags");
}

int main(int argc, char** argv)
{
    const char* subtype = argc > 1 ? argv[1] : "Khpr";

    AudioComponentDescription desc {};
    desc.componentType = kAudioUnitType_Effect;
    desc.componentSubType = fourcc(subtype);
    desc.componentManufacturer = fourcc("Kirn");

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr)
    {
        std::fprintf(stderr, "component not found: %s\n", subtype);
        return 2;
    }

    AudioUnit unit = nullptr;
    OSStatus status = AudioComponentInstanceNew(comp, &unit);
    if (status != noErr || unit == nullptr)
    {
        printStatus("AudioComponentInstanceNew", status);
        return 3;
    }

    dump(unit, "initial");
    setStream(unit, kAudioUnitScope_Input, 2);
    setStream(unit, kAudioUnitScope_Output, 2);
    dump(unit, "after stereo stream");
    setStream(unit, kAudioUnitScope_Input, 1);
    setStream(unit, kAudioUnitScope_Output, 1);
    dump(unit, "after mono stream");

    AudioComponentInstanceDispose(unit);
    return 0;
}
