#include "ObservatoryCompositeContractTest.h"

#include "../src/HyphaAbsoluteComponent.h"
#include "../src/HyphaAttackComponent.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaPerceptualComponent.h"
#include "../src/HyphaSpectrumComponent.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Observatory composite contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_COMPOSITE_REQUIRE(expression) require ((expression), #expression, __LINE__)

KirinObservatoryFrame activeFrame()
{
    KirinObservatoryFrame frame {};
    frame.version = KIRIN_OBSERVATORY_FRAME_VERSION;
    frame.signal_state = KIRIN_SIGNAL_STATE_ACTIVE;
    frame.lra_state = KIRIN_LRA_READY;
    frame.delta_available = 1u;
    frame.lra_elapsed_seconds = 245.0;
    auto& meter = frame.meter;
    meter.generation = 11u;
    meter.active_frames = 48'000u * 245u;
    meter.observed_frames = meter.active_frames;
    meter.sample_rate = 48'000u;
    meter.state = KIRIN_METER_SESSION_ACTIVE;
    meter.lufs_m = -13.8;
    meter.lufs_s = -14.2;
    meter.lufs_i = -14.3;
    meter.lra = 6.4;
    meter.true_peak = -3.6;
    meter.max_true_peak = -1.2;
    meter.plr = 13.1;
    meter.channels = 2u;
    meter.balance_state = KIRIN_BALANCE_NUMERIC;
    meter.balance_db = 0.82;
    meter.correlation = 0.76;
    meter.field_size = KIRIN_STEREO_FIELD_SIZE;
    meter.field_observation_count = 30u;
    for (std::size_t index = 0; index < KIRIN_STEREO_FIELD_BINS; ++index)
        meter.field_density[index] = static_cast<std::uint8_t> ((index * 37u) % 192u);
    frame.delta.mode = KIRIN_DELTA_MODE_ACTIVE;
    frame.delta.lufs = 1.1;
    frame.delta.lufs_s = 0.8;
    frame.delta.true_peak = 0.4;
    frame.delta.sharpness = 0.22;
    return frame;
}

juce::Image render (juce::Component& component)
{
    juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics (image);
    component.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right,
                     juce::Rectangle<int> requested = {})
{
    KIRIN_COMPOSITE_REQUIRE (left.getBounds() == right.getBounds());
    const auto area = requested.isEmpty() ? left.getBounds()
                                          : requested.getIntersection (left.getBounds());
    auto count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}

void paintIntoBody (juce::Image& destination, juce::Component& body,
                    juce::Rectangle<int> bounds)
{
    body.setSize (bounds.getWidth(), bounds.getHeight());
    juce::Graphics graphics (destination);
    juce::Graphics::ScopedSaveState saved (graphics);
    graphics.addTransform (juce::AffineTransform::translation (
        static_cast<float> (bounds.getX()), static_cast<float> (bounds.getY())));
    body.paintEntireComponent (graphics, true);
}

juce::Image compose (observatory::View& shell, juce::Component& body)
{
    auto image = render (shell);
    paintIntoBody (image, body, shell.bodyBounds());
    return image;
}

void writeCompositePreview (const juce::String& name, const juce::Image& image)
{
    const auto outputDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    if (outputDirectory.isEmpty())
        return;
    const juce::File directory (outputDirectory);
    KIRIN_COMPOSITE_REQUIRE (directory.createDirectory().wasOk());
    auto output = directory.getChildFile (name).createOutputStream();
    KIRIN_COMPOSITE_REQUIRE (output != nullptr);
    KIRIN_COMPOSITE_REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *output));
}

KirinSpectrumView spectrumFixture()
{
    KirinSpectrumView view {};
    view.status = KIRIN_SPECTRUM_ACTIVE;
    view.has_data = 1u;
    view.post_has_data = 1u;
    view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
    view.channels = 2u;
    view.sample_rate = 48'000u;
    view.aperture_samples = 4'096u;
    view.fft_size = 8'192u;
    view.min_hz = 10.0f;
    view.max_hz = 22'000.0f;
    view.presentation_end_samples = 288'000;
    for (std::size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const auto phase = static_cast<float> (index) * 0.071f;
        view.pre_dbfs[index] = -48.0f + std::sin (phase) * 12.0f;
        view.display_db[index] = std::sin (phase * 1.7f) * 5.0f;
        view.post_dbfs[index] = view.pre_dbfs[index] + view.display_db[index];
    }
    return view;
}

