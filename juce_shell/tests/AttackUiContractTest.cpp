#include "../src/HyphaAttackComponent.h"
#include "../src/HyphaAttackPainter.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaTheme.h"
#include "AttackUiImageHelpers.h"
#include "AttackUiLaneContract.h"
#include "AttackUiChromeContract.h"
#include "AttackUiSelectionContract.h"
#include "AttackUiOverviewContract.h"
#include "AttackUiSizeContract.h"
#include "AttackUiLifecycleContract.h"
#include "AttackUiRuntimeContract.h"
#include "AttackUiFrameBudget.h"
#include "PolylineGeometryContractTest.h"
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
namespace
{
    using namespace hypha::attack_ui_test;

    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "ATTACK UI contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }
#define KIRIN_REQUIRE(expression) require ((expression), #expression, __LINE__)

    const auto context = hypha::presentation::forEditor (600, 400);
    constexpr int width = 580;
    constexpr int height = 248;
    const auto layout = hypha::attack_ui::layoutFor (width, height, context);

    int eventX (std::int64_t sample, std::int64_t latest)
    {
        const auto plot = historyRect (layout);
        return plot.getX() + hypha::attack_ui::eventX (sample, latest, 48'000, plot.getWidth());
    }

    // The selection hypha is drawn at 90% opacity through the HISTORY plot.
    bool selectionNear (const juce::Image& image, int x)
    {
        const auto plot = historyRect (layout);
        return countColour (image, plot.withX (x - 3).withWidth (7),
                            juce::Colour (hypha::attack_ui::selectionColour), 40) > 0;
    }

    juce::MouseEvent mouseEvent (juce::Component& component, float x, float y)
    {
        const auto eventTime = juce::Time::getCurrentTime();
        return {
            juce::Desktop::getInstance().getMainMouseSource(),
            { x, y }, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &component, &component, eventTime, { x, y }, eventTime, 0, false
        };
    }
}
int main()
{
    static_assert (sizeof (KirinAttackWaveformPoint) == 40);
    static_assert (sizeof (KirinAttackWaveformBatch) == 24'008);
    static_assert (sizeof (KirinAttackDetail) == 512);
    static_assert (offsetof (KirinAttackDetail, shape) == 128);
    static_assert (sizeof (KirinAttackDetailBatch) == 122'888);
    static_assert (sizeof (KirinAttackPairEvent) == 112);
    static_assert (sizeof (KirinAttackPairEventBatch) == 26'896);
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    auto componentStorage = std::make_unique<hypha::AttackComponent>();
    auto& component = *componentStorage;
    component.setPresentationContext (context);
    component.setSize (width, height);
    KirinAttackStats stats {};
    stats.available = 1;
    stats.enabled = 1;
    stats.worker_running = 1;
    KirinAttackEventBatch events {};
    events.capacity = KIRIN_ATTACK_EVENT_BATCH_CAPACITY;
    events.count = 4;
    for (std::uint32_t index = 0; index < events.count; ++index)
    {
        events.events[index].generation = 7;
        events.events[index].sample_rate = 48'000;
    }
    events.events[0].event_sample = 0;
    events.events[1].event_sample = 144'000;
    events.events[2].event_sample = 288'000;
    events.events[3].event_sample = 200'000;
    events.events[3].generation = 6; // stale transport generation must not be painted

    auto waveformStorage = std::make_unique<KirinAttackWaveformBatch>();
    auto& waveform = *waveformStorage;
    waveform.capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    waveform.count = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
    for (std::uint32_t index = 0; index < waveform.count; ++index)
    {
        auto& point = waveform.points[index];
        point.generation = 7;
        point.sample_rate = 48'000;
        point.channels = 2;
        point.start_sample = static_cast<std::int64_t> (index) * 480;
        point.end_sample = point.start_sample + 480;
        const auto sample = point.start_sample + 240;
        const auto nearestEvent = sample < 72'000 ? 0 : sample < 216'000 ? 144'000 : 288'000;
        const auto relative = sample - nearestEvent;
        auto pulse = 0.0f;
        if (relative >= 0)
        {
            const auto elapsed = static_cast<float> (relative);
            pulse = 0.70f * std::exp (-elapsed / 8'500.0f)
                  + 0.20f * std::exp (-elapsed / 2'600.0f) * std::abs (std::sin (elapsed / 720.0f))
                  + 0.10f * std::exp (-elapsed / 25'000.0f);
        }
        else if (relative >= -1'440)
            pulse = 0.08f * std::exp (static_cast<float> (relative) / 520.0f);
        point.rms_dbfs = -58.0f + pulse * 48.0f;
    }

    auto detailsStorage = std::make_unique<KirinAttackDetailBatch>();
    auto& details = *detailsStorage;
    details.capacity = KIRIN_ATTACK_DETAIL_BATCH_CAPACITY;
    details.count = 3;
    for (const auto [index, sample] : { std::pair<std::uint32_t, std::int64_t> { 0, 288'000 },
                                        { 1, 144'000 }, { 2, 0 } })
    {
        details.details[index] = laneDetail (sample);
        details.details[index].sharpness_acum = 1.6f;
    }
    auto preWaveformStorage = std::make_unique<KirinAttackWaveformBatch> (waveform);
    auto& preWaveform = *preWaveformStorage;
    for (std::uint32_t index = 0; index < preWaveform.count; ++index)
        preWaveform.points[index].rms_dbfs -= 2.0f;
    auto preDetailsStorage = std::make_unique<KirinAttackDetailBatch> (details);
    auto& preDetails = *preDetailsStorage;
    for (std::uint32_t index = 0; index < preDetails.count; ++index)
    {
        preDetails.details[index].transient_db = 5.0f;
        preDetails.details[index].attack_rms_dbfs = -20.0f;
        preDetails.details[index].crest_db = 10.0f;
        preDetails.details[index].sharpness_acum = 1.2f;
    }
    KirinAttackPairEventBatch pairEvents {};
    pairEvents.status = KIRIN_SPECTRUM_ACTIVE;
    pairEvents.capacity = KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY;
    pairEvents.count = 3;
    for (std::uint32_t index = 0; index < pairEvents.count; ++index)
    {
        auto& pair = pairEvents.events[index];
        pair.event_sample = pair.pre_event_sample = pair.post_event_sample
            = events.events[index].event_sample;
        pair.sample_rate = 48'000;
        pair.pre_generation = pair.post_generation = 7;
        pair.pre_available = pair.post_available = 1;
    }

    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    component.setOverlayMode (false);
    // Timing runs before the image-heavy contracts: their caches and heap growth measurably slow
    // later frames in the same process (about 0.5 ms at 300% / DPI 2).
    KIRIN_REQUIRE (verifyAttackFrameBudget());
    KIRIN_REQUIRE (verifyLaneModel());
    KIRIN_REQUIRE (verifyDetailLifecycle (events, waveform, details, pairEvents, stats));
    KIRIN_REQUIRE (verifyMeasuredEnvelope());
    KIRIN_REQUIRE (verifyEnvelopeSimplificationBound());
    KIRIN_REQUIRE (verifyEnvelopeRaster());
    KIRIN_REQUIRE (verifyContinuousTrace (waveform));
    KIRIN_REQUIRE (verifyLaneRendering());
    KIRIN_REQUIRE (verifyHistoryIsolation());
    KIRIN_REQUIRE (verifyPostOnlyLanes());
    KIRIN_REQUIRE (verifyLanesShowDifferencesOnly());
    KIRIN_REQUIRE (verifyLoupe());
    KIRIN_REQUIRE (verifyCompactLine());
    KIRIN_REQUIRE (verifySelectionHypha());
    KIRIN_REQUIRE (verifyOffscreenLock());
    KIRIN_REQUIRE (verifyHoldAtEverySize());
    KIRIN_REQUIRE (verifyChromeCache());
    hypha::tests::verifyPolylineGeometryContract();
    KIRIN_REQUIRE (verifyRedrawContract (events, waveform, details, pairEvents, stats));
    const auto image = renderAttack (component);
    KIRIN_REQUIRE (writePreviewTo ("KIRIN_ATTACK_UI_PREVIEW_PATH", image));
    KIRIN_REQUIRE (selectionNear (image, eventX (288'000, 288'000)));

    // With identical PRE and POST, the two HISTORY rows are the same measured envelope. The
    // envelope ink is isolated from the row's vertical material gradient by subtracting a frame
    // painted without envelopes.
    auto identityComponentStorage = std::make_unique<hypha::AttackComponent>();
    auto& identityComponent = *identityComponentStorage;
    identityComponent.setPresentationContext (context);
    identityComponent.setSize (width, height);
    identityComponent.setOverlayMode (false);
    auto emptyWaveform = std::make_unique<KirinAttackWaveformBatch>();
    identityComponent.setSnapshot (events, *emptyWaveform, details, *emptyWaveform, details,
                                   pairEvents, 288'000, 48'000, 7, stats);
    const auto withoutEnvelope = renderAttack (identityComponent);
    identityComponent.setSnapshot (events, waveform, details, waveform, details, pairEvents,
                                   288'000, 48'000, 7, stats);
    const auto identity = renderAttack (identityComponent);
    const auto history = historyRect (layout);
    const auto rowHeight = history.getHeight() / 2;
    const juce::Rectangle<int> preRow { history.getX() + 40, history.getY() + 2,
                                        history.getWidth() - 48, rowHeight - 4 };
    const auto ink = [&] (int x, int y) {
        return identity.getPixelAt (x, y) != withoutEnvelope.getPixelAt (x, y); };
    int identityInk = 0, identityDifferences = 0;
    for (int y = preRow.getY(); y < preRow.getBottom(); ++y)
        for (int x = preRow.getX(); x < preRow.getRight(); ++x)
        {
            identityInk += ink (x, y);
            identityDifferences += ink (x, y) != ink (x, y + rowHeight);
        }
    if (identityDifferences > 100)
        std::cerr << "identity rows: " << identityDifferences << " of " << identityInk << '\n';
    KIRIN_REQUIRE (writePreviewTo ("KIRIN_ATTACK_UI_IDENTITY_PREVIEW_PATH", identity));
    KIRIN_REQUIRE (identityInk > 200);
    KIRIN_REQUIRE (identityDifferences <= 100); // Mirrored curve antialiasing differs at subpixels.
    identityComponent.setOverlayMode (true);
    const auto identityOverlay = renderAttack (identityComponent);

    const auto firstEventX = eventX (0, 288'000);
    const auto middleEventX = eventX (144'000, 288'000);
    const auto lastEventX = eventX (288'000, 288'000);
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), middleEventX));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), firstEventX));
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), middleEventX));

    // A locked selection moves with time at the presentation rate and never jumps to new hits.
    auto laterPairEvents = pairEvents;
    laterPairEvents.count = 4;
    laterPairEvents.events[3] = pairEvents.events[2];
    laterPairEvents.events[3].event_sample = 320'000;
    laterPairEvents.events[3].pre_event_sample = 320'000;
    laterPairEvents.events[3].post_event_sample = 320'000;
    const auto transitionStart = juce::Time::getMillisecondCounterHiRes();
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, laterPairEvents,
                           336'000, 48'000, 7, stats);
    KIRIN_REQUIRE (selectionNear (renderAttack (component), middleEventX));
    component.presentationTickAt (transitionStart + 25.0);
    KIRIN_REQUIRE (selectionNear (renderAttack (component), eventX (144'000, 300'000)));
    component.presentationTickAt (transitionStart + 50.0);
    KIRIN_REQUIRE (selectionNear (renderAttack (component), eventX (144'000, 312'000)));
    component.presentationTickAt (transitionStart + 200.0);
    const auto newLastX = eventX (320'000, 336'000);
    KIRIN_REQUIRE (selectionNear (renderAttack (component), eventX (144'000, 336'000)));
    KIRIN_REQUIRE (! selectionNear (renderAttack (component), newLastX));
    // END returns to the newest hit with delivered POST detail, not an incomplete event.
    KIRIN_REQUIRE (component.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), eventX (288'000, 336'000)));
    KIRIN_REQUIRE (! selectionNear (renderAttack (component), newLastX));

    auto newestPairEvents = laterPairEvents;
    newestPairEvents.count = 5;
    newestPairEvents.events[4] = laterPairEvents.events[3];
    newestPairEvents.events[4].event_sample = 370'000;
    newestPairEvents.events[4].pre_event_sample = 370'000;
    newestPairEvents.events[4].post_event_sample = 370'000;
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, newestPairEvents,
                           384'000, 48'000, 7, stats);
    // Finish the pending time movement first: HOLD freezes time, it does not move the lanes.
    component.presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
    const auto beforeHold = renderAttack (component);
    component.presentationTick (false); // Silence/stop holds the last complete hit.
    const auto held = renderAttack (component);
    KIRIN_REQUIRE (differences (beforeHold, held, lanesArea (layout)) == 0
                   && differences (beforeHold, held) > 0);
    component.presentationTick (true);
    KIRIN_REQUIRE (! component.keyPressed (juce::KeyPress ('x')));

