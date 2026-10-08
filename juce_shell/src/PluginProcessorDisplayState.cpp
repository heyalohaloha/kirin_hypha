#include "PluginProcessor.h"
#include "HyphaObservatoryResizeContract.h"
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
            startTimer (50);
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
