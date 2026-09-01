#include "ObservatoryViewContractTest.h"

#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaCaptureHistoryPainter.h"
#include "../src/HyphaSpectrumComponent.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Observatory view contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_OBSERVATORY_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinMeterSession activeMeter()
{
    KirinMeterSession meter {};
    meter.generation = 7;
    meter.active_frames = 48'000 * 272;
    meter.observed_frames = meter.active_frames;
    meter.sample_rate = 48'000;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -13.8;
    meter.lufs_s = -14.2;
    meter.lufs_i = -14.3;
    meter.lra = 6.4;
    meter.true_peak = -3.6;
    meter.max_true_peak = -1.2;
    meter.plr = 13.1;
    meter.channels = 2;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.sample_peak_dbfs[0] = -5.1;
    meter.sample_peak_dbfs[1] = -5.7;
    meter.sample_peak_hold_dbfs[0] = -2.8;
    meter.sample_peak_hold_dbfs[1] = -3.0;
    meter.channel_true_peak_dbtp[0] = -4.2;
    meter.channel_true_peak_dbtp[1] = -4.8;
    meter.channel_max_true_peak_dbtp[0] = -1.2;
    meter.channel_max_true_peak_dbtp[1] = -1.5;
    meter.clip_events[0] = 2;
    meter.clip_events[1] = 1;
    meter.balance_db = 0.7;
    meter.correlation = 0.82;
    meter.field_size = KIRIN_STEREO_FIELD_SIZE;
    meter.field_observation_count = 30;
    constexpr size_t fieldCentre = KIRIN_STEREO_FIELD_SIZE / 2u;
    for (size_t offset = 2u; offset < KIRIN_STEREO_FIELD_SIZE - 2u; ++offset)
    {
        const int distance = std::abs (static_cast<int> (offset)
                                      - static_cast<int> (fieldCentre));
        meter.field_density[offset * KIRIN_STEREO_FIELD_SIZE + fieldCentre]
            = static_cast<uint8_t> (juce::jmax (42, 255 - distance * 16));
    }
    return meter;
}
KirinWatchDisplay activeWatch()
{
    KirinWatchDisplay watch {};
    watch.current.lufs_m = -13.8;
    watch.current.lufs_s = -14.2;
    watch.current.true_peak = -3.6;
    watch.current.crest = 12.7;
    watch.maximum.lufs_m = -9.4;
    watch.maximum.lufs_s = -10.1;
    watch.maximum.true_peak = -1.2;
    watch.maximum.crest = 16.3;
    return watch;
}
KirinDelta activeDelta()
{
    KirinDelta delta {};
    delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    delta.lufs = 1.1;
    delta.lufs_s = 0.8;
    delta.true_peak = 0.4;
    delta.crest = -1.6;
    return delta;
}
KirinObservatoryFrame activeFrame()
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.lra_state = KIRIN_LRA_READY;
    frame.delta_available = 1u;
    frame.lra_elapsed_seconds = 272.0;
    frame.meter = activeMeter();
    frame.delta = activeDelta();
    return frame;
}
std::vector<KirinMeterHistoryEntry> historyFixture()
{
    std::vector<KirinMeterHistoryEntry> result (90);
    for (size_t index = 0; index < result.size(); ++index)
    {
        auto& entry = result[index];
        entry.generation = 7;
        entry.run_id = index < 44 ? 1 : 2;
        entry.first_observed_frames = index * 4'800;
        entry.last_observed_frames = entry.first_observed_frames + 4'799;
        entry.first_timeline_endpoint_samples = static_cast<int64_t> (entry.first_observed_frames);
        entry.last_timeline_endpoint_samples = static_cast<int64_t> (entry.last_observed_frames);
        entry.observation_count = 1;
        entry.resolution = KIRIN_METER_HISTORY_10_HZ;
        const auto wave = std::sin (static_cast<double> (index) * 0.24);
        entry.lufs_m.min = entry.lufs_m.max = entry.lufs_m.mean = -22.0 + 3.2 * wave;
        entry.lufs_s.min = entry.lufs_s.max = entry.lufs_s.mean = -20.0 + 2.0 * wave;
        entry.true_peak.min = entry.true_peak.max = entry.true_peak.mean = -5.0 + wave;
        entry.correlation.min = entry.correlation.max = entry.correlation.mean = 0.8;
        entry.plr.min = entry.plr.max = entry.plr.mean = 12.0 + wave;
    }
    return result;
}
juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}
int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_OBSERVATORY_REQUIRE (left.getBounds() == right.getBounds());
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
    KIRIN_OBSERVATORY_REQUIRE (directory.createDirectory().wasOk());
    auto output = directory.getChildFile (name).createOutputStream();
    KIRIN_OBSERVATORY_REQUIRE (output != nullptr);
    KIRIN_OBSERVATORY_REQUIRE (
        juce::PNGImageFormat().writeImageToStream (image, *output));
}
void verifyRoleAtEverySize (observatory::Role role,
                            const KirinMeterSession& meter,
                            const std::vector<KirinMeterHistoryEntry>& history)
{
    for (const auto preset : observatory::sizePresets)
    {
        observatory::View view (role);
        view.setSize (preset.width, preset.height);
        view.setConnection (role == observatory::Role::post ? "PAIR DRUM" : "SOURCE PRE",
                            COL_LED_BLUE,
                            role == observatory::Role::post
                                ? observatory::ConnectionState::paired
                                : observatory::ConnectionState::source);
        view.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
        view.setMeterSnapshot (meter, true);
        view.setWatchDisplay (activeWatch(), true);
        view.setHistory (history);
        for (const auto domain : {
                 observatory::Domain::level, observatory::Domain::time,
                 observatory::Domain::frequency, observatory::Domain::space })
        {
            view.setDomain (domain);
            const auto body = view.bodyBounds();
            KIRIN_OBSERVATORY_REQUIRE (! body.isEmpty());
            KIRIN_OBSERVATORY_REQUIRE (view.getLocalBounds().contains (body));
            const auto image = render (view);
            KIRIN_OBSERVATORY_REQUIRE (image.getPixelAt (0, 0).getAlpha() != 0);
            KIRIN_OBSERVATORY_REQUIRE (image.getPixelAt (
                body.getCentreX(), body.getCentreY()).getAlpha() != 0);
            if (domain == observatory::Domain::level)
            {
                auto noClipsMeter = meter;
                noClipsMeter.clip_events[0] = 0;
                noClipsMeter.clip_events[1] = 0;
                view.setMeterSnapshot (noClipsMeter, true);
                const auto clipPixels = differentPixels (image, render (view));
                KIRIN_OBSERVATORY_REQUIRE (
                    observatory::isCompactMeter (preset) ? clipPixels == 0 : clipPixels > 8);
                view.setMeterSnapshot (meter, true);
                auto alternateWatch = activeWatch();
                if (observatory::isCompactMeter (preset))
                    alternateWatch.current.true_peak -= 6.0;
                else
                    alternateWatch.current.crest += 5.0;
                view.setWatchDisplay (alternateWatch, true);
                KIRIN_OBSERVATORY_REQUIRE (differentPixels (image, render (view)) > 8);
                view.setWatchDisplay (activeWatch(), true);
            }
        }
    }
}
}