    // Pointer selection works in HISTORY and in every lane, which share one time column.
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto historyY = static_cast<float> (history.getCentreY());
    component.mouseDown (mouseEvent (component, static_cast<float> (firstEventX), historyY));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), firstEventX));
    component.mouseDrag (mouseEvent (component, static_cast<float> (middleEventX), historyY));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), middleEventX));
    KIRIN_REQUIRE (writePreviewTo ("KIRIN_ATTACK_UI_LOCK_PREVIEW_PATH", renderAttack (component)));
    const auto laneY = static_cast<float> (layout.lanes[2].y + layout.lanes[2].height / 2);
    component.mouseDown (mouseEvent (component, static_cast<float> (firstEventX), laneY));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), firstEventX));
    const auto axis = columnRect (layout, layout.axis);
    component.mouseDown (mouseEvent (component, static_cast<float> (axis.getRight() - 4),
                                     static_cast<float> (axis.getCentreY())));
    KIRIN_REQUIRE (selectionNear (renderAttack (component), lastEventX));

    const auto twoRows = renderAttack (component);
    component.setOverlayMode (true);
    const auto overlay = renderAttack (component);
    KIRIN_REQUIRE (differences (twoRows, overlay) > 100);
    KIRIN_REQUIRE (differences (identityOverlay, overlay, history) > 0);
    KIRIN_REQUIRE (writePreviewTo ("KIRIN_ATTACK_UI_OVERLAY_PREVIEW_PATH", overlay));
    KIRIN_REQUIRE (verifySupportedSizes (component));
    component.setPresentationContext (context);
    component.setSize (width, height);
    stats.worker_running = 0;
    component.setSnapshot (events, waveform, details, preWaveform, preDetails, pairEvents,
                           288'000, 48'000, 7, stats);
    const auto warming = renderAttack (component);
    KIRIN_REQUIRE (! selectionNear (warming, lastEventX));
    KIRIN_REQUIRE (verifyDormantQuiet (warming, layout));
    std::cout << "ATTACK UI contract passed: HISTORY, per-hit lanes, loupe, one-row readout\n";
    return EXIT_SUCCESS;
}
