#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "kirin_hypha_ffi.h" // C ABI to the Rust RT-measure engine (Phase 1 / B-052)

// Phase 2 後段 段C: PRE implementation. processBlock is a read-only passthrough (R-12)
// that derives signal state and feeds kirin_hypha_push_samples; a minimal Watch editor
// polls kirin_hypha_poll_result for LUFS-M / True Peak / Crest. Mirrors the existing
// nih-plug PRE (crates/hypha_pre) behaviour without reimplementing any measurement.
class KirinHyphaPREProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater
{
public:
    KirinHyphaPREProcessor();
    ~KirinHyphaPREProcessor() override;

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

    // B-070: enable PRE plugin_data writes exactly once, on the message thread, after both
    // prepareToPlay (create + set_license) and any setStateInformation (identity restore)
    // have run. Triggered from the first processBlock via AsyncUpdater because JUCE does not
    // guarantee setStateInformation precedes prepareToPlay, and enable_pre_writes spawns an
    // io_thread (not RT-safe, so it must not run on the audio thread).
    void handleAsyncUpdate() override;
    std::atomic<bool> writesEnabled { false };         // PRE writes enabled (idempotent guard)

    juce::AudioParameterBool* bypassParam = nullptr;   // owned by AudioProcessor (addParameter)
    std::vector<float> interleaveScratch;              // pre-allocated in prepareToPlay (RT-safe; no alloc in processBlock)

    juce::CriticalSection handleLock;                  // guards hyphaHandle vs editor poll / create / destroy
    KirinHypha* hyphaHandle = nullptr;                 // owned; created in prepareToPlay, destroyed in releaseResources/dtor

    // B-069: persisted identity (4 keys) round-tripped via get/setStateInformation as a
    // JUCE-native XML chunk. Source of truth for the chunk; synced from the FFI at enable
    // (B-070 handleAsyncUpdate get_identity readback). Empty until restored or generated.
    juce::String persistInstanceId, persistProjectUuid, persistDawSessionUuid, persistName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaPREProcessor)
};
