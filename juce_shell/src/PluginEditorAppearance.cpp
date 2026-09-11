#include "PluginEditor.h"

void KirinHyphaEditor::refreshAppearance()
{
    auto& service = hypha::appearance::Service::shared();
    const bool shouldBeRegistered = isShowing();
    if (shouldBeRegistered != appearanceVisibleRegistered)
    {
        appearanceVisibleRegistered = shouldBeRegistered;
        if (appearanceVisibleRegistered)
            service.editorBecameVisible();
        else
            service.editorBecameHidden();
    }
    if (appearanceVisibleRegistered)
        service.pulse();
    appearanceSnapshot = service.snapshot();
}

void KirinHyphaEditor::releaseAppearanceVisibility()
{
    if (! appearanceVisibleRegistered)
        return;
    appearanceVisibleRegistered = false;
    hypha::appearance::Service::shared().editorBecameHidden();
}
