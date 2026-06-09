#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "kirin_hypha_ffi.h" // C ABI to the Rust RT-measure engine (Phase 1 / B-052)

// Role-parameterized base for both the Kirin Hypha PRE and POST JUCE shells (B-070).
// All FFI wiring (create / set_license / push_samples / poll_result), the identity state
// chunk, the deferred enable (AsyncUpdater), and the R-12 read-only passthrough live here.
// The Role selects enable_pre_writes vs enable_post_writes and the display name; the two
// plugin targets differ solely by the Role passed in their createPluginFilter()
// (src/PluginMainPRE.cpp / src/PluginMainPOST.cpp). POST also exposes the pairing surface
// (B-072: set_pair_target / keep / stop / is_recording); the editor only uses it for POST.
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

    // --- B-072: POST pairing surface (used by the editor only when isPostRole()) ----------
    bool isPostRole() const { return role == Role::Post; }
    int  licenseCode() const { return cachedLicenseCode.load (std::memory_order_acquire); } // 0=Os 1=Sense 2=Unknown
    bool isRecording() const;                          // FFI kirin_hypha_is_recording (Watch/Record toggle)
    juce::String pairName() const { return persistPairName; }
    void setPairName (const juce::String& name);       // persist + set_pair_target (sanitized in FFI)
    bool keepPair();                                    // kirin_hypha_keep (Os + unique PRE)
    void stopPair();                                    // kirin_hypha_stop

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
    // prepareToPlay (create + set_license) and any setStateInformation (identity + pair name
    // restore) have run. Triggered from the first processBlock via AsyncUpdater because JUCE
    // does not guarantee setStateInformation precedes prepareToPlay, and enable_*_writes
    // spawns an io_thread (not RT-safe). The Role selects PRE/POST.
    void handleAsyncUpdate() override;

    const Role role;                                   // Pre or Post (selects enable + display name)

    juce::AudioParameterBool* bypassParam = nullptr;   // owned by AudioProcessor (addParameter)
    std::vector<float> interleaveScratch;              // pre-allocated in prepareToPlay (RT-safe; no alloc in processBlock)

    juce::CriticalSection handleLock;                  // guards hyphaHandle vs editor poll / create / destroy
    KirinHypha* hyphaHandle = nullptr;                 // owned; created in prepareToPlay, destroyed in releaseResources/dtor

    // Persisted identity (4 keys) + POST pair target name, round-tripped via get/setState as a
    // JUCE-native XML chunk. Source of truth for the chunk; synced from the FFI at enable
    // (B-070/B-072 handleAsyncUpdate). Empty until restored or generated.
    juce::String persistInstanceId, persistProjectUuid, persistDawSessionUuid, persistName;
    juce::String persistPairName;                      // POST pair target (B-072)

    std::atomic<int>  cachedLicenseCode { 2 };         // B-072: license read once in prepareToPlay (0=Os)
    std::atomic<bool> writesEnabled { false };         // plugin_data writes enabled (idempotent guard)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaProcessorBase)
};
