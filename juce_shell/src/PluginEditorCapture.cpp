#include "PluginEditor.h"

#include "HyphaCaptureHistoryPainter.h"

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

juce::String captureDomainId (hypha::observatory::Domain domain)
{
    switch (domain)
    {
        case hypha::observatory::Domain::level:     return "level";
        case hypha::observatory::Domain::time:      return "time";
        case hypha::observatory::Domain::frequency: return "frequency";
        case hypha::observatory::Domain::space:     return "space";
    }
    return "level";
}
}

void KirinHyphaEditor::beginObservatoryCapture()
{
    const auto metadata = availableCaptureMetadata().normalized();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&pairMenuLookAndFeel());
    menu.addSectionHeader ("Capture format");
    menu.addItem (1, "1200 x 630  Landscape");
    menu.addItem (2, "1080 x 1080  Square");
    menu.addItem (3, "1080 x 1350  Portrait");
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    const auto workReference = processorRef.connectedWorkReference();
    if (isPost && workReference.valid()
        && workReference.targetRole == hypha::pre_display::GuideTargetRole::post)
    {
        juce::PopupMenu attachmentMenu;
        attachmentMenu.addItem (21, "1200 x 630  Landscape");
        attachmentMenu.addItem (22, "1080 x 1080  Square");
        attachmentMenu.addItem (23, "1080 x 1350  Portrait");
        const auto title = workReference.displayTitle.isNotEmpty()
            ? workReference.displayTitle.substring (0, 36) : juce::String ("Connected Work");
        menu.addSubMenu ("Attach to Work - " + title, attachmentMenu);
    }
    else if (isPost)
        menu.addItem (20, "Attach to Work - connect from Kirin OS", false, false);
#endif
    menu.addSeparator();
    menu.addSectionHeader ("Privacy - private by default");
    menu.addItem (10, "Include OS Guide", true, capturePrivacy.includeGuide);
    menu.addItem (11, "Include PRE name", metadata.preName.isNotEmpty(),
                  capturePrivacy.includePreName);
    menu.addItem (12, "Include POST name", metadata.postName.isNotEmpty(),
                  capturePrivacy.includePostName);
    menu.addItem (13, "Include project name", metadata.projectName.isNotEmpty(),
                  capturePrivacy.includeProjectName);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent (&observatoryView)
        .withDeletionCheck (*this)
        .withMinimumWidth (220);
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    menu.showMenuAsync (options, [safeThis
       #if KIRIN_HYPHA_GUIDE_TRANSPORT
        , workReference
       #endif
    ] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == 1) safeThis->chooseObservatoryCapture (1'200, 630);
        else if (result == 2) safeThis->chooseObservatoryCapture (1'080, 1'080);
        else if (result == 3) safeThis->chooseObservatoryCapture (1'080, 1'350);
#if KIRIN_HYPHA_GUIDE_TRANSPORT
        else if (result == 21) safeThis->attachObservatoryCapture (1'200, 630, workReference);
        else if (result == 22) safeThis->attachObservatoryCapture (1'080, 1'080, workReference);
        else if (result == 23) safeThis->attachObservatoryCapture (1'080, 1'350, workReference);
#endif
        else if (result == 10)
        {
            safeThis->capturePrivacy.includeGuide = ! safeThis->capturePrivacy.includeGuide;
            safeThis->showToast (safeThis->capturePrivacy.includeGuide
                ? "OS Guide will be included" : "OS Guide will stay private");
        }
        else if (result == 11)
        {
            safeThis->capturePrivacy.includePreName =
                ! safeThis->capturePrivacy.includePreName;
            safeThis->showToast (safeThis->capturePrivacy.includePreName
                ? "PRE name will be included" : "PRE name will stay private");
        }
        else if (result == 12)
        {
            safeThis->capturePrivacy.includePostName =
                ! safeThis->capturePrivacy.includePostName;
            safeThis->showToast (safeThis->capturePrivacy.includePostName
                ? "POST name will be included" : "POST name will stay private");
        }
        else if (result == 13)
        {
            safeThis->capturePrivacy.includeProjectName =
                ! safeThis->capturePrivacy.includeProjectName;
            safeThis->showToast (safeThis->capturePrivacy.includeProjectName
                ? "Project name will be included" : "Project name will stay private");
        }
    });
}

hypha::capture::DisplayMetadata KirinHyphaEditor::availableCaptureMetadata() const
{
    hypha::capture::DisplayMetadata metadata;
    metadata.preName = isPost ? processorRef.pairName() : processorRef.preName();
    metadata.postName = isPost ? processorRef.hostTrackName() : juce::String {};
   #if KIRIN_HYPHA_GUIDE_TRANSPORT
    metadata.projectName = processorRef.connectedWorkTitle();
   #endif
    return metadata;
}

