#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "kirin_hypha_ffi.h" // C ABI to the Rust RT-measure engine (Phase 1 / B-052)

// Role-parameterized base for both the Kirin Hypha PRE and POST JUCE shells (B-070).
// All FFI wiring (create / set_license / push_samples / poll_result), the identity state
// chunk, the deferred enable (B-126: lock-free flag + non-RT Timer), and the R-12 read-only
// passthrough live here.
// The Role selects enable_pre_writes vs enable_post_writes and the display name; the two
// plugin targets differ solely by the Role passed in their createPluginFilter()
// (src/PluginMainPRE.cpp / src/PluginMainPOST.cpp). POST also exposes the pairing surface
// (B-072: set_pair_target / keep / stop / is_recording) and the Δ readout (B-073:
// poll_delta + cached signal state); the editor uses those only for POST.
class KirinHyphaProcessorBase : public juce::AudioProcessor,
                                private juce::Timer
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
    bool recordExclusionConflict() const;               // B-118 (②): kirin_hypha_record_exclusion_conflict (keep pre-check)
    juce::String recordErrorMessage() const;            // B-118 (③): kirin_hypha_record_error_message (io fail status / G-115-29)
    juce::String pathAnomalyMessage() const;            // B-128 (G-115-371 D3): kirin_hypha_drain_path_event (restore identity anomaly surface)
    bool licenseIsOs() const;                           // B-118 (①): kirin_hypha_load_license()==Os
    void stopPair();                                    // kirin_hypha_stop

    // --- B-073: POST Δ readout (editor display branching) --------------------------------
    int  signalStateLive() const;                      // B-113: FFI kirin_hypha_get_signal_state (heartbeat-aware, no stale Active)
    bool pollDelta (KirinDelta& out) const;            // FFI kirin_hypha_poll_delta (mode + Δ values)
    bool isPlaying() const { return lastPlaying.load (std::memory_order_acquire); } // transport (POST pair lock)

    // --- B-054: PRE live name + LED pollers (egui parity) --------------------------------
    juce::String preName() const   { return persistName; }       // PRE self name (= identity.name)
    juce::String instanceId() const { return persistInstanceId; } // for the empty-name 8-char fallback
    void setPreName (const juce::String& name);                   // persist + kirin_hypha_set_pre_name (live, sanitized in FFI)
    // (isRecording() is declared in the B-072 block above; record_sm reflects PRE autonomous record too.)
    bool measureAlive() const;      // FFI kirin_hypha_measure_alive (LED Error state)
    bool heartbeatLive() const;     // B-115: FFI kirin_hypha_heartbeat_live (processBlock liveness / POST pair lock)
    bool recordAcknowledged() const;// FFI kirin_hypha_record_acknowledged (PRE Keeping banner / RecordActive LED)
    bool presetAvailable() const;   // FFI kirin_hypha_preset_available (PresetAvailable LED)
    bool addAnnotation (const juce::String& memo); // FFI kirin_hypha_add_annotation (POST Note → Good/Fix/Hold)

    // --- B-102: POST broadcast (All Keep / All Stop) + candidate enumeration (new↔new) -----
    struct PreCandidate { juce::String instanceId; juce::String name; bool hasName = false; };
    bool keepAll();                                       // FFI kirin_hypha_keep_all (broadcast + self keep)
    void stopAll();                                       // FFI kirin_hypha_stop_all (broadcast + self stop)
    juce::Array<PreCandidate> enumeratePreCandidates() const; // FFI kirin_hypha_enumerate_pre_candidates
    int keepReadyCount() const;                               // FFI kirin_hypha_count_keep_ready (egui n_ready)

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
    static bool bufferIsSilent (const juce::AudioBuffer<float>& buffer); // B-107: peak < -140 dBFS (parity)

    // B-070/B-126 + Logic stopped-state fix: enable plugin_data writes exactly once, on the message thread, after
    // prepareToPlay (create + set_license). processBlock only sets lock-free flags; it no longer
    // short-circuits the state-restore grace window, because enable_*_writes snapshots the path
    // identity into the io_thread. Hosts such as Logic may not call processBlock while stopped, so
    // the Timer publishes Inactive PRE/POST presence after either setStateInformation arrives or the
    // restore grace expires. enable_*_writes spawns an io_thread (not RT-safe), hence the deferral.
    void timerCallback() override;        // B-126: non-RT poll (message thread) — observes enablePending
    void enableWritesNow();               // B-070 enable body (set_identity -> enable_*_writes -> readback)

    const Role role;                                   // Pre or Post (selects enable + display name)

    juce::AudioParameterBool* bypassParam = nullptr;   // owned by AudioProcessor (addParameter)
    std::vector<float> interleaveScratch;              // pre-allocated in prepareToPlay (RT-safe; no alloc in processBlock)
    size_t scratchCapacitySamples = 0;                 // B-125: cached interleaveScratch capacity (prealloc-max); processBlock's oversized guard

    juce::CriticalSection handleLock;                  // guards hyphaHandle vs editor poll / create / destroy
    KirinHypha* hyphaHandle = nullptr;                 // owned; created in prepareToPlay, destroyed in releaseResources/dtor

    // Persisted identity (4 keys) + POST pair target name, round-tripped via get/setState as a
    // JUCE-native XML chunk. Source of truth for the chunk; synced from the FFI at enable
    // (B-070/B-072 enableWritesNow). Empty until restored or generated.
    juce::String persistInstanceId, persistProjectUuid, persistDawSessionUuid, persistName;
    juce::String persistPairName;                      // POST pair target (B-072)

    std::atomic<int>  cachedLicenseCode { 2 };         // B-072: license read once in prepareToPlay (0=Os)
    std::atomic<bool> lastPlaying { false };           // B-054: transport playing (POST pair lock during playback)
    std::atomic<bool> writesEnabled { false };         // plugin_data writes enabled (idempotent guard)
    std::atomic<bool> enablePending { false };         // B-126: set by prepare/processBlock, observed by the Timer
    std::atomic<int>  enableDelayTicks { 0 };          // prepare fallback restore grace; setState clears it
    std::atomic<bool> stateInformationSeen { false };  // setStateInformation reached this instance at least once

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KirinHyphaProcessorBase)
};
