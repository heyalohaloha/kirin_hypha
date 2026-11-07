#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HyphaPluginFormat.h"
#include "ChannelRoles.h"
#include <algorithm>
#include <cmath> // B-107: std::abs(float) for the silence peak threshold
namespace
{
    // The ABI sizes are asserted once, in HyphaObservationEquality.h, which HyphaObservatoryFrame.cpp
    // compiles into this same target. A duplicate here went stale at 872/1112 when B-910 grew it.
    // This file does compile on Linux; the target stops earlier on an unrelated GCC ambiguity in
    // AppearanceContract.cpp, so check this object alone with ninja rather than the whole target.
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::unknown)
                       == KIRIN_HYPHA_CLOCK_UNKNOWN);
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::projectTimeline)
                       == KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE);
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::audioRenderTimeline)
                       == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE);
#endif
    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    uint8_t resolveSignalStateCode (bool bypassed,
                                    bool playing,
                                    bool silent,
                                    bool recording,
                                    bool nonRealtime)
    {
        if (bypassed)
            return 2;
        // B-205: silence gates the displayed Active state even during Record. A held Record
        // session with the transport stopped (playing=false, silent=true) reports Inactive
        // instead of showing stale near-zero residue as a bogus ~-400 LUFS/TP. Record capture is
        // deliberately decided by shouldCaptureBufferForMeasurement() below, so silent
        // non-realtime Record buffers can still advance the Record timeline without forcing the
        // display state to Active.
        // B-230: Studio One can render bounce audio with playing=false before the PRE side has
        // observed the POST Record signal. Non-realtime non-silent buffers are real audio and
        // must feed the meter/Record path, otherwise PRE can close with frames=0 while POST has data.
        if (! silent && (recording || playing || nonRealtime))
            return 1;
        return 0;
    }
    bool shouldCaptureBufferForMeasurement (uint8_t stateCode,
                                            bool bypassed,
                                            bool recording,
                                            bool playing,
                                            bool positionChanged,
                                            bool nonRealtime)
    {
        return ! bypassed
            && (stateCode == 1 || (recording && (playing || positionChanged || nonRealtime)));
    }
}
KirinHyphaProcessorBase::KirinHyphaProcessorBase (Role roleIn)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      role (roleIn),
      localBlindProductSession ([this] (std::uint64_t epoch) { return releaseLocalBlindProductScope (epoch); }),
      localBlindCapture (localBlindCaptureSide (roleIn), localBlindCaptureHooks (*this))
{
    // Host bypass keeps audio unchanged while reporting the matching signal state.
    addParameter (bypassParam = new juce::AudioParameterBool ({ "bypass", 1 }, "Bypass", false));
}



void KirinHyphaProcessorBase::releaseResources()
{
    // B-141: releaseResources is an audio lifecycle callback, not user intent. Hosts may call it
    // around offline bounce/freeze; destroying the engine here drops Record state and makes the
    // editor fall back to Watch/Keep mid-bounce. Keep the handle alive until destructor or an
    // incompatible prepareToPlay rebuild.
    // Offline bounce end is not user intent either. Record stop authority stays with explicit
    // Stop/All Stop and the IO-thread idle timeout; this callback intentionally does nothing.
}

void KirinHyphaProcessorBase::updateTrackProperties (const TrackProperties& properties)
{
    // JUCE specifies this callback on the message thread. Keep only the host's human-readable
    // label; colour and all implementation identity remain outside Capture.
    hostTrackDisplayName = properties.name;
    liveCompare.candidate.trackHash.store (properties.name.isNotEmpty()
        ? hypha::live_compare::pairKeyForPreInstance (properties.name.toStdString()) : 0, std::memory_order_release);
}

void KirinHyphaProcessorBase::hostComponentActivationChanged (bool active)
{
    // VST3 IComponent::setActive is distinct from transport/silence. Studio Pro uses this path
    // when the insert power button is changed, while releaseResources alone is too ambiguous
    // (sample-rate reconfigure / offline render / teardown). Rust applies the shared heartbeat
    // grace before publishing Bypassed, so transient host reconfiguration remains Inactive.
    const juce::ScopedLock sl (handleLock);
    hostComponentActive = active;
    liveCompare.candidate.hostActive.store (active, std::memory_order_release);
    if (role == Role::Pre)
        if (const auto* mapping = liveCompare.ring.control(); mapping && mapping->ring())
            mapping->ring()->header.candidate.hostInactive.store (!active, std::memory_order_release);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_host_component_active (hyphaHandle, active);
}

bool KirinHyphaProcessorBase::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    // Exact 5.1 stays in the measurement core; hosts are not offered it until surround acceptance.
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

#include "chain_trace/ChainTraceScope.h"
#include "ProcessorAudioCallback.h"

