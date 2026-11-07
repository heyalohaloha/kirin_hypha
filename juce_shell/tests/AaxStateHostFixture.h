#pragma once
#include "../src/PluginProcessor.h"
#include <array>
#include <cstring>
#include <vector>
#include <thread>

namespace hypha::tests::aax_state
{
using juce::AudioProcessor;
using AAX_CTypeID = std::uint32_t;
using AAX_Result = int;
constexpr AAX_Result AAX_SUCCESS = 0;
constexpr AAX_CTypeID juceChunkType = 0x1234;
struct AAX_SPlugInChunk
{
    std::int32_t fSize = 0;
    std::array<char, 65536> storage {};
    char* fData = storage.data();
};
// Interface stand-ins only. Callback bodies below are extracted from the built JUCE wrapper.
// Parameter automation and SDK ABI layout are covered separately; these settings are non-parameter state.
struct AAX_CEffectParameters
{
    virtual ~AAX_CEffectParameters() = default;
    virtual AAX_Result GetChunkSize (AAX_CTypeID, std::uint32_t*) const { return 37; }
    virtual AAX_Result GetChunk (AAX_CTypeID, AAX_SPlugInChunk*) const { return 37; }
    virtual AAX_Result SetChunk (AAX_CTypeID, const AAX_SPlugInChunk*) { return 37; }
    virtual AAX_Result GetNumberOfChanges (std::int32_t* changes) const { *changes = mNumPlugInChanges; return AAX_SUCCESS; }
protected:
    std::int32_t mNumPlugInChanges = 0; // the SDK base counter is also incremented by the JUCE callback
};
class Host final : public AAX_CEffectParameters, public juce::AudioProcessorListener
{
public:
    explicit Host (juce::AudioProcessor& processor) : pluginInstance (&processor) { processor.addListener (this); }
    ~Host() override { pluginInstance->removeListener (this); }
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    #include "AaxStateChunkBody.h"
    #include "AaxStateChangesBody.h"
    #include "AaxStateDirtyBody.h"
private:
    struct CachedState { juce::MemoryBlock data; bool isValid = false; };
    mutable juce::ThreadLocalValue<CachedState> perThreadFilterData;
    struct Parameters : std::vector<juce::AudioProcessorParameter*>
    {
        int getNumParameters() const { return static_cast<int> (size()); }
        juce::AudioProcessorParameter* getParamForIndex (int index) { return at (static_cast<std::size_t> (index)); }
    } juceParameters;
    struct ParameterManager { int* GetParameterByID (const char*) { return nullptr; } } mParameterManager;
    struct ControllerStub { int SetSignalLatency (int) { return 0; } } controller;
    ControllerStub* Controller() { return &controller; }
    static const char* getAAXParamIDFromJuceIndex (int) { return nullptr; }
    static void SetParameterNormalizedValue (const char*, float) {}
    static void syncParameterAttributes (int*, const juce::AudioProcessorParameter*) {}
    static void check (int) {}
    juce::AudioProcessor* pluginInstance;
    std::unique_ptr<juce::AudioProcessorParameter> ownedBypassParameter;
    std::int32_t numSetDirtyCalls = 0;
};

template <typename Require>
juce::MemoryBlock save (Host& host, Require require)
{
    std::uint32_t size = 0;
    require (host.GetChunkSize (juceChunkType, &size) == AAX_SUCCESS, "AAX asks the real wrapper to snapshot its chunk");
    AAX_SPlugInChunk chunk;
    AAX_Result foreignResult = AAX_SUCCESS;
    std::thread foreign ([&] { foreignResult = host.GetChunk (juceChunkType, &chunk); });
    foreign.join();
    require (foreignResult != AAX_SUCCESS, "another thread cannot consume this thread's AAX snapshot");
    require (size <= chunk.storage.size(), "fixture bounds the AAX destination");
    require (host.GetChunk (juceChunkType, &chunk) == AAX_SUCCESS && chunk.fSize == static_cast<int> (size),
             "AAX receives the exact chunk snapshot and size");
    juce::MemoryBlock saved (chunk.fData, size);
    require (host.GetChunk (juceChunkType, &chunk) != AAX_SUCCESS, "the wrapper cannot reuse an already consumed snapshot");
    return saved;
}

template <typename Require>
void restore (juce::AudioProcessor& processor, const juce::MemoryBlock& saved, Require require)
{
    Host host (processor);
    AAX_SPlugInChunk chunk;
    require (saved.getSize() <= chunk.storage.size(), "fixture bounds the AAX restore input");
    std::memcpy (chunk.fData, saved.getData(), saved.getSize());
    chunk.fSize = static_cast<int> (saved.getSize());
    require (host.SetChunk (juceChunkType, &chunk) == AAX_SUCCESS, "AAX restores through the actual SetChunk method");
    require (host.SetChunk (0, &chunk) == 37, "foreign chunk IDs preserve the SDK delegation");
}
}
