#pragma once
#include "../src/HyphaObservatoryView.h"
#include "MetricFrameUpdateContract.h"

namespace hypha::tests
{
template<class Render, class Difference, class Check>
void verifyWatchOnlyRepaint (const metric_needle::Frame& frame, const KirinMeterSession& meter,
    const lu_needle::Facts& reference, const std::shared_ptr<const metric_face::MetricChassisPair>& mid,
    Render render, Difference differences, Check check)
{
    using namespace observatory;
    View view (frame.ownIsPre ? Role::pre : Role::post, mid);
    view.setSize (300, 200);
    const auto previousDomain = frame.ownIsPre ? Domain::time : Domain::frequency;
    view.setDomain (previousDomain);
    view.setMeterSnapshot (meter, true);
    view.setHybridVuOnRecordEnabled (true);
    view.lu().setFacts (reference);
    view.setMetricFace (frame.face);
    view.setMetricFrame (frame);
    KirinWatchDisplay watch {};
    watch.current.lufs_m = -13.0; watch.current.lufs_s = -12.0; watch.current.crest = 11.0;
    check (view.domain() == previousDomain && !view.hybridVuVisible() && !view.setWatchDisplay (watch, true),
           "non-LEVEL with Hybrid hidden stores the Watch facts without repaint");
    check (view.setHostRecording (true) && view.hybridVuShownByRecording() && view.metricFaceActive(),
           "recording over a non-LEVEL page presents the actual Hybrid CREST face");
    const auto before = render (view);
    const auto readouts = juce::Rectangle<int> (0, 144, 300, 31);
    watch.current.lufs_m = -28.0; watch.current.lufs_s = -27.0; watch.current.crest = 19.0;
    check (!view.setMetricFrame (frame) && view.setWatchDisplay (watch, true),
           "Watch-only value change requests one repaint with the same metric frame and meter");
    const auto changed = render (view);
    check (differences (before, changed, readouts) > 0, "actual Hybrid LUFS/CREST readouts change");
    check (!view.setWatchDisplay (watch, true)
        && differences (changed, render (view), changed.getBounds()) == 0,
           "equal repeat requests no repaint and leaves all pixels identical");
    check (view.setWatchDisplay (watch, false), "availability alone requests one repaint");
    const auto absent = render (view);
    check (differences (changed, absent, readouts) > 0
        && !view.setWatchDisplay (watch, false), "unavailable readouts clear without redundant repaint");
    check (view.setHostRecording (false) && !view.hybridVuVisible()
        && !view.setWatchDisplay (watch, true), "hidden non-LEVEL again requests zero");
}

template<class Render, class Difference, class Check>
void verifyMetricFrameRepaint (observatory::View& view, const metric_needle::Frame& frame,
    const juce::Image& full, Render render, Difference differences, Check check,
    const KirinMeterSession& meter, const lu_needle::Facts& reference,
    const std::shared_ptr<const metric_face::MetricChassisPair>& mid)
{
    verifyMetricFrameUpdate (check);
    verifyWatchOnlyRepaint (frame, meter, reference, mid, render, differences, check);
    auto idle = frame; idle.own = idle.pre = idle.reference = metric_needle::missing;
    check (view.setMetricFrame (idle), "changed value requests repaint");
    const auto unchanged = render (view);
    auto duplicate = idle; duplicate.own = std::nan ("42"); // Value is still missing.
    int requests = 0;
    for (int tick = 0; tick < 60; ++tick)
        requests += view.setMetricFrame (duplicate) ? 1 : 0;
    check (requests == 0, "60 repeated metric frames request zero repaints");
    check (differences (unchanged, render (view), unchanged.getBounds()) == 0,
           "skipped frames leave every pixel identical in the product View");
    duplicate.context.playing = true;
    check (view.setMetricFrame (duplicate), "play start repaints before any measured value");
    ++duplicate.context.playbackRun;
    check (view.setMetricFrame (duplicate), "new playback run repaints even between View polls");
    duplicate.context.referenceIdentity = "different-source-same-name";
    check (view.setMetricFrame (duplicate), "Reference identity change repaints with equal data");
    ++duplicate.context.referenceRevision;
    check (view.setMetricFrame (duplicate), "Reference revision repaints with equal data");
    check (differences (unchanged, render (view), unchanged.getBounds()) == 0,
           "invalidation metadata does not alter the current pixels");
    duplicate.reason = "preparing";
    check (view.setMetricFrame (duplicate), "reason forces repaint");
    duplicate.status = metric_needle::Status::measuring;
    check (view.setMetricFrame (duplicate), "state forces repaint");
    check (!view.setMetricFrame (duplicate), "stable state and reason skip repaint");
    check (view.setMetricFrame (frame), "restore ready frame repaints");
    check (differences (full, render (view), full.getBounds()) == 0,
           "restored ready frame is pixel-identical");
}
}