void writeFrequencyObservatoryPreview (const KirinSpectrumView& snapshot)
{
    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_FREQ_OBSERVATORY_OUTPUT", {});
    if (outputPath.isEmpty())
        return;
    observatory::View shell (observatory::Role::post);
    shell.setSize (600, 400);
    shell.setDomain (observatory::Domain::frequency);
    shell.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
    shell.setGuide ("OS GUIDE  MASKING 03:18", "3150-3700 HZ", true);
    auto meter = activeMeter();
    shell.setMeterSnapshot (meter, true);
    juce::Image composed (juce::Image::ARGB, 600, 400, true);
    juce::Graphics composedGraphics (composed);
    shell.paintEntireComponent (composedGraphics, true);

    SpectrumComponent frequencyBody;
    const auto body = shell.bodyBounds();
    frequencyBody.setSize (body.getWidth(), body.getHeight());
    frequencyBody.setAbsoluteObservation (true);
    frequencyBody.setSnapshot (snapshot);
    guide_frequency::Overlay overlay;
    overlay.count = 1;
    overlay.bands[0].emphasis = guide_frequency::Emphasis::active;
    overlay.bands[0].lowHz = 3'150.0;
    overlay.bands[0].highHz = 3'700.0;
    frequencyBody.setGuideFrequencyOverlay (overlay);
    {
        juce::Graphics::ScopedSaveState saved (composedGraphics);
        composedGraphics.addTransform (juce::AffineTransform::translation (
            static_cast<float> (body.getX()), static_cast<float> (body.getY())));
        frequencyBody.paintEntireComponent (composedGraphics, true);
    }

    auto output = juce::File (outputPath).createOutputStream();
    KIRIN_OBSERVATORY_REQUIRE (output != nullptr);
    KIRIN_OBSERVATORY_REQUIRE (
        juce::PNGImageFormat().writeImageToStream (composed, *output));
}

