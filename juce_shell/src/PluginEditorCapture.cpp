#include "PluginEditor.h"

namespace
{
const char* captureDomainName (hypha::observatory::Domain domain)
{
    switch (domain)
    {
        case hypha::observatory::Domain::level:     return "LEVEL";
        case hypha::observatory::Domain::time:      return "TIME";
        case hypha::observatory::Domain::frequency: return "FREQ";
        case hypha::observatory::Domain::space:     return "SPACE";
    }
    return "LEVEL";
}
}

void KirinHyphaEditor::beginObservatoryCapture()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Capture format");
    menu.addItem (1, "1200 x 630  Landscape");
    menu.addItem (2, "1080 x 1080  Square");
    menu.addItem (3, "1080 x 1350  Portrait");
    menu.addSeparator();
    menu.addItem (10, "Include OS Guide", true, captureIncludeGuide);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView)
        .withDeletionCheck (*this)
        .withMinimumWidth (220);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == 1) safeThis->chooseObservatoryCapture (1'200, 630);
        else if (result == 2) safeThis->chooseObservatoryCapture (1'080, 1'080);
        else if (result == 3) safeThis->chooseObservatoryCapture (1'080, 1'350);
        else if (result == 10)
        {
            safeThis->captureIncludeGuide = ! safeThis->captureIncludeGuide;
            safeThis->showToast (safeThis->captureIncludeGuide
                ? "OS Guide will be included" : "OS Guide will stay private");
        }
    });
}

void KirinHyphaEditor::chooseObservatoryCapture (int width, int height)
{
    // Freeze the complete visual fact before opening the asynchronous save panel. The meter and
    // any external analysis may keep advancing while the user chooses a filename, but the exported
    // image must remain the exact frame selected by the Capture action.
    const auto capturedAt = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
    auto image = observatoryView.createCaptureImage (
        width, height, captureIncludeGuide, capturedAt, JucePlugin_VersionString);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    juce::Component* external = nullptr;
    if (analysisPage == AnalysisPage::spectrum)
        external = &spectrumView;
    else if (analysisPage == AnalysisPage::perceptual)
        external = &perceptualView;
    else if (analysisPage == AnalysisPage::absolute)
        external = &absoluteView;
    else if (analysisPage == AnalysisPage::attack)
        external = &attackInternalView;
    if (external != nullptr && ! external->getLocalBounds().isEmpty())
    {
        const auto body = observatoryView.captureBodyBounds (width, height, captureIncludeGuide);
        const auto scale = juce::jmax (
            (float) body.getWidth() / (float) external->getWidth(),
            (float) body.getHeight() / (float) external->getHeight());
        const auto analysis = external->createComponentSnapshot (
            external->getLocalBounds(), true, scale);
        juce::Graphics graphics (image);
        graphics.drawImageWithin (analysis, body.getX(), body.getY(),
                                  body.getWidth(), body.getHeight(),
                                  juce::RectanglePlacement::centred, false);
    }
   #endif

    const auto role = isPost ? juce::String ("POST") : juce::String ("PRE");
    const auto target = observatoryView.target()
        == hypha::observatory::ObservationTarget::delta ? juce::String ("DELTA") : role;
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto filename = "Hypha-" + role + "-" + captureDomainName (observatoryDomain)
                        + "-" + target + "-" + stamp + "-"
                        + juce::String (width) + "x" + juce::String (height) + ".png";
    const auto initial = juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                             .getChildFile (filename);
    captureChooser = std::make_unique<juce::FileChooser> (
        "Save Hypha capture", initial, "*.png", true);
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    captureChooser->launchAsync (flags, [safeThis, image] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;
        const auto outputFile = chooser.getResult();
        if (outputFile == juce::File {})
        {
            safeThis->captureChooser.reset();
            return;
        }

        auto stream = outputFile.createOutputStream();
        bool saved = false;
        if (stream != nullptr)
        {
            saved = juce::PNGImageFormat().writeImageToStream (image, *stream);
            stream->flush();
        }
        safeThis->captureChooser.reset();
        if (! saved)
            safeThis->showToast ("Capture could not be saved");
        else
            safeThis->showToast ("Capture saved");
    });
}
