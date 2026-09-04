#include "PluginProcessor.h"

bool KirinHyphaProcessorBase::addNote (const juce::String& memo)
{
    refreshLicenseForUserAction();
    const juce::ScopedLock lock (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_add_note (hyphaHandle, memo.toRawUTF8());
}
