#include "PluginProcessor.h"
#include "PluginEditor.h"

KirinHyphaProcessorBase::KirinHyphaProcessorBase (Role roleIn)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      role (roleIn)
{
    // Host bypass routed through this parameter; processBlock reads it to set the
    // Bypassed signal state while still passing audio through (parity with hypha_pre).
    addParameter (bypassParam = new juce::AudioParameterBool ({ "bypass", 1 }, "Bypass", false));
}

KirinHyphaProcessorBase::~KirinHyphaProcessorBase()
{
    cancelPendingUpdate(); // B-070: ensure no deferred enable fires during teardown.
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
}

void KirinHyphaProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = getTotalNumInputChannels();

    // Pre-allocate the interleave scratch so processBlock never allocates (RT-safe).
    interleaveScratch.assign ((size_t) juce::jmax (0, samplesPerBlock)
                                  * (size_t) juce::jmax (1, numCh),
                              0.0f);

    const juce::ScopedLock sl (handleLock);
    // Re-prepare is allowed: drop any prior handle before creating a fresh one.
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }

    // num_channels: pass the actual negotiated input channel count (stereo expected).
    hyphaHandle = kirin_hypha_create ((uint32_t) sampleRate, (uint32_t) numCh);

    // B-070: a fresh handle needs license applied and (re-)enabling. License is read once
    // from identity.json (single source) and cached for the editor's Os-gate (B-072).
    // set_identity + enable_*_writes are deferred to the first processBlock
    // (handleAsyncUpdate) so any setStateInformation restore is applied before enable.
    if (hyphaHandle != nullptr)
    {
        const uint8_t lic = kirin_hypha_load_license();
        cachedLicenseCode.store ((int) lic, std::memory_order_release);
        kirin_hypha_set_license (hyphaHandle, lic);
        writesEnabled.store (false, std::memory_order_release);
    }
    // A null handle (create failure) is tolerated; processBlock / pollMeasureResult guard on it.
}

void KirinHyphaProcessorBase::releaseResources()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
}

bool KirinHyphaProcessorBase::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    // Stereo only. The measurement engine consumes 2ch interleaved (kirin_measure),
    // so a mono layout would be accepted by the host and then dropped by the FFI
    // guard, producing audio passthrough with no measurement. Reject it at the bus
    // level instead so only a measurable layout can be negotiated.
    return mainOut == juce::AudioChannelSet::stereo();
}

void KirinHyphaProcessorBase::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh     = getTotalNumInputChannels();
    const int numOut    = getTotalNumOutputChannels();
    const int numFrames = buffer.getNumSamples();

    // R-12: read-only passthrough. Never write to signal channels; only clear surplus
    // output channels with no matching input (no-op when in == out).
    for (int ch = numCh; ch < numOut; ++ch)
        buffer.clear (ch, 0, numFrames);

    // Not locked on the audio thread: JUCE suspends processing around prepare/release,
    // so hyphaHandle is stable here. (The editor poll and create/destroy use handleLock.)
    if (hyphaHandle == nullptr)
        return;

    // B-070: enable writes once, deferred to here so any setStateInformation (identity /
    // pair name restore) has already run. triggerAsyncUpdate is safe from the audio thread
    // and coalesces; the actual enable runs on the message thread (handleAsyncUpdate).
    if (! writesEnabled.load (std::memory_order_acquire))
        triggerAsyncUpdate();

    // --- Signal state derivation (parity: hypha_pre.rs:397-403) -------------------
    const bool bypassed = (bypassParam != nullptr && bypassParam->get());

    bool playing = false;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    lastPlaying.store (playing, std::memory_order_release); // B-054: POST pair lock reads this

    const bool silent = bufferIsSilent (buffer);

    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    uint8_t stateCode;
    if (bypassed)                 stateCode = 2; // Bypassed
    else if (! playing || silent) stateCode = 0; // Inactive
    else                          stateCode = 1; // Active
    kirin_hypha_set_signal_state (hyphaHandle, stateCode);
    lastSignalState.store ((int) stateCode, std::memory_order_release); // B-073: editor display branching

    // --- Feed the engine ---------------------------------------------------------
    // Parity with nih-plug: ring push only when Active; heartbeat advances every block.
    // push_samples advances heartbeat internally, so a 0-frame call is a heartbeat-only
    // keepalive (no ring push) for the Inactive / Bypassed case.
    if (stateCode == 1)
    {
        const size_t needed = (size_t) numFrames * (size_t) numCh;
        if (numCh > 0 && needed <= interleaveScratch.size())
        {
            // Interleave [c0f0, c1f0, c0f1, c1f1, ...] — same order as nih-plug iter_samples.
            // getReadPointer only: the input buffer is never modified (R-12).
            size_t idx = 0;
            for (int f = 0; f < numFrames; ++f)
                for (int ch = 0; ch < numCh; ++ch)
                    interleaveScratch[idx++] = buffer.getReadPointer (ch)[f];

            kirin_hypha_push_samples (hyphaHandle, interleaveScratch.data(),
                                      (size_t) numFrames, (uint32_t) numCh);
        }
        else
        {
            kirin_hypha_push_samples (hyphaHandle, nullptr, 0, (uint32_t) numCh);
        }
    }
    else
    {
        kirin_hypha_push_samples (hyphaHandle, nullptr, 0, (uint32_t) numCh);
    }
}