std::unique_ptr<AttackComponent> attackFixture()
{
    auto component = std::make_unique<AttackComponent>();
    KirinAttackEventBatch events {};
    events.capacity = KIRIN_ATTACK_EVENT_BATCH_CAPACITY;
    events.count = 1u;
    events.events[0].generation = 11u;
    events.events[0].sample_rate = 48'000u;
    events.events[0].event_sample = 288'000;
    auto waveform = std::make_unique<KirinAttackWaveformBatch>();
    waveform->capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    waveform->count = 120u;
    for (std::uint32_t index = 0; index < waveform->count; ++index)
    {
        auto& point = waveform->points[index];
        point.generation = 11u;
        point.sample_rate = 48'000u;
        point.channels = 2u;
        point.start_sample = 230'400 + static_cast<std::int64_t> (index) * 480;
        point.end_sample = point.start_sample + 480;
        const auto distance = std::abs (point.start_sample - 288'000);
        point.rms_dbfs = -54.0f + 44.0f * std::exp (-static_cast<float> (distance) / 8'500.0f);
    }
    auto details = std::make_unique<KirinAttackDetailBatch>();
    details->capacity = KIRIN_ATTACK_DETAIL_BATCH_CAPACITY;
    details->count = 1u;
    auto& detail = details->details[0];
    detail.generation = 11u;
    detail.sample_rate = 48'000u;
    detail.channels = 2u;
    detail.event_sample = 288'000;
    detail.shape_start_sample = 283'200;
    detail.shape_end_sample = 289'440;
    detail.shape_count = KIRIN_ATTACK_SHAPE_CAPACITY;
    detail.contrast_db = 8.0f;
    detail.attack_rms_dbfs = -14.0f;
    detail.sample_peak_dbfs = -3.0f;
    detail.crest_db = 6.0f;
    detail.sample_edge_ratio_db = -12.0f;
    detail.peak_plateau_ms = 1.5f;
    detail.sharpness_available = 1u;
    detail.sharpness_acum = 1.6f;
    for (std::uint32_t index = 0; index < detail.shape_count; ++index)
        detail.shape[index] = index < 70u ? 0.03f
            : 0.82f * std::exp (-static_cast<float> (index - 70u) / 8.0f) + 0.02f;
    auto preWaveform = std::make_unique<KirinAttackWaveformBatch> (*waveform);
    auto preDetails = std::make_unique<KirinAttackDetailBatch> (*details);
    for (std::uint32_t index = 0; index < preWaveform->count; ++index)
        preWaveform->points[index].rms_dbfs -= 2.0f;
    preDetails->details[0].contrast_db = 5.0f;
    preDetails->details[0].attack_rms_dbfs = -20.0f;
    preDetails->details[0].sharpness_acum = 1.2f;
    KirinAttackPairEventBatch pairs {};
    pairs.status = KIRIN_SPECTRUM_ACTIVE;
    pairs.capacity = KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY;
    pairs.count = 1u;
    pairs.events[0].event_sample = 288'000;
    pairs.events[0].pre_event_sample = 288'000;
    pairs.events[0].post_event_sample = 288'000;
    pairs.events[0].pre_available = 1u;
    pairs.events[0].post_available = 1u;
    KirinAttackStats stats {};
    stats.available = 1u;
    stats.enabled = 1u;
    stats.worker_running = 1u;
    component->setSnapshot (events, *waveform, *details, *preWaveform, *preDetails, pairs,
                            288'000, 48'000u, 11u, stats);
    component->setOverlayMode (false);
    return component;
}

void requireExternalComposite (observatory::View& shell, juce::Component& body)
{
    const auto parent = render (shell);
    const auto combined = compose (shell, body);
    const auto bodyBounds = shell.bodyBounds();
    const auto changed = differentPixels (parent, combined, bodyBounds);
    const auto minimumInk = juce::jmax (128, bodyBounds.getWidth()
                                            * bodyBounds.getHeight() / 500);
    std::cout << "Observatory external body " << bodyBounds.getWidth() << 'x'
              << bodyBounds.getHeight() << ": " << changed << " changed pixels\n";
    KIRIN_COMPOSITE_REQUIRE (changed > minimumInk);
}
}

void verifyObservatoryCompositeContract()
{
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[0]).family
        == observatory::ExperienceFamily::compactMeter);
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[1]).family
        == observatory::ExperienceFamily::compactMeter);
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[1]).maximumNumericFacts == 3u);
    KIRIN_COMPOSITE_REQUIRE (observatory::maximumConcurrentObservatorySlots == 2u);
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[2]).family
        == observatory::ExperienceFamily::observatory);
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[3]).family
        == observatory::ExperienceFamily::observatory);
    KIRIN_COMPOSITE_REQUIRE (
        observatory::presentationContract (observatory::sizePresets[4]).family
        == observatory::ExperienceFamily::observatory);

    observatory::View shell (observatory::Role::post);
    shell.setSize (600, 400);
    shell.setObservatoryFrame (activeFrame(), true);
    shell.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
    shell.setGuide ("OS GUIDE  MASKING 03:18", "3150-3700 HZ", true);

    SpectrumComponent spectrum;
    spectrum.setSnapshot (spectrumFixture());
    shell.setDomain (observatory::Domain::frequency);
    requireExternalComposite (shell, spectrum);

    PerceptualComponent sharpness;
    KirinPerceptualView sharpnessView {};
    sharpnessView.status = KIRIN_SPECTRUM_ACTIVE;
    sharpnessView.has_data = 1u;
    sharpnessView.channels = 2u;
    sharpnessView.sample_rate = 48'000u;
    sharpnessView.aperture_samples = 4'800u;
    sharpnessView.pre_sharpness = 1.2;
    sharpnessView.post_sharpness = 1.6;
    sharpnessView.delta_sharpness = 0.4;
    sharpnessView.presentation_end_samples = 288'000;
    sharpness.setSnapshot (sharpnessView);
    sharpness.presentationTickAt (1'000.0);
    shell.setDomain (observatory::Domain::time);
    requireExternalComposite (shell, sharpness);

    AbsoluteComponent live;
    KirinAbsoluteBatch liveBatch {};
    liveBatch.count = 1u;
    liveBatch.frames[0].status = KIRIN_SPECTRUM_ACTIVE;
    liveBatch.frames[0].has_data = 1u;
    liveBatch.frames[0].channels = 2u;
    liveBatch.frames[0].sample_rate = 48'000u;
    liveBatch.frames[0].aperture_samples = 4'800u;
    liveBatch.frames[0].lufs_m = -13.8;
    liveBatch.frames[0].true_peak = -3.6;
    liveBatch.frames[0].sharpness = 1.6;
    liveBatch.frames[0].presentation_end_samples = 288'000;
    liveBatch.latest = liveBatch.frames[0];
    live.setBatchAt (liveBatch, 1'000.0);
    requireExternalComposite (shell, live);

    auto attack = attackFixture();
    requireExternalComposite (shell, *attack);
    writeCompositePreview ("post-attack-600x400.png", compose (shell, *attack));

    shell.setSize (900, 600);
    KIRIN_COMPOSITE_REQUIRE (
        shell.experienceFamily() == observatory::ExperienceFamily::observatory);
    shell.setDomain (observatory::Domain::frequency);
    requireExternalComposite (shell, spectrum);
    writeCompositePreview ("post-freq-900x600.png", compose (shell, spectrum));
    shell.setDomain (observatory::Domain::time);
    requireExternalComposite (shell, sharpness);
    writeCompositePreview ("post-sharp-900x600.png", compose (shell, sharpness));
    requireExternalComposite (shell, live);
    writeCompositePreview ("post-live-900x600.png", compose (shell, live));
    requireExternalComposite (shell, *attack);
    writeCompositePreview ("post-attack-900x600.png", compose (shell, *attack));

    shell.setSize (300, 200);
    KIRIN_COMPOSITE_REQUIRE (
        shell.experienceFamily() == observatory::ExperienceFamily::compactMeter);
    requireExternalComposite (shell, *attack);
    writeCompositePreview ("post-attack-300x200.png", compose (shell, *attack));
    shell.setSize (600, 400);
    KIRIN_COMPOSITE_REQUIRE (
        shell.experienceFamily() == observatory::ExperienceFamily::observatory);

    const auto captureBase = shell.createCaptureImage (
        1'200, 630, false, "2026-09-01 00:00:00", "0.1.0");
    auto captureComposite = captureBase.createCopy();
    const auto captureBody = shell.captureBodyBounds (1'200, 630, false);
    attack->setSize (
        juce::roundToInt ((float) captureBody.getWidth()
                          / observatory::captureRenderScale),
        juce::roundToInt ((float) captureBody.getHeight()
                          / observatory::captureRenderScale));
    const auto attackSnapshot = attack->createComponentSnapshot (
        attack->getLocalBounds(), true, observatory::captureRenderScale);
    {
        juce::Graphics graphics (captureComposite);
        graphics.setColour (BG);
        graphics.fillRoundedRectangle (captureBody.toFloat(), 4.0f);
        graphics.drawImage (attackSnapshot, captureBody.getX(), captureBody.getY(),
                            captureBody.getWidth(), captureBody.getHeight(),
                            0, 0, attackSnapshot.getWidth(), attackSnapshot.getHeight(), false);
    }
    const auto captureInk = differentPixels (captureBase, captureComposite, captureBody);
    std::cout << "Observatory capture body " << captureBody.getWidth() << 'x'
              << captureBody.getHeight() << ": " << captureInk << " changed pixels\n";
    KIRIN_COMPOSITE_REQUIRE (
        captureInk > captureBody.getWidth() * captureBody.getHeight() / 500);
    writeCompositePreview ("capture-attack-1200x630.png", captureComposite);
}
}
