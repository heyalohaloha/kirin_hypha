#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "kirin_hypha_ffi.h" // C ABI to the Rust RT-measure engine (Phase 1 / B-052)

// Role-parameterized base for both the Kirin Hypha PRE and POST JUCE shells (B-070).
// All FFI wiring (create / set_license / push_samples / poll_result), the identity state
// chunk, the deferred enable (AsyncUpdater), and the R-12 read-only passthrough live here.
// The Role only selects enable_pre_writes vs enable_post_writes and the display name; the
// two plugin targets differ solely by the Role passed in their createPluginFilter()
// (src/PluginMainPRE.cpp / src/PluginMainPOST.cpp). This keeps PRE and POST from diverging.
class KirinHyphaProcessorBase : public juce::AudioProcessor,
                                private juce::AsyncUpdater
{
public:
    enum class Role { Pre, Post };

    explicit KirinHyphaProcessorBase (Role role);
    ~KirinHyphaProcessorBase() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Editor (message thread) reads the latest RT result. Null handle / lock contention -> false.
    bool pollMeasureResult (KirinMeasureResult& out) const;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    static bool bufferIsSilent (const juce::AudioBuffer<float>& buffer); // strict all-zero (parity)

    // B-070: enable plugin_data writes exactly once, on the message thread, after both
    // prepareToPlay (create + set_license) and any setStateInformation (identity restore)
    // have run. Triggered from the first processBlock via AsyncUpdater because JUCE does not
    // guarantee setStateInformation precedes prepareToPlay, and enable_*_writes spawns an
    // io_thread (not RT-safe, so it must not run on the audio thread). The Role selects PRE
    // (enable_pre_writes) or POST (enable_post_writes).
    void handleAsyncUpdate() override;

    const Role role;                                   // Pre or Post (selects enable + display name)

    juce::AudioParameterBool* bypassParam = nullptr;   // owned by AudioProcessor (addParameter)
    std::vector<float> interleaveScratch;              // pre-allocated in prepareToPlay (RT-safe; no alloc in processBlock)

    juce::CriticalSection handleLock;                  // guards hyphaHandle vs editor poll / create / destroy
    KirinHypha* hyphaHandle = nullptr;                 // owned; created in prepareToPlay, destroyed in releaseResources/dtor

    // Persisted identity (4 keys) round-tripped via get/setStateInformation as a JUCE-native
    // XML chunk. Source of truth for the chunk; synced from the FFI at enable (B-070
    // handleAsyncUpdate get_identity readback). Empty until restored or generated.
    juce::String persistInstanceId, persistProjectUuid, persistDawSessionUuid, persistName;

    std::atomic<bool> writesEnabled { false };         // plugin_data writes enabled (idempotent guard)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaProcessorBase)
};