juce::AudioProcessorEditor* KirinHyphaProcessorBase::createEditor()
{
    refreshLicenseForUserAction();
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


void KirinHyphaProcessorBase::setUseShortTermLoudness (bool shortTerm)
{
    if (persistShortTermLoudness.exchange (shortTerm, std::memory_order_acq_rel) == shortTerm)
        return;
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

// --- B-072: POST pairing surface ---------------------------------------------------------

bool KirinHyphaProcessorBase::isRecording() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_is_recording (hyphaHandle);
}

int KirinHyphaProcessorBase::keepPhase() const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr ? (int) kirin_hypha_keep_phase (hyphaHandle)
                                  : (int) KIRIN_KEEP_PHASE_IDLE;
}

bool KirinHyphaProcessorBase::recordExclusionConflict() const
{
    // B-118 (②): advisory only. keepPair()/keepAll() の reserve→count>MAX が正本。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_record_exclusion_conflict (hyphaHandle);
}

juce::String KirinHyphaProcessorBase::recordErrorMessage() const
{
    // B-118 (③): io_thread 連続失敗の固定文言（G-115-29）。None は空 String（R-26 沈黙）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char buf[128] = { 0 };
    if (kirin_hypha_record_error_message (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}

juce::String KirinHyphaProcessorBase::drainKeepActionNotice()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char buf[128] = { 0 };
    if (kirin_hypha_drain_keep_action_notice (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}

juce::String KirinHyphaProcessorBase::pathAnomalyMessage() const
{
    // B-128 (G-115-373 / D3): 当該 instance（hyphaHandle の instance_id）の anomaly を per-instance で
    // drain（materialize event は自 instance のみ / wall event は global）。文言あり=Some / 無し=空。
    const juce::ScopedLock sl (handleLock);
    char buf[256] = { 0 };
    if (kirin_hypha_drain_path_event (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}
bool KirinHyphaProcessorBase::licenseIsOs() const
{
    return cachedLicenseCode.load (std::memory_order_acquire) == 0;
}

void KirinHyphaProcessorBase::refreshLicenseForUserAction()
{
    const int observed = (int) kirin_hypha_load_license();
    // Missing or malformed identity is Unknown and must revoke OS-only actions immediately.
    // Retaining a previous OS observation here would turn the UI and the FFI gate fail-open.
    cachedLicenseCode.store (observed, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_license (hyphaHandle, (uint8_t) observed);
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



bool KirinHyphaProcessorBase::spectrumStats (KirinSpectrumStats& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_spectrum_stats (hyphaHandle, &out);
}

// --- B-054: PRE live name + LED pollers ---------------------------------------------------

void KirinHyphaProcessorBase::setPreName (const juce::String& name)
{
    // PRE self name == identity.name: persist it (chunk round-trip) AND push it live to the
    // io_thread via the FFI (sanitized to ASCII graphic + space / 16 there). Mirrors how the
    // egui PRE writes its shared name Arc; persistName keeps DAW save/load consistent.
    persistName = name;
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    if (preDisplayController != nullptr)
        preDisplayController->setName (name);
#endif
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

int KirinHyphaProcessorBase::signalStateLive() const
{
    // B-113: editor の表示分岐は Rust の signal_state を直読する。Measure Thread が heartbeat
    // 停止検出で Inactive へ上書きするため、processBlock 停止後に stale な Active を表示しない
    // （旧 B-073 lastSignalState キャッシュを置換）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return 0; // Inactive（安全側 = ---）
    return (int) kirin_hypha_get_signal_state (hyphaHandle);
}

bool KirinHyphaProcessorBase::heartbeatLive() const
{
    // B-115: heartbeat 鮮度（processBlock が呼ばれている事実 / Measure Thread が publish）。
    // editor は `playing かつ live` で POST pair 変更をロックする（playing 凍結値の false-release
    // 防止 / signal_state とは別軸＝無音再生中も state=Inactive のため state を live の代用にしない）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false; // 安全側 = live でない = ロックしない
    return kirin_hypha_heartbeat_live (hyphaHandle);
}

bool KirinHyphaProcessorBase::presetAvailable() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_preset_available (hyphaHandle);
}

bool KirinHyphaProcessorBase::addMark (const juce::String& tag)
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_add_mark (hyphaHandle, tag.toRawUTF8());
}

// --- B-102: POST broadcast + candidate enumeration -----------------------------------------

void KirinHyphaProcessorBase::stopAll()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_stop_all (hyphaHandle);
}

int KirinHyphaProcessorBase::keepReadyCount() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return 0;
    return (int) kirin_hypha_count_keep_ready (hyphaHandle);
}

const juce::String KirinHyphaProcessorBase::getName() const
{
    return role == Role::Post ? "POST Kirin Hypha" : "PRE Kirin Hypha";
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
