#include "ObservatoryCaptureContractTest.h"

#include "../src/HyphaCaptureHistoryPainter.h"
#include "../src/HyphaObservatoryView.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Observatory Capture contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_OBSERVATORY_CAPTURE_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (left.getBounds() == right.getBounds());
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}

void writePreview (const juce::File& directory,
                   const juce::String& name,
                   const juce::Image& image)
{
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (directory.createDirectory().wasOk());
    auto output = directory.getChildFile (name).createOutputStream();
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (output != nullptr);
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        juce::PNGImageFormat().writeImageToStream (image, *output));
}

std::vector<KirinMeterHistoryEntry> fullMinutePreviewHistory (
    const std::vector<KirinMeterHistoryEntry>& source)
{
    if (source.empty())
        return {};
    std::vector<KirinMeterHistoryEntry> result (600);
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        auto entry = source[index % source.size()];
        entry.run_id = index < 294u ? 1u : 2u;
        entry.first_observed_frames = index * 4'800u;
        entry.last_observed_frames = entry.first_observed_frames + 4'799u;
        entry.first_timeline_endpoint_samples = static_cast<std::int64_t> (
            entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = static_cast<std::int64_t> (
            entry.last_observed_frames);
        const auto wave = std::sin (static_cast<double> (index) * 0.24);
        entry.lufs_m = { -22.0 + 3.2 * wave, -22.0 + 3.2 * wave,
                         -22.0 + 3.2 * wave };
        entry.lufs_s = { -20.0 + 2.0 * wave, -20.0 + 2.0 * wave,
                         -20.0 + 2.0 * wave };
        const auto peak = index == 178u ? -1.6
                        : index == 422u ? -1.2 : -5.0 + wave;
        entry.true_peak = { peak, peak, peak };
        entry.clip_event_count[0] = index == 132u ? 1u : 0u;
        entry.clip_event_count[1] = index == 422u ? 2u : 0u;
        result[index] = entry;
    }
    return result;
}

void writePreviews (observatory::View& post,
                    observatory::View& pre,
                    const std::vector<KirinMeterHistoryEntry>& history,
                    const KirinObservatoryFrame& activeFrame,
                    const KirinObservatoryFrame& inactiveFrame,
                    const capture::DisplayMetadata& unsafeNames,
                    const capture::PrivacyOptions& allNames)
{
    const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_OBSERVATORY_PREVIEW_DIR", {});
    if (previewDirectory.isEmpty())
        return;
    const juce::File directory (previewDirectory);
    const auto previewHistory = fullMinutePreviewHistory (history);
    post.setTarget (observatory::ObservationTarget::absolute);
    post.setObservatoryFrame (activeFrame, true);
    post.setGuide ("OS GUIDE  MASKING 03:18", "3150-3700 HZ", true);
    post.setHistory (previewHistory);
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        for (const auto domain : {
                 observatory::Domain::level,
                 observatory::Domain::time,
                 observatory::Domain::space })
        {
            post.setDomain (domain);
            const auto suffix = domain == observatory::Domain::level ? "level"
                              : domain == observatory::Domain::time ? "time" : "space";
            writePreview (directory,
                          "post-" + juce::String (suffix) + "-"
                              + juce::String (preset.width) + "x"
                              + juce::String (preset.height) + ".png",
                          render (post));
        }
    }
    post.setSize (300, 200);
    post.setDomain (observatory::Domain::level);
    post.setCompactMaximum (true);
    writePreview (directory, "post-level-max-300x200.png", render (post));
    post.setShortTermLoudness (true);
    writePreview (directory, "post-level-max-shortterm-300x200.png", render (post));
    post.setTarget (observatory::ObservationTarget::delta);
    writePreview (directory, "post-level-delta-300x200.png", render (post));
    post.setSize (600, 400);
    writePreview (directory, "post-level-delta-600x400.png", render (post));
    post.setSize (300, 200);
    post.setTarget (observatory::ObservationTarget::absolute);
    post.setShortTermLoudness (false);
    post.setCompactMaximum (false);
    post.setObservatoryFrame (inactiveFrame, true);
    writePreview (directory, "post-level-inactive-300x200.png", render (post));
    post.setObservatoryFrame (activeFrame, true);

    pre.setSize (600, 400);
    pre.setConnection ("SOURCE PRE", COL_LED_BLUE, observatory::ConnectionState::source);
    pre.setObservatoryFrame (activeFrame, true);
    pre.setGuide ("OS GUIDE  INSPECT 03:18", "SOURCE OBSERVATION", false);
    pre.setDomain (observatory::Domain::level);
    writePreview (directory, "pre-level-600x400.png", render (pre));
    pre.setSize (300, 200);
    writePreview (directory, "pre-level-300x200.png", render (pre));

    post.setSize (600, 400);
    post.setDomain (observatory::Domain::level);
    post.setObservatoryFrame (inactiveFrame, true);
    writePreview (directory, "post-level-inactive-600x400.png", render (post));
    post.setObservatoryFrame (activeFrame, true);
    writePreview (directory, "capture-level-1200x630.png",
                  post.createCaptureImage (
                      1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
                      {}, &previewHistory));
    writePreview (directory, "capture-level-named-1200x630.png",
                  post.createCaptureImage (
                      1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
                      unsafeNames.applying (allNames), &previewHistory));
    writePreview (directory, "capture-level-1080x1080.png",
                  post.createCaptureImage (
                      1'080, 1'080, false, "2026-09-01 00:00:00", "0.1.0",
                      {}, &previewHistory));
    writePreview (directory, "capture-level-1080x1350.png",
                  post.createCaptureImage (
                      1'080, 1'350, false, "2026-09-01 00:00:00", "0.1.0",
                      {}, &previewHistory));
}
}