bool KirinHyphaProcessorBase::bufferIsSilent (const juce::AudioBuffer<float>& buffer)
{
    // Parity with hypha_pre.rs:442-451: true iff every sample in every channel is exactly 0.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* p = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (p[i] != 0.0f)
                return false;
    }
    return true;
}

juce::AudioProcessorEditor* KirinHyphaProcessorBase::createEditor()
{
    return new KirinHyphaEditor (*this);
}

bool KirinHyphaProcessorBase::hasEditor() const          { return true; }

juce::AudioProcessorParameter* KirinHyphaProcessorBase::getBypassParameter() const
{
    return bypassParam;
}

bool KirinHyphaProcessorBase::pollMeasureResult (KirinMeasureResult& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_result (hyphaHandle, &out);
}

// --- B-072: POST pairing surface ---------------------------------------------------------

bool KirinHyphaProcessorBase::isRecording() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_is_recording (hyphaHandle);
}

void KirinHyphaProcessorBase::setPairName (const juce::String& name)
{
    persistPairName = name; // persisted; the FFI sanitizes its own copy (ASCII graphic + space, 16).
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_pair_target (hyphaHandle, name.toRawUTF8());
}

bool KirinHyphaProcessorBase::keepPair()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

void KirinHyphaProcessorBase::stopPair()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_stop (hyphaHandle);
}

// --- B-073: POST Δ readout ---------------------------------------------------------------

bool KirinHyphaProcessorBase::pollDelta (KirinDelta& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_delta (hyphaHandle, &out);
}

// --- B-054: PRE live name + LED pollers ---------------------------------------------------

void KirinHyphaProcessorBase::setPreName (const juce::String& name)
{
    // PRE self name == identity.name: persist it (chunk round-trip) AND push it live to the
    // io_thread via the FFI (sanitized to ASCII graphic + space / 16 there). Mirrors how the
    // egui PRE writes its shared name Arc; persistName keeps DAW save/load consistent.
    persistName = name;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_pre_name (hyphaHandle, name.toRawUTF8());
}

bool KirinHyphaProcessorBase::measureAlive() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_measure_alive (hyphaHandle);
}

bool KirinHyphaProcessorBase::recordAcknowledged() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_record_acknowledged (hyphaHandle);
}

bool KirinHyphaProcessorBase::presetAvailable() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_preset_available (hyphaHandle);
}

bool KirinHyphaProcessorBase::addAnnotation (const juce::String& memo)
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_add_annotation (hyphaHandle, memo.toRawUTF8());
}

const juce::String KirinHyphaProcessorBase::getName() const
{
    return role == Role::Post ? "Kirin Hypha POST" : "Kirin Hypha PRE";
}

