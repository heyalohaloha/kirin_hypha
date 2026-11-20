#pragma once
#include "HyphaMetricFacePainter.h"
namespace hypha::metric_face
{
struct Selection
{
    metric_needle::Face face = metric_needle::Face::vu;
    metric_needle::Frame frame;
    // PRE local SHARP exceeds the optional CPU gate; production keeps it absent.
    // Test fixtures may enable it only to exercise shared geometry.
    bool preSharpAvailable = false;
    bool configured = false;
};
}