void verifyObservatoryCaptureContract (
    observatory::View& post,
    observatory::View& pre,
    const std::vector<KirinMeterHistoryEntry>& history,
    const KirinObservatoryFrame& activeFrame,
    const KirinObservatoryFrame& inactiveFrame)
{
    for (const auto dimensions : {
             std::pair { 1'200, 630 }, std::pair { 1'080, 1'080 },
             std::pair { 1'080, 1'350 } })
    {
        const auto capture = post.createCaptureImage (dimensions.first, dimensions.second);
        const auto body = post.captureBodyBounds (dimensions.first, dimensions.second);
        KIRIN_OBSERVATORY_CAPTURE_REQUIRE (capture.getWidth() == dimensions.first);
        KIRIN_OBSERVATORY_CAPTURE_REQUIRE (capture.getHeight() == dimensions.second);
        KIRIN_OBSERVATORY_CAPTURE_REQUIRE (capture.getBounds().contains (body));
    }
    post.setGuide ("MASKING PRIVATE", "3150-3700 HZ", true);
    const auto defaultPrivate = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0");
    post.clearGuide();
    const auto withoutGuide = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0");
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (differentPixels (defaultPrivate, withoutGuide) == 0);
    post.setGuide ("MASKING PRIVATE", "3150-3700 HZ", true);
    const auto explicitGuide = post.createCaptureImage (
        1'200, 630, true, "2026-09-01 00:00:00", "0.1.0");
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        differentPixels (defaultPrivate, explicitGuide) > 500);
    const auto changedMetadata = post.createCaptureImage (
        1'200, 630, false, "2026-09-02 12:34:56", "9.8.7");
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        differentPixels (defaultPrivate, changedMetadata) > 100);

    const capture::DisplayMetadata unsafeNames {
        "  Drum PRE\n01  ", "Track\tPOST", "  Album\r\nProject  "
    };
    const auto normalizedNames = unsafeNames.normalized();
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (normalizedNames.preName == "Drum PRE 01");
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (normalizedNames.postName == "Track POST");
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (normalizedNames.projectName == "Album Project");
    const capture::PrivacyOptions privateNames;
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        unsafeNames.applying (privateNames).footerLine().isEmpty());
    auto preOnly = privateNames;
    preOnly.includePreName = true;
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        unsafeNames.applying (preOnly).footerLine() == "PRE  Drum PRE 01");
    auto allNames = preOnly;
    allNames.includePostName = true;
    allNames.includeProjectName = true;
    const auto namedCapture = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
        unsafeNames.applying (allNames));
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        differentPixels (defaultPrivate, namedCapture) > 100);
    capture::Snapshot frozen;
    frozen.image = namedCapture;
    frozen.capturedAt = "2026-09-01 00:00:00";
    frozen.filenameStamp = "20260901-000000";
    frozen.capturedAtMs = 1'788'220'800'000;
    frozen.pixelWidth = 1'200;
    frozen.pixelHeight = 630;
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (frozen.complete());

    post.setDomain (observatory::Domain::level);
    const std::vector<KirinMeterHistoryEntry> emptyHistory;
    const auto emptySignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &emptyHistory);
    const auto measuredSignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &history);
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        differentPixels (emptySignature, measuredSignature) > 500);
    auto boundedHistory = history;
    auto future = boundedHistory.back();
    future.last_observed_frames = post.captureHistoryEndpoint() + 1u;
    boundedHistory.push_back (future);
    capture_history::retainThrough (boundedHistory, post.captureHistoryEndpoint());
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (boundedHistory.size() == history.size());
    capture_history::retainThrough (boundedHistory, 0u);
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (boundedHistory.empty());
    auto deltaHistory = history;
    for (std::size_t index = 0; index < deltaHistory.size(); ++index)
    {
        const auto value = std::sin (static_cast<double> (index) * 0.115) * 3.0;
        deltaHistory[index].lufs_m = { value - 0.2, value + 0.2, value };
        deltaHistory[index].lufs_s = { value * 0.6 - 0.1,
                                       value * 0.6 + 0.1, value * 0.6 };
    }
    post.setTarget (observatory::ObservationTarget::delta);
    const auto deltaSignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &deltaHistory);
    KIRIN_OBSERVATORY_CAPTURE_REQUIRE (
        differentPixels (measuredSignature, deltaSignature) > 500);
    post.setTarget (observatory::ObservationTarget::absolute);
    writePreviews (post, pre, history, activeFrame, inactiveFrame, unsafeNames, allNames);
}
}
