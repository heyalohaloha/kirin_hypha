#include "PluginProcessor.h"
#include "HyphaObservatoryResizeContract.h"
#include "HyphaMetricReferenceZero.h"
#include "kirin_hypha_vu_calibration_ffi.h"

void KirinHyphaProcessorBase::setObservatoryDomainPreference (uint8_t value)
{
    const uint8_t bounded = value < 5u ? value : uint8_t { 0 };
    if (preferredObservatoryDomain.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setObservatoryTargetPreference (uint8_t value)
{
    const uint8_t bounded = value < 2u ? value : uint8_t { 0 };
    if (preferredObservatoryTarget.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setObservatoryTimeRangePreference (uint8_t value)
{
    const uint8_t bounded = value < 5u ? value : uint8_t { 0 };
    if (preferredObservatoryTimeRange.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setMeterContextPreference (
    hypha::meter_context::MeterContext value, bool notifyHost)
{
    const auto encoded = hypha::meter_context::stateValue (value);
    if (! hypha::meter_context::drumAttackAvailable (value)
        && hypha::analysis::isAttack (requestedAnalysisDemand()))
        releaseAnalysisDemand (analysisDemandOwner.currentOwner());
    const bool changed = preferredMeterContext.exchange (
        encoded, std::memory_order_acq_rel) != encoded;
    if (changed)
    {
        // The explicit context chooses the Blind gain policy. Never continue a prepared or active
        // trial under a newly selected context.
        localBlindProductSession.invalidate();
        if (localBlindProductSession.needsService())
            requestServiceTimer();
    }
    if (changed && notifyHost)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setScaleModePreference (
    hypha::meter_context::ScaleMode value)
{
    const auto encoded = hypha::meter_context::stateValue (value);
    if (preferredScaleMode.exchange (encoded, std::memory_order_acq_rel) != encoded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setHybridVuOnRecordPreference (bool enabled)
{
    if (preferredHybridVuOnRecord.exchange (enabled, std::memory_order_acq_rel) != enabled)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

bool KirinHyphaProcessorBase::hybridVuOnRecordPreference() const noexcept
{
    return preferredHybridVuOnRecord.load (std::memory_order_acquire);
}

bool KirinHyphaProcessorBase::manualHybridVuSelection() const noexcept
{
    return manualHybridVuSelected.load (std::memory_order_acquire);
}

void KirinHyphaProcessorBase::setManualHybridVuSelection (bool visible) noexcept
{
    manualHybridVuSelected.store (visible, std::memory_order_release);
}

juce::String KirinHyphaProcessorBase::hybridVuCalibrationScope() const
{
    const juce::ScopedLock lock (handleLock);
    if (! writesEnabled.load (std::memory_order_acquire) || hyphaHandle == nullptr) return {};
    std::array<char, 65> project {}, instance {}; // full legal 64 bytes plus the NUL
    if (! kirin_hypha_get_vu_calibration_locator (hyphaHandle,
            project.data(), project.size(), instance.data(), instance.size())) return {};
    return hypha::vu_calibration::scopeKey (juce::String::fromUTF8 (project.data()),
                                          juce::String::fromUTF8 (instance.data()));
}

int KirinHyphaProcessorBase::refreshHybridVuCalibration()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    return hybridVuCalibration.read (hybridVuCalibrationScope(),
                                    juce::Time::getApproximateMillisecondCounter());
}

bool KirinHyphaProcessorBase::setHybridVuCalibration (int value, const juce::String& expectedScope)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto scope = hybridVuCalibrationScope();
    return scope.isNotEmpty() && scope == expectedScope && hybridVuCalibration.set (
        scope, value, juce::Time::getApproximateMillisecondCounter());
}

void KirinHyphaProcessorBase::setLuChoicePreference (uint8_t packed)
{
    if (preferredLuChoice.exchange (packed, std::memory_order_acq_rel) != packed)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::shareLuZero (const hypha::lu_needle::Facts& facts)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    luSharedZero.publish (hybridVuCalibrationScope(), facts);
}

hypha::lu_needle::Facts KirinHyphaProcessorBase::sharedLuZero()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    return luSharedZero.read (hybridVuCalibrationScope(), juce::Time::getApproximateMillisecondCounter());
}

bool KirinHyphaProcessorBase::setObservatoryEditorSizePreference (int width, int height)
{
    if (! hypha::observatory::validEditorSize (width, height))
        return false;
    const auto packed = hypha::observatory::packEditorSize ({ width, height });
    return preferredEditorSize.exchange (packed, std::memory_order_acq_rel) != packed;
}

void KirinHyphaProcessorBase::notifyObservatoryEditorSizeChanged()
{
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setSpectrumAveragePreference (hypha::spectrum_average::Choice value)
{
    if (spectrumAverage.set (value))
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setDisplaySettingsPreference (hypha::display_settings::Values value)
{
    if (displaySettings.set (value))
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

// B の Balance の重ね比べ（ONE／ALL・描き方・出した線）。保存する表示の選択なので、ほかの表示の選択と並べる。
bool KirinHyphaProcessorBase::setReferenceOverlay (const hypha::reference_audition::BalanceOverlay& overlay)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr && referenceAuditionController->setOverlay (overlay);
   #else
    juce::ignoreUnused (overlay);
    return false;
   #endif
}

// C の画面の CUE／SONG（比べる値）。保存する表示の選択なので、重ね比べと並べる。
bool KirinHyphaProcessorBase::setReferenceCheckSong (bool song)
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr && referenceAuditionController->setCheckSong (song);
   #else
    juce::ignoreUnused (song);
    return false;
   #endif
}

hypha::metric_needle::Face KirinHyphaProcessorBase::metricFacePreference() const noexcept
{
    const auto saved = preferredMetricFace.load (std::memory_order_acquire);
    return hypha::metric_needle::restoredFace (saved == 255 ? std::nullopt : std::optional<int> (saved), luChoicePreference());
}
void KirinHyphaProcessorBase::setMetricFacePreference (hypha::metric_needle::Face face)
{
    const auto saved = static_cast<uint8_t> (hypha::metric_needle::normaliseFace (static_cast<int> (face)));
    if (preferredMetricFace.exchange (saved, std::memory_order_acq_rel) != saved)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setReferenceMetricObservationEnabled (bool enabled)
{
    // Editor close withdraws only its demand. Another PRE window may still need
    // the POST-owned Reference job and its processor-lifetime publication.
    if (metricChainDemand.local() == enabled) return;
    metricChainDemand.local (enabled);
    if (role == Role::Pre && !enabled) metricChainDemand.withdraw();
    requestServiceTimer();
}

void KirinHyphaProcessorBase::serviceReferenceMetrics()
{
    if (!metricChainDemand.serviceDue()) return;
    const auto scope = hybridVuCalibrationScope();
    const auto sampleRate = getSampleRate();
    const auto rate = std::isfinite (sampleRate) && sampleRate >= 8000 && sampleRate <= 384000
        && std::floor (sampleRate) == sampleRate ? static_cast<std::uint32_t> (sampleRate) : 0u;
    const auto channels = static_cast<std::uint32_t> (getTotalNumInputChannels());
    const bool permitted = stereoWorkflowsSupported() && licenseIsOs();
    if (role == Role::Pre)
    { metricChainDemand.request (scope, rate, channels, permitted && metricChainDemand.local()); return; }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    std::shared_ptr<hypha::reference_audition::ReferenceComparisonController> controller;
    { const juce::ScopedLock lock (handleLock); controller = referenceAuditionController; }
    if (!controller) { metricChainDemand.selectionOwnership (scope, false, false); return; }
    const auto selection = controller->snapshot();
    const bool selected = permitted && selection.libraryReceived && hypha::lu_needle::zeroRole (selection) != nullptr;
    const bool wanted = metricChainDemand.local() || metricChainDemand.requested (scope, rate, channels);
    if (!selected || !wanted)
    {
        // Revoke admission/publication first. Worker-held leases keep the lock
        // alive until cancellation completes, even after this processor yields.
        controller->setMetricObservationEnabled (false);
        controller->setMetricOwnershipLease ({});
        metricChainDemand.selectionOwnership (scope, selected, wanted);
        return;
    }
    const auto ownership = metricChainDemand.selectionOwnership (scope, selected, wanted);
    controller->setMetricOwnershipLease (ownership);
    controller->setMetricObservationEnabled (ownership != nullptr);
    if (!ownership) return; // Never overwrite the actual chain owner's publication.
    const auto runtime = controller->snapshot();
    if (scope != hybridVuCalibrationScope()) return;
    const bool readable = permitted && runtime.libraryReceived;
    for (std::size_t i = 0; i < 3; ++i)
        shareMetricZero (scope, hypha::metric_needle::referenceZero (runtime, readable, sampleRate, channels, i));
   #endif
}

void KirinHyphaProcessorBase::shareMetricZero (const juce::String& scope, const hypha::metric_needle::SharedReferenceZero& zero)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    // B/C have independent selection counters. The shared writer owns one
    // monotonic publication sequence so switching roles cannot go backwards.
    if (sharedMetricRevision == std::numeric_limits<std::uint64_t>::max()) return;
    auto sequenced = zero;
    sequenced.generation = ++sharedMetricRevision;
    metricSharedZero.publish (scope, sequenced); // Publish only into the scope covered by the retained owner lease.
}
hypha::metric_needle::SharedReferenceZero KirinHyphaProcessorBase::sharedMetricZero (hypha::metric_needle::Face face, bool readable)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto rate = getSampleRate();
    return metricSharedZero.read (hybridVuCalibrationScope(), face,
        std::isfinite (rate) && rate >= 8000 && rate <= 384000 ? static_cast<std::uint32_t> (rate) : 0u,
        static_cast<std::uint32_t> (getTotalNumInputChannels()), readable && stereoWorkflowsSupported());
}
