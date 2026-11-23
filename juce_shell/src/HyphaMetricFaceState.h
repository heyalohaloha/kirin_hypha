#pragma once
#include "HyphaMetricFacePainter.h"
#include "HyphaMetricInstrumentCandidate.h"
#include <memory>
namespace hypha::metric_face
{
struct Selection
{
    metric_needle::Face face = metric_needle::Face::vu;
    metric_needle::FrameUpdate frames;
    std::shared_ptr<const MetricChassisPair> candidateChassis;
    MeterBackdrop candidateBackdrop;
    // PRE local SHARP exceeds the optional CPU gate; production keeps it absent.
    // Test fixtures may enable it only to exercise shared geometry.
    bool preSharpAvailable = false;
    bool configured = false;

    std::optional<bool> referenceReady() const noexcept
    {
        if (! metric_needle::metric (face)) return std::nullopt;
        const auto& frame = frames.current();
        return frame.face == face && frame.status == metric_needle::Status::ready && std::isfinite (frame.median);
    }
};
}