void verifyObservatoryViewContract()
{
    const auto meter = activeMeter();
    const auto delta = activeDelta();
    const auto watch = activeWatch();
    const auto history = historyFixture();
    observatory_world::Backdrop backdrop;
    KIRIN_OBSERVATORY_REQUIRE (backdrop.isValid());
    juce::Image specimenImage (juce::Image::ARGB, 300, 200, true);
    specimenImage.clear (specimenImage.getBounds(), BG);
    const auto specimenBlank = specimenImage.createCopy();
    {
        juce::Graphics graphics (specimenImage);
        observatory_world::State state;
        state.domain = observatory::Domain::time;
        state.active = true;
        backdrop.drawHyphaSpecimen (graphics, specimenImage.getBounds(), state);
    }
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (specimenBlank, specimenImage) > 2'000);
    juce::Image levelCornersImage (juce::Image::ARGB, 580, 112, true);
    levelCornersImage.clear (levelCornersImage.getBounds(), BG);
    const auto levelCornersBlank = levelCornersImage.createCopy();
    {
        juce::Graphics graphics (levelCornersImage);
        observatory_world::State state;
        state.domain = observatory::Domain::level;
        state.density = observatory::Density::observatory;
        state.active = true;
        backdrop.drawLevelCorners (graphics, levelCornersImage.getBounds(), state);
    }
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (levelCornersBlank, levelCornersImage) > 2'000);
    const auto wideCrop = observatory_world::aspectFillSourceBounds (1536, 1024, 1200, 630);
    KIRIN_OBSERVATORY_REQUIRE (std::abs (wideCrop.getWidth() - 1536.0f) < 0.01f);
    KIRIN_OBSERVATORY_REQUIRE (std::abs (wideCrop.getHeight() - 806.4f) < 0.1f);
    KIRIN_OBSERVATORY_REQUIRE (std::abs (wideCrop.getY() - 108.8f) < 0.1f);
    const auto tallCrop = observatory_world::aspectFillSourceBounds (1536, 1024, 600, 600);
    KIRIN_OBSERVATORY_REQUIRE (std::abs (tallCrop.getWidth() - 1024.0f) < 0.1f);
    KIRIN_OBSERVATORY_REQUIRE (std::abs (tallCrop.getX() - 256.0f) < 0.1f);
    juce::Image fillSource (juce::Image::RGB, 40, 20, true);
    fillSource.clear (fillSource.getBounds(), COL_FLORA);
    juce::Image fillTarget (juce::Image::RGB, 30, 30, true);
    fillTarget.clear (fillTarget.getBounds(), BG);
    {
        juce::Graphics graphics (fillTarget);
        observatory_world::drawAspectFill (graphics, fillSource, fillTarget.getBounds());
    }
    for (int y = 0; y < fillTarget.getHeight(); ++y)
        for (int x = 0; x < fillTarget.getWidth(); ++x)
            KIRIN_OBSERVATORY_REQUIRE (fillTarget.getPixelAt (x, y) == COL_FLORA);
    KIRIN_OBSERVATORY_REQUIRE (meter.field_size == KIRIN_STEREO_FIELD_SIZE);
    KIRIN_OBSERVATORY_REQUIRE (meter.field_observation_count == 30u);
    verifyRoleAtEverySize (observatory::Role::pre, meter, history);
    verifyRoleAtEverySize (observatory::Role::post, meter, history);

    observatory::View pre (observatory::Role::pre);
    pre.setWatchDisplay (watch, true);
    pre.setTarget (observatory::ObservationTarget::delta);
    KIRIN_OBSERVATORY_REQUIRE (pre.target() == observatory::ObservationTarget::absolute);
    pre.setDomain (observatory::Domain::frequency);
    KIRIN_OBSERVATORY_REQUIRE (pre.domain() == observatory::Domain::level);
    KIRIN_OBSERVATORY_REQUIRE (! pre.bodyOwnedByExternalAnalysis());

    observatory::View post (observatory::Role::post);
    post.setSize (600, 400);
    post.setMeterSnapshot (meter, true);
    post.setDeltaSnapshot (delta, true);
    post.setWatchDisplay (watch, true);
    post.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
    post.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
    post.setDomain (observatory::Domain::level);
    post.setObservatoryFrame (activeFrame(), true);
    observatory::View connectionProbe (observatory::Role::post);
    connectionProbe.setSize (300, 200);
    connectionProbe.setObservatoryFrame (activeFrame(), true);
    connectionProbe.setConnection ("PAIR", COL_LED_BLUE,
                                   observatory::ConnectionState::waiting);
    const auto compactWaiting = render (connectionProbe);
    connectionProbe.setConnection ("PAIR", COL_LED_BLUE,
                                   observatory::ConnectionState::paired);
    KIRIN_OBSERVATORY_REQUIRE (connectionProbe.connection()
                               == observatory::ConnectionState::paired);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (compactWaiting, render (connectionProbe)) > 8);
    connectionProbe.setSize (600, 400);
    connectionProbe.setConnection ("PAIR", COL_LED_BLUE,
                                   observatory::ConnectionState::waiting);
    const auto observatoryWaiting = render (connectionProbe);
    connectionProbe.setConnection ("PAIR", COL_LED_BLUE,
                                   observatory::ConnectionState::paired);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (observatoryWaiting, render (connectionProbe)) > 8);
    post.setSize (300, 200);
    post.setTarget (observatory::ObservationTarget::absolute);
    post.setShortTermLoudness (false);
    post.setCompactMaximum (false);
    const auto compactCurrentMomentary = render (post);
    post.setCompactMaximum (true);
    const auto compactMaximumMomentary = render (post);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (compactCurrentMomentary, compactMaximumMomentary) > 100);
    post.setShortTermLoudness (true);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (compactMaximumMomentary, render (post)) > 30);
    post.setCompactMaximum (false);
    post.setTarget (observatory::ObservationTarget::delta);
    const auto compactDelta = render (post);
    auto alternateCrestFrame = activeFrame();
    alternateCrestFrame.delta.crest = 4.8;
    post.setObservatoryFrame (alternateCrestFrame, true);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (compactDelta, render (post)) > 20);
    post.setShortTermLoudness (false);
    post.setTarget (observatory::ObservationTarget::absolute);
    post.setObservatoryFrame (activeFrame(), true);
    post.setSize (600, 400);
    const auto absolute = render (post);
    auto noClipsMeter = meter;
    noClipsMeter.clip_events[0] = 0;
    noClipsMeter.clip_events[1] = 0;
    post.setMeterSnapshot (noClipsMeter, true);
    const auto noClips = render (post);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, noClips) > 20);
    post.setMeterSnapshot (meter, true);
    auto inactiveFrame = activeFrame();
    inactiveFrame.signal_state = KIRIN_SIGNAL_STATE_INACTIVE;
    inactiveFrame.meter.state = KIRIN_METER_SESSION_PAUSED;
    post.setObservatoryFrame (inactiveFrame, true);
    const auto inactive = render (post);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, inactive) > 500);
    auto bypassedFrame = inactiveFrame;
    bypassedFrame.signal_state = KIRIN_SIGNAL_STATE_BYPASSED;
    post.setObservatoryFrame (bypassedFrame, true);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (inactive, render (post)) > 10);
    auto warmingFrame = activeFrame();
    warmingFrame.lra_state = KIRIN_LRA_WARMING;
    warmingFrame.lra_elapsed_seconds = 12.0;
    warmingFrame.meter.lra = 0.0;
    post.setObservatoryFrame (warmingFrame, true);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, render (post)) > 20);
    post.setObservatoryFrame (activeFrame(), true);
    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_OBSERVATORY_RENDER_OUTPUT", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_OBSERVATORY_REQUIRE (output != nullptr);
        KIRIN_OBSERVATORY_REQUIRE (
            juce::PNGImageFormat().writeImageToStream (absolute, *output));
    }
    post.setTarget (observatory::ObservationTarget::delta);
    const auto difference = render (post);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (absolute, difference) > 500);
    auto observatoryCrestFrame = activeFrame();
    observatoryCrestFrame.delta.crest = 4.8;
    post.setObservatoryFrame (observatoryCrestFrame, true);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (difference, render (post)) > 20);
    auto staleDeltaFrame = activeFrame();
    staleDeltaFrame.delta.mode = KIRIN_DELTA_MODE_STALE;
    staleDeltaFrame.delta_available = 1u;
    post.setObservatoryFrame (staleDeltaFrame, true);
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (difference, render (post)) > 100);
    post.setObservatoryFrame (activeFrame(), true);
    KIRIN_OBSERVATORY_REQUIRE (post.domain() == observatory::Domain::level);
    post.setDomain (observatory::Domain::time);
    post.setHistory (history);
    KIRIN_OBSERVATORY_REQUIRE (post.domain() == observatory::Domain::time);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::delta);
    post.setDomain (observatory::Domain::space);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::absolute);
    post.setTarget (observatory::ObservationTarget::delta);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::absolute);
    post.setDomain (observatory::Domain::level);
    KIRIN_OBSERVATORY_REQUIRE (post.target() == observatory::ObservationTarget::delta);
    post.setDomain (observatory::Domain::time);
    const auto request = post.historyRequest();
    KIRIN_OBSERVATORY_REQUIRE (request.resolution == KIRIN_METER_HISTORY_10_HZ);
    KIRIN_OBSERVATORY_REQUIRE (request.maxEntries == 300);
    KIRIN_OBSERVATORY_REQUIRE (request.maxOutputEntries <= 1'200);
    for (const auto dimensions : {
             std::pair { 1'200, 630 }, std::pair { 1'080, 1'080 },
             std::pair { 1'080, 1'350 } })
    {
        const auto capture = post.createCaptureImage (dimensions.first, dimensions.second);
        const auto body = post.captureBodyBounds (dimensions.first, dimensions.second);
        KIRIN_OBSERVATORY_REQUIRE (capture.getWidth() == dimensions.first);
        KIRIN_OBSERVATORY_REQUIRE (capture.getHeight() == dimensions.second);
        KIRIN_OBSERVATORY_REQUIRE (capture.getBounds().contains (body));
    }
    post.setGuide ("MASKING PRIVATE", "3150-3700 HZ", true);
    const auto defaultPrivate = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0");
    post.clearGuide();
    const auto withoutGuide = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0");
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (defaultPrivate, withoutGuide) == 0);
    post.setGuide ("MASKING PRIVATE", "3150-3700 HZ", true);
    const auto explicitGuide = post.createCaptureImage (
        1'200, 630, true, "2026-09-01 00:00:00", "0.1.0");
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (defaultPrivate, explicitGuide) > 500);
    const auto changedMetadata = post.createCaptureImage (
        1'200, 630, false, "2026-09-02 12:34:56", "9.8.7");
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (defaultPrivate, changedMetadata) > 100);
    const capture::DisplayMetadata unsafeNames {
        "  Drum PRE\n01  ", "Track\tPOST", "  Album\r\nProject  "
    };
    const auto normalizedNames = unsafeNames.normalized();
    KIRIN_OBSERVATORY_REQUIRE (normalizedNames.preName == "Drum PRE 01");
    KIRIN_OBSERVATORY_REQUIRE (normalizedNames.postName == "Track POST");
    KIRIN_OBSERVATORY_REQUIRE (normalizedNames.projectName == "Album Project");
    const capture::PrivacyOptions privateNames;
    KIRIN_OBSERVATORY_REQUIRE (
        unsafeNames.applying (privateNames).footerLine().isEmpty());
    auto preOnly = privateNames;
    preOnly.includePreName = true;
    KIRIN_OBSERVATORY_REQUIRE (
        unsafeNames.applying (preOnly).footerLine() == "PRE  Drum PRE 01");
    auto allNames = preOnly;
    allNames.includePostName = true;
    allNames.includeProjectName = true;
    const auto namedCapture = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
        unsafeNames.applying (allNames));
    KIRIN_OBSERVATORY_REQUIRE (differentPixels (defaultPrivate, namedCapture) > 100);
    capture::Snapshot frozen;
    frozen.image = namedCapture;
    frozen.capturedAt = "2026-09-01 00:00:00";
    frozen.filenameStamp = "20260901-000000";
    frozen.pixelWidth = 1'200;
    frozen.pixelHeight = 630;
    KIRIN_OBSERVATORY_REQUIRE (frozen.complete());
    post.setDomain (observatory::Domain::level);
    const std::vector<KirinMeterHistoryEntry> emptyHistory;
    const auto emptySignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &emptyHistory);
    const auto measuredSignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &history);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (emptySignature, measuredSignature) > 500);
    auto boundedHistory = history;
    auto future = boundedHistory.back();
    future.last_observed_frames = post.captureHistoryEndpoint() + 1u;
    boundedHistory.push_back (future);
    capture_history::retainThrough (boundedHistory, post.captureHistoryEndpoint());
    KIRIN_OBSERVATORY_REQUIRE (boundedHistory.size() == history.size());
    capture_history::retainThrough (boundedHistory, 0u);
    KIRIN_OBSERVATORY_REQUIRE (boundedHistory.empty());
    auto deltaHistory = history;
    for (size_t index = 0; index < deltaHistory.size(); ++index)
    {
        const auto value = std::sin ((double) index * 0.115) * 3.0;
        deltaHistory[index].lufs_m = { value - 0.2, value + 0.2, value };
        deltaHistory[index].lufs_s = { value * 0.6 - 0.1,
                                       value * 0.6 + 0.1, value * 0.6 };
    }
    post.setTarget (observatory::ObservationTarget::delta);
    const auto deltaSignature = post.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0", {}, &deltaHistory);
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (measuredSignature, deltaSignature) > 500);
    post.setTarget (observatory::ObservationTarget::absolute);
    const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_OBSERVATORY_PREVIEW_DIR", {});
    if (previewDirectory.isNotEmpty())
    {
        const juce::File directory (previewDirectory);
        post.setTarget (observatory::ObservationTarget::absolute);
        post.setObservatoryFrame (activeFrame(), true);
        post.setGuide ("OS GUIDE  MASKING 03:18", "3150-3700 HZ", true);
        post.setHistory (history);
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
        post.setObservatoryFrame (activeFrame(), true);
        pre.setSize (600, 400);
        pre.setConnection ("SOURCE PRE", COL_LED_BLUE, observatory::ConnectionState::source);
        pre.setObservatoryFrame (activeFrame(), true);
        pre.setGuide ("OS GUIDE  INSPECT 03:18", "SOURCE OBSERVATION", false);
        pre.setDomain (observatory::Domain::level);
        writePreview (directory, "pre-level-600x400.png", render (pre));
        pre.setSize (300, 200);
        writePreview (directory, "pre-level-300x200.png", render (pre));

        post.setSize (600, 400);
        post.setDomain (observatory::Domain::level);
        post.setObservatoryFrame (inactiveFrame, true);
        writePreview (directory, "post-level-inactive-600x400.png", render (post));
        post.setObservatoryFrame (activeFrame(), true);
        writePreview (directory, "capture-level-1200x630.png",
                      post.createCaptureImage (
                          1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
                          {}, &history));
        writePreview (directory, "capture-level-named-1200x630.png",
                      post.createCaptureImage (
                          1'200, 630, false, "2026-09-01 00:00:00", "0.1.0",
                          unsafeNames.applying (allNames), &history));
        writePreview (directory, "capture-level-1080x1080.png",
                      post.createCaptureImage (
                          1'080, 1'080, false, "2026-09-01 00:00:00", "0.1.0",
                          {}, &history));
        writePreview (directory, "capture-level-1080x1350.png",
                      post.createCaptureImage (
                          1'080, 1'350, false, "2026-09-01 00:00:00", "0.1.0",
                          {}, &history));
    }
}
}