bool KirinHyphaProcessorBase::acceptsMidi() const        { return false; }
bool KirinHyphaProcessorBase::producesMidi() const       { return false; }
bool KirinHyphaProcessorBase::isMidiEffect() const       { return false; }
double KirinHyphaProcessorBase::getTailLengthSeconds() const { return 0.0; }

int KirinHyphaProcessorBase::getNumPrograms()            { return 1; }
int KirinHyphaProcessorBase::getCurrentProgram()         { return 0; }
void KirinHyphaProcessorBase::setCurrentProgram (int)    {}
const juce::String KirinHyphaProcessorBase::getProgramName (int) { return {}; }
void KirinHyphaProcessorBase::changeProgramName (int, const juce::String&) {}

void KirinHyphaProcessorBase::getStateInformation (juce::MemoryBlock& destData)
{
    // B-069/B-072: serialize the 4 identity keys + POST pair target as a JUCE-native XML
    // chunk. The persist members are kept in sync with the FFI at enable time.
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("instance_id",      persistInstanceId);
    xml.setAttribute ("project_uuid",     persistProjectUuid);
    xml.setAttribute ("daw_session_uuid", persistDawSessionUuid);
    xml.setAttribute ("name",             persistName);
    xml.setAttribute ("pair_pre_name",    persistPairName);
    copyXmlToBinary (xml, destData);
}

void KirinHyphaProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    // B-069/B-072: restore the 4 identity keys + pair target into the persist members. May
    // run before or after prepareToPlay (JUCE does not guarantee ordering); the FFI receives
    // these at enable time (handleAsyncUpdate), deferred to the first processBlock.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("KirinHyphaState"))
        {
            persistInstanceId     = xml->getStringAttribute ("instance_id");
            persistProjectUuid    = xml->getStringAttribute ("project_uuid");
            persistDawSessionUuid = xml->getStringAttribute ("daw_session_uuid");
            persistName           = xml->getStringAttribute ("name");
            persistPairName       = xml->getStringAttribute ("pair_pre_name");
        }
    }
}

void KirinHyphaProcessorBase::handleAsyncUpdate()
{
    // B-070: message-thread one-shot. Applies the FFI contract order create -> set_license
    // (done in prepareToPlay) -> set_identity -> enable_*_writes, once both prepareToPlay
    // and any setStateInformation have run (guaranteed by triggering from the first
    // processBlock).
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || writesEnabled.load (std::memory_order_acquire))
        return;

    // set_identity BEFORE enable: empty keys -> the FFI generates fresh UUIDs and writes
    // them back; restored keys -> reused. A set_identity after enable would not propagate
    // (the io_thread snapshots identity at enable), hence this single ordered point.
    kirin_hypha_set_identity (hyphaHandle,
                              persistInstanceId.toRawUTF8(),
                              persistProjectUuid.toRawUTF8(),
                              persistDawSessionUuid.toRawUTF8(),
                              persistName.toRawUTF8());

    // Role selects the plugin_data role: PRE (Watch pre.json + Record) vs POST (post.json
    // Δ via select_target_pre). enable_pre_writes / enable_post_writes are mutually
    // exclusive and idempotent in the FFI.
    if (role == Role::Post)
    {
        kirin_hypha_enable_post_writes (hyphaHandle);
        // B-072: apply the restored/current pair target after enable (contract order).
        kirin_hypha_set_pair_target (hyphaHandle, persistPairName.toRawUTF8());
    }
    else
    {
        kirin_hypha_enable_pre_writes (hyphaHandle);
    }

    // Read back the final (restored or freshly generated) identity so getStateInformation
    // persists it across DAW save/load.
    KirinIdentity id;
    kirin_hypha_get_identity (hyphaHandle, &id);
    persistInstanceId     = juce::String::fromUTF8 (id.instance_id);
    persistProjectUuid    = juce::String::fromUTF8 (id.project_uuid);
    persistDawSessionUuid = juce::String::fromUTF8 (id.daw_session_uuid);
    persistName           = juce::String::fromUTF8 (id.name);

    writesEnabled.store (true, std::memory_order_release);
}
