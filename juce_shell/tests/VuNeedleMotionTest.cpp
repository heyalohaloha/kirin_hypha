#include "../src/HyphaVuNeedleMotion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}
}

int main()
{
    using hypha::hybrid_vu::NeedleMotion;
    NeedleMotion sixty, fast;
    sixty.setTarget (1.0);
    fast.setTarget (1.0);
    double previous = 0.0;
    for (int i = 1; i <= 6; ++i)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() >= previous && sixty.position() <= 1.0,
                 "step must be monotone and not overshoot");
        previous = sixty.position();
    }
    require (sixty.position() >= 0.99, "step must reach 99% within 100 ms");
    for (int i = 0; i < 12; ++i) fast.advance (1.0 / 144.0);
    NeedleMotion other;
    other.setTarget (1.0);
    for (int i = 0; i < 5; ++i) other.advance (1.0 / 60.0);
    require (std::abs (fast.position() - other.position()) < 1.0e-10,
             "elapsed time must determine position");
    NeedleMotion ramp;
    double largestFrame = 0.0;
    double previousFrame = 0.0;
    bool foundIntermediate = false;
    for (int frame = 0; frame < 60; ++frame)
    {
        if (frame % 6 == 0) ramp.setTarget (0.1 * (frame / 6 + 1));
        ramp.advance (1.0 / 60.0);
        const auto position = ramp.position();
        largestFrame = std::max (largestFrame, std::abs (position - previousFrame));
        foundIntermediate |= position > previousFrame && position < ramp.target();
        previousFrame = position;
    }
    require (foundIntermediate && largestFrame < 0.05,
             "60 Hz ramp must interpolate the 100 ms publication steps");
    hypha::hybrid_vu::NeedleMotionPair pair;
    pair.setTarget (0, 1.0);
    int work = 0;
    for (int frame = 0; frame < 30; ++frame) work += pair.tick (1.0 / 60.0);
    require (work == 0, "hidden callbacks must do no frame work");
    pair.setVisible (true);
    for (int frame = 0; frame < 120; ++frame) work += pair.tick (1.0 / 60.0);
    const auto settledWork = work;
    for (int frame = 0; frame < 30; ++frame) work += pair.tick (1.0 / 60.0);
    require (settledWork > 0 && work == settledWork, "settled callbacks must do no frame work");
    pair.setTarget (1, 0.5);
    require (pair.active(), "a new publication must resume work");
    pair.setVisible (false);
    require (! pair.tick (1.0 / 60.0), "hiding must stop frame work");
    NeedleMotion missing;
    missing.snap (1.0);
    missing.setTarget (std::numeric_limits<double>::quiet_NaN());
    require (! missing.advance (0.0) && ! missing.advance (-0.1), "invalid dt must do no work");
    missing.advance (0.251);
    require (missing.position() == 0.0 && ! missing.active(), "missing value must rest at zero");
    NeedleMotion jump;
    jump.setTarget (0.7);
    jump.advance (0.251);
    require (jump.position() == 0.7 && ! jump.active(), "long pause must snap");
    for (int i = 0; i < 120 && sixty.active(); ++i) sixty.advance (1.0 / 60.0);
    require (! sixty.active() && sixty.position() == 1.0, "motion must settle");
    require (! sixty.advance (1.0 / 60.0), "settled motion must do no work");
    sixty.setTarget (0.0);
    previous = sixty.position();
    for (int frame = 0; frame < 6; ++frame)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() <= previous && sixty.position() >= 0.0,
                 "falling step must be monotone and not overshoot");
        previous = sixty.position();
    }
    require (sixty.position() <= 0.01, "falling step must reach 99% within 100 ms");
    sixty.setTarget (1.0);
    for (int i = 0; i < 120 && sixty.active(); ++i)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() >= 0.0 && sixty.position() <= 1.0,
                 "retarget must stay within the scale");
    }
    return 0;
}
