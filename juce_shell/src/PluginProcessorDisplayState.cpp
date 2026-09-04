#include "PluginProcessor.h"
#include "HyphaObservatoryResizeContract.h"

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
    hypha::meter_context::MeterContext value)
{
    const auto encoded = hypha::meter_context::stateValue (value);
    if (preferredMeterContext.exchange (encoded, std::memory_order_acq_rel) != encoded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setScaleModePreference (
    hypha::meter_context::ScaleMode value)
{
    const auto encoded = hypha::meter_context::stateValue (value);
    if (preferredScaleMode.exchange (encoded, std::memory_order_acq_rel) != encoded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
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
