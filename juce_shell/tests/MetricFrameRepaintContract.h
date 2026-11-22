#pragma once
#include "../src/HyphaObservatoryView.h"
#include "MetricFrameUpdateContract.h"

namespace hypha::tests
{
template<class Render, class Difference, class Check>
void verifyMetricFrameRepaint (observatory::View& view, const metric_needle::Frame& frame,
    const juce::Image& full, Render render, Difference differences, Check check)
{
    verifyMetricFrameUpdate (check);
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
