#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::refreshCaptureControls()
{
    const bool concealed=localBlindOpen || hypha::reference_ui::isBlindSession(referenceView.state().blindPhase);
    captureStatus.update(referenceView.state().captureAccess,concealed,hypha::presentation::forEditor(getWidth(),getHeight()));
    captureStatus.setBounds(observatoryView.statusStripBounds());
    captureStatus.setVisible(captureStatus.isVisible() && !referenceView.isVisible());
    if(captureStatus.isVisible()) captureStatus.toFront(false);
}
#endif
