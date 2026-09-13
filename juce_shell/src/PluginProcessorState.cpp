#include "PluginProcessor.h"
#include "HyphaObservatoryResizeContract.h"

void KirinHyphaProcessorBase::getStateInformation (juce::MemoryBlock& destData)
{
    // Hosts may restore PRE and POST in either order; exact ID becomes a Waiting fixed-path latch.
    juce::String livePairProjectHash, livePairInstanceId;
    if (pairedPreLocator (livePairProjectHash, livePairInstanceId))
    {
        persistPairInstanceId = livePairInstanceId;
        persistPairProjectHash = livePairProjectHash;
    }
    else if (writesEnabled.load (std::memory_order_acquire))
    {
        persistPairInstanceId.clear();
        persistPairProjectHash.clear();
    }
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("instance_id",      persistInstanceId);
    xml.setAttribute ("project_uuid",     persistProjectUuid);
    xml.setAttribute ("daw_session_uuid", persistDawSessionUuid);
    xml.setAttribute ("name",             persistName);
    xml.setAttribute ("pair_pre_name",    persistPairName);
    xml.setAttribute ("paired_pre_instance_id", persistPairInstanceId);
    xml.setAttribute ("paired_pre_project_hash", persistPairProjectHash);
    xml.setAttribute ("loudness_view",
                      persistShortTermLoudness.load (std::memory_order_acquire) ? "S" : "M");
    xml.setAttribute ("display_state_version", 5);
    xml.setAttribute ("observatory_domain", (int) observatoryDomainPreference());
    xml.setAttribute ("observatory_target", (int) observatoryTargetPreference());
    xml.setAttribute ("observatory_time_range", (int) observatoryTimeRangePreference());
    xml.setAttribute ("observatory_size", (int) spectrumSizePreference());
    const auto editorSize = hypha::observatory::unpackEditorSize (
        observatoryEditorSizePreference());
    xml.setAttribute ("observatory_width", editorSize.width);
    xml.setAttribute ("observatory_height", editorSize.height);
    xml.setAttribute ("meter_context", (int) hypha::meter_context::stateValue (
        meterContextPreference()));
    xml.setAttribute ("scale_mode", (int) hypha::meter_context::stateValue (
        scaleModePreference()));
    xml.setAttribute ("hybrid_vu_on_record", hybridVuOnRecordPreference());
    copyXmlToBinary (xml, destData);
}
void KirinHyphaProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    // B-069/B-072: restore the 4 identity keys + pair target into the persist members. May
    // run before or after prepareToPlay (JUCE does not guarantee ordering); the FFI receives
    // these at enable time (enableWritesNow), deferred to the message-thread Timer.
    stateInformationSeen.store (true, std::memory_order_release);

    juce::String restoredInstanceId, restoredProjectUuid, restoredDawSessionUuid;
    juce::String restoredName, restoredPairName, restoredPairInstanceId, restoredPairProjectHash;
    bool restoredShortTermLoudness = false;
    uint8_t restoredObservatoryDomain = 0;
    uint8_t restoredObservatoryTarget = 0;
    uint8_t restoredObservatoryTimeRange = 0;
    uint8_t restoredObservatorySize = 0;
    int restoredEditorWidth = 300;
    int restoredEditorHeight = 200;
    auto restoredMeterContext = hypha::meter_context::defaultContext;
    auto restoredScaleMode = hypha::meter_context::defaultScale;
    bool restoredHybridVuOnRecord = true, restored = false;

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("KirinHyphaState"))
        {
            restoredInstanceId     = xml->getStringAttribute ("instance_id");
            restoredProjectUuid    = xml->getStringAttribute ("project_uuid");
            restoredDawSessionUuid = xml->getStringAttribute ("daw_session_uuid");
            restoredName           = xml->getStringAttribute ("name");
            restoredPairName       = xml->getStringAttribute ("pair_pre_name");
            restoredPairInstanceId = xml->getStringAttribute ("paired_pre_instance_id");
            restoredPairProjectHash = xml->getStringAttribute ("paired_pre_project_hash");
            restoredShortTermLoudness = xml->getStringAttribute ("loudness_view") == "S";
            const int displayStateVersion = xml->getIntAttribute ("display_state_version", 0);
            if (displayStateVersion >= 2)
            {
                restoredObservatoryDomain = (uint8_t) juce::jlimit (
                    0, 4, xml->getIntAttribute ("observatory_domain", 0));
                restoredObservatoryTarget = (uint8_t) juce::jlimit (
                    0, 1, xml->getIntAttribute ("observatory_target", 0));
                restoredObservatoryTimeRange = (uint8_t) juce::jlimit (
                    0, 4, xml->getIntAttribute ("observatory_time_range", 0));
                restoredObservatorySize = (uint8_t) juce::jlimit (
                    0, 4, xml->getIntAttribute ("observatory_size", 0));
                const auto preset = hypha::observatory::sizePresets[restoredObservatorySize];
                const auto restoredEditorSize = hypha::observatory::editorSizeFromState (
                    displayStateVersion,
                    restoredObservatorySize,
                    xml->getIntAttribute ("observatory_width", preset.width),
                    xml->getIntAttribute ("observatory_height", preset.height));
                restoredEditorWidth = restoredEditorSize.width;
                restoredEditorHeight = restoredEditorSize.height;
            }
            if (displayStateVersion >= 4)
            {
                restoredMeterContext = hypha::meter_context::contextFromState (
                    (uint8_t) xml->getIntAttribute ("meter_context", 1));
                restoredScaleMode = hypha::meter_context::scaleFromState (
                    (uint8_t) xml->getIntAttribute ("scale_mode", 1));
            }
            if (displayStateVersion >= 5) restoredHybridVuOnRecord =
                xml->getBoolAttribute ("hybrid_vu_on_record", true);
            restored = true;
        }
    }

    // Existing Studio One projects contain the old nih-plug VST3 JSON state. The JUCE shell keeps
    // the same component CID and decodes that exact one-time legacy contract here, so switching the
    // shipped VST3 adapter does not fabricate new identities or lose the selected PRE name.
    if (! restored && data != nullptr && sizeInBytes > 0)
    {
        KirinLegacyNihState legacy {};
        if (kirin_hypha_decode_legacy_nih_state (
                static_cast<const uint8_t*> (data), (size_t) sizeInBytes, &legacy))
        {
            restoredInstanceId     = juce::String::fromUTF8 (legacy.instance_id);
            restoredProjectUuid    = juce::String::fromUTF8 (legacy.project_uuid);
            restoredDawSessionUuid = juce::String::fromUTF8 (legacy.daw_session_uuid);
            restoredName           = juce::String::fromUTF8 (legacy.name);
            restoredPairName       = juce::String::fromUTF8 (legacy.pair_pre_name);
            restored = true;
        }
    }

    if (restored)
    {
        // Additive display state keeps established defaults for older JUCE and nih-plug states.
        persistShortTermLoudness.store (restoredShortTermLoudness, std::memory_order_release);
        preferredObservatoryDomain.store (restoredObservatoryDomain, std::memory_order_release);
        preferredObservatoryTarget.store (restoredObservatoryTarget, std::memory_order_release);
        preferredObservatoryTimeRange.store (restoredObservatoryTimeRange,
                                              std::memory_order_release);
        preferredSpectrumSize.store (restoredObservatorySize, std::memory_order_release);
        preferredEditorSize.store (hypha::observatory::packEditorSize (
            { restoredEditorWidth, restoredEditorHeight }), std::memory_order_release);
        // Restore shares DRUM admission, without writing a host change notification back.
        setMeterContextPreference (restoredMeterContext, false);
        preferredScaleMode.store (
            hypha::meter_context::stateValue (restoredScaleMode), std::memory_order_release);
        preferredHybridVuOnRecord.store (restoredHybridVuOnRecord, std::memory_order_release);
        // Once writes are enabled, the io_thread has already snapshotted path identity. Only the
        // live-editable name/pair fields may be applied at that point; the exact-path writer stays
        // coherent with its established identity.
        if (writesEnabled.load (std::memory_order_acquire))
        {
            persistName = restoredName;
            persistPairName = restoredPairName;
            persistPairInstanceId = restoredPairInstanceId;
            persistPairProjectHash = restoredPairProjectHash;
            const juce::ScopedLock sl (handleLock);
            if (hyphaHandle != nullptr)
            {
                if (role == Role::Post)
                    restorePersistedPairUnderHandleLock();
                else
                    kirin_hypha_set_pre_name (hyphaHandle, persistName.toRawUTF8());
            }
            return;
        }

        persistInstanceId = restoredInstanceId;
        persistProjectUuid = restoredProjectUuid;
        persistDawSessionUuid = restoredDawSessionUuid;
        persistName = restoredName;
        persistPairName = restoredPairName;
        persistPairInstanceId = restoredPairInstanceId;
        persistPairProjectHash = restoredPairProjectHash;
    }

    if (! writesEnabled.load (std::memory_order_acquire))
    {
        enableDelayTicks.store (0, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }
}