hypha::capture::Snapshot KirinHyphaEditor::freezeObservatoryCapture (int width, int height)
{
    // This function is the single Capture read boundary. Every displayed shell fact and external
    // analysis surface is rendered synchronously on the message thread before the asynchronous
    // save panel opens. Later UI/timer updates cannot alter the owned image in this snapshot.
    const auto now = juce::Time::getCurrentTime();
    hypha::capture::Snapshot snapshot;
    snapshot.domain = observatoryView.domain();
    snapshot.target = observatoryView.target();
    snapshot.capturedAt = now.formatted ("%Y-%m-%d %H:%M:%S");
    snapshot.filenameStamp = now.formatted ("%Y%m%d-%H%M%S");
    snapshot.capturedAtMs = now.toMilliseconds();
    snapshot.pixelWidth = width;
    snapshot.pixelHeight = height;
    const auto metadata = availableCaptureMetadata().applying (capturePrivacy);
    std::vector<KirinMeterHistoryEntry> levelHistory;
    const std::vector<KirinMeterHistoryEntry>* historySnapshot = nullptr;
    if (snapshot.domain == hypha::observatory::Domain::level)
    {
        const auto maximumOutput = static_cast<size_t> (juce::jlimit (
            128, 600, juce::roundToInt ((float) width
                                       / hypha::observatory::captureRenderScale)));
        if (snapshot.target == hypha::observatory::ObservationTarget::absolute)
            processorRef.pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, levelHistory,
                                           600, maximumOutput);
        else
            processorRef.pollMeterDeltaHistory (KIRIN_METER_HISTORY_10_HZ, levelHistory,
                                                600, maximumOutput);
        hypha::capture_history::retainThrough (
            levelHistory, observatoryView.captureHistoryEndpoint());
        // Even an empty result is authoritative for this click. Never reuse an earlier TIME page.
        historySnapshot = &levelHistory;
    }
    snapshot.image = observatoryView.createCaptureImage (
        width, height, capturePrivacy.includeGuide, snapshot.capturedAt,
        JucePlugin_VersionString, metadata, historySnapshot);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    juce::Component* external = nullptr;
    if (analysisPage == AnalysisPage::spectrum)
        external = &spectrumView;
    else if (analysisPage == AnalysisPage::perceptual)
        external = &perceptualView;
    else if (analysisPage == AnalysisPage::absolute)
        external = &absoluteView;
    else if (analysisPage == AnalysisPage::attack)
        external = &attackView;
    if (external != nullptr && ! external->getLocalBounds().isEmpty())
    {
        const auto body = observatoryView.captureBodyBounds (
            width, height, capturePrivacy.includeGuide);
        const auto originalBounds = external->getBounds();
        external->setSize (
            juce::roundToInt ((float) body.getWidth()
                              / hypha::observatory::captureRenderScale),
            juce::roundToInt ((float) body.getHeight()
                              / hypha::observatory::captureRenderScale));
        const auto analysis = external->createComponentSnapshot (
            external->getLocalBounds(), true, hypha::observatory::captureRenderScale);
        external->setBounds (originalBounds);
        juce::Graphics graphics (snapshot.image);
        graphics.setColour (hypha::BG);
        graphics.fillRoundedRectangle (body.toFloat(), 4.0f);
        graphics.drawImage (analysis, body.getX(), body.getY(), body.getWidth(), body.getHeight(),
                            0, 0, analysis.getWidth(), analysis.getHeight(), false);
    }
   #endif
    return snapshot;
}

#if KIRIN_HYPHA_GUIDE_TRANSPORT
void KirinHyphaEditor::attachObservatoryCapture (
    int width, int height, const hypha::pre_display::WorkReference& expectedWork)
{
    const auto snapshot = freezeObservatoryCapture (width, height);
    if (! snapshot.complete())
    {
        showToast ("Capture could not be prepared");
        return;
    }
    juce::MemoryBlock pngBytes;
    juce::MemoryOutputStream stream (pngBytes, false);
    if (! juce::PNGImageFormat().writeImageToStream (snapshot.image, stream))
    {
        showToast ("Capture could not be prepared");
        return;
    }
    hypha::capture::WorkAttachmentDescriptor descriptor;
    descriptor.pixelWidth = snapshot.pixelWidth;
    descriptor.pixelHeight = snapshot.pixelHeight;
    descriptor.domain = captureDomainId (snapshot.domain);
    descriptor.observationTarget = snapshot.target
        == hypha::observatory::ObservationTarget::delta ? "delta" : "absolute";
    descriptor.capturedAtMs = snapshot.capturedAtMs;
    const auto submitted = processorRef.attachCaptureToWork (
        expectedWork, std::move (pngBytes), std::move (descriptor));
    if (submitted == hypha::capture::WorkAttachmentSubmit::accepted)
        showToast ("Sending Capture to Work");
    else if (submitted == hypha::capture::WorkAttachmentSubmit::busy)
        showToast ("Another Work attachment is still in progress");
    else if (submitted == hypha::capture::WorkAttachmentSubmit::invalidReference)
        showToast ("Work connection changed; Capture was not attached");
    else
        showToast ("Capture could not be prepared");
}
#endif

void KirinHyphaEditor::chooseObservatoryCapture (int width, int height)
{
    const auto snapshot = freezeObservatoryCapture (width, height);
    if (! snapshot.complete())
    {
        showToast ("Capture could not be prepared");
        return;
    }

    const auto role = isPost ? juce::String ("POST") : juce::String ("PRE");
    const auto target = snapshot.target
        == hypha::observatory::ObservationTarget::delta ? juce::String ("DELTA") : role;
    const auto filename = "Hypha-" + role + "-" + captureDomainName (snapshot.domain)
                        + "-" + target + "-" + snapshot.filenameStamp + "-"
                        + juce::String (width) + "x" + juce::String (height) + ".png";
    const auto initial = juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                             .getChildFile (filename);
    captureChooser = std::make_unique<juce::FileChooser> (
        "Save Hypha capture", initial, "*.png", true);
    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);
    captureChooser->launchAsync (
        flags, [safeThis, image = snapshot.image] (const juce::FileChooser& chooser)
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
