#include "SpectrumShapeContractTest.h"

#include "../src/HyphaSpectrumDeltaSelection.h"
#include "../src/HyphaSpectrumGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <utility>

namespace hypha::tests
{
namespace
{
void require (bool value, const char* expression, int line)
{
    if (value) return;
    std::cerr << "Spectrum SHAPE contract failed at " << line << ": "
              << expression << '\n';
    std::exit (EXIT_FAILURE);
}
#define SHAPE_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::MouseEvent eventAt (juce::Component& component, juce::Point<float> point)
{
    const auto time = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), point, {},
             0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component,
             time, point, time, 0, false };
}
}

void verifySpectrumShapeContract (const KirinSpectrumView& baseline)
{
    auto view = baseline;
    if (view.has_data == 0u)
    {
        view.status = KIRIN_SPECTRUM_ACTIVE;
        view.has_data = view.post_has_data = 1u;
        view.channel_mode = KIRIN_SPECTRUM_CHANNEL_LR;
        view.channels = 2u;
        view.sample_rate = 48'000u;
        view.aperture_samples = 4'096u;
        view.fft_size = 8'192u;
        view.approximate_below_hz = 35.15625f;
        view.presentation_end_samples = 48'000;
        view.min_hz = 10.0f;
        view.max_hz = 22'000.0f;
        std::fill (std::begin (view.pre_dbfs), std::end (view.pre_dbfs), -30.0f);
        std::fill (std::begin (view.post_dbfs), std::end (view.post_dbfs), -24.0f);
        std::fill (std::begin (view.display_db), std::end (view.display_db), 6.0f);
    }
    view.shape_has_energy = 1u;
    view.shape_energy_delta_db = 6.0f;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        view.shape_db[index] = 3.0f;
        view.shape_valid[index] = 1u;
    }
    view.shape_valid[100] = 0u;
    const spectrum_delta::Bins calm {};
    const auto raw = spectrum_delta::select (view, false, calm);
    const auto shape = spectrum_delta::select (view, true, calm);
    SHAPE_REQUIRE (raw.valid[100] == 1u);
    SHAPE_REQUIRE (shape.valid[100] == 0u);
    SHAPE_REQUIRE (shape.values[100] == 0.0f);
    SHAPE_REQUIRE (shape.valid[101] == 1u && shape.values[101] == 3.0f);
    view.shape_has_energy = 0u;
    SHAPE_REQUIRE (! spectrum_delta::select (view, true, calm).anyValid);
    view.shape_has_energy = 1u;

    SpectrumComponent component;
    component.setSize (600, 400);
    component.setSignalActive (true);
    component.setSnapshot (view);
    SHAPE_REQUIRE (! component.isShapeObservationForTest());
    SHAPE_REQUIRE (component.readoutDeltaValidForTest (100));
    const auto bounds = component.getLocalBounds().toFloat();
    const auto scale = spectrum_geometry::visualScaleFor (bounds);
    const auto selector = spectrum_geometry::deltaModeBoundsFor (
        spectrum_geometry::plotBoundsFor (bounds), scale);
    const auto mark = spectrum_geometry::markBoundsFor (
        spectrum_geometry::plotBoundsFor (bounds), scale);
    component.mouseDown (eventAt (component, mark.getCentre()));
    SHAPE_REQUIRE (component.hasMark());
    component.mouseDown (eventAt (component,
                                  { selector.getRight() - 1.0f, selector.getCentreY() }));
    SHAPE_REQUIRE (component.isShapeObservationForTest());
    SHAPE_REQUIRE (! component.hasMark());
    SHAPE_REQUIRE (component.focusTrailSizeForTest() == 1u);
    SHAPE_REQUIRE (! component.readoutDeltaValidForTest (100));
    SHAPE_REQUIRE (component.readoutDeltaValidForTest (101));
    SHAPE_REQUIRE (std::abs (component.readoutDeltaForTest (101) - 3.0f) < 0.001f);
    if (const auto* preview = std::getenv ("HYPHA_SHAPE_PREVIEW"))
    {
        juce::Image image (juce::Image::ARGB, 600, 400, true);
        { juce::Graphics graphics (image); component.paintEntireComponent (graphics, true); }
        auto output = std::unique_ptr<juce::FileOutputStream> (
            juce::File (preview).createOutputStream());
        SHAPE_REQUIRE (output != nullptr);
        SHAPE_REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *output));
    }
    auto invalid = view;
    invalid.shape_has_energy = 0u;
    component.setSnapshot (invalid);
    SHAPE_REQUIRE (! component.readoutDeltaValidForTest (101));
    component.mouseDown (eventAt (component, mark.getCentre()));
    SHAPE_REQUIRE (! component.hasMark());
    component.mouseDown (eventAt (component,
                                  { selector.getX() + 1.0f, selector.getCentreY() }));
    SHAPE_REQUIRE (! component.isShapeObservationForTest());
    SHAPE_REQUIRE (component.readoutDeltaValidForTest (100));

    for (const auto [width, height] : { std::pair { 300, 200 }, { 375, 250 },
                                        { 450, 300 }, { 600, 400 }, { 900, 600 } })
    {
        SpectrumComponent sized;
        sized.setSize (width, height);
        sized.setSignalActive (true);
        sized.setSnapshot (view);
        const auto area = sized.getLocalBounds().toFloat();
        const auto control = spectrum_geometry::deltaModeBoundsFor (
            spectrum_geometry::plotBoundsFor (area),
            spectrum_geometry::visualScaleFor (area));
        sized.mouseDown (eventAt (sized,
                                  { control.getRight() - 1.0f, control.getCentreY() }));
        SHAPE_REQUIRE (sized.isShapeObservationForTest());
        juce::Image image (juce::Image::ARGB, width, height, true);
        { juce::Graphics graphics (image); sized.paintEntireComponent (graphics, true); }
        SHAPE_REQUIRE (image.getPixelAt (width / 2, height / 2).getARGB() != 0u);
    }

    spectrum_focus::FocusTrailHistory trail;
    spectrum_focus::DeltaBins values {};
    values.fill (3.0f);
    spectrum_focus::Validity mask {};
    mask.fill (1u);
    mask[100] = 0u;
    SHAPE_REQUIRE (trail.append (1'600, 48'000, values, mask)
                   == spectrum_focus::AppendResult::appended);
    SHAPE_REQUIRE (std::isnan (trail.valueAt (0u, spectrum_geometry::bandCentreNormalisedX (100))));
    SHAPE_REQUIRE (std::isfinite (
        trail.valueAt (0u, spectrum_geometry::bandCentreNormalisedX (110))));
    std::cout << "Spectrum SHAPE contract: RAW/SHAPE, mask, toggle, trail PASS\n";
}
}
