#pragma once

namespace hypha::tests
{
namespace domain_bed_key_contract
{
template <class Tag, typename Tag::Type Member> struct Access
{ friend typename Tag::Type member (Tag) { return Member; } };
struct Raster
{
    using Type = juce::Image observatory_world::Backdrop::*;
    friend Type member (Raster);
};
template struct Access<Raster, &observatory_world::Backdrop::domainBed>;
inline void verify()
{
    for (auto domain : { observatory::Domain::frequency, observatory::Domain::space,
                         observatory::Domain::reference, observatory::Domain::level,
                         observatory::Domain::time })
    {
        observatory_world::Backdrop retained;
        observatory_world::State state;
        state.domain = domain;
        state.density = observatory::Density::observatory;
        state.active = true;
        state.energy = 0.25f;
        state.direction = -0.3f;
        juce::Image output (juce::Image::ARGB, 300, 200, true);
        juce::Graphics g (output);
        const auto draw = [&] { retained.drawDomainBed (g, { 8, 12, 280, 170 }, state); };
        draw();
        const auto first = retained.*member (Raster {});
        state.direction = 0.7f;
        draw();
        const auto moved = retained.*member (Raster {});
        const bool directionUsed = domain == observatory::Domain::level || domain == observatory::Domain::time;
        KIRIN_OBSERVATORY_REQUIRE ((first.getPixelData() != moved.getPixelData()) == directionUsed);
        state.energy = 0.8f;
        draw();
        const auto energized = retained.*member (Raster {});
        KIRIN_OBSERVATORY_REQUIRE ((moved.getPixelData() != energized.getPixelData())
            == (domain != observatory::Domain::reference));
    }
}
}

inline void verifyObservatoryDomainBedContract()
{
    domain_bed_key_contract::verify();
    observatory_world::Backdrop retained;
    for (auto domain : { observatory::Domain::time, observatory::Domain::frequency,
                         observatory::Domain::space, observatory::Domain::level,
                         observatory::Domain::reference })
        for (float scale : { 1.0f, 1.5f, 2.0f })
            for (int transition = 0; transition < 4; ++transition)
            {
                const juce::Rectangle<int> area (8, 12, transition == 3 ? 430 : 580, 228);
                observatory_world::State state;
                state.domain = domain;
                state.density = observatory::Density::observatory;
                state.active = transition != 0;
                state.energy = transition == 1 ? 0.83f : 0.19f;
                state.direction = transition == 2 ? -0.75f : 0.65f;
                const auto render = [&] (bool cached)
                {
                    juce::Image result (juce::Image::ARGB,
                        juce::roundToInt (600 * scale), juce::roundToInt (260 * scale), true);
                    juce::Graphics graphics (result);
                    graphics.fillAll (BG);
                    graphics.addTransform (juce::AffineTransform::scale (scale));
                    if (cached) retained.drawDomainBed (graphics, area, state);
                    else observatory_world::paintDomainBed (graphics, area, state);
                    return result;
                };
                const auto expected = render (false);
                const auto actual = render (true);
                const auto repeated = render (true);
                double error = 0.0;
                for (int y = 0; y < expected.getHeight(); ++y)
                    for (int x = 0; x < expected.getWidth(); ++x)
                    {
                        const auto a = actual.getPixelAt (x, y);
                        const auto b = expected.getPixelAt (x, y);
                        KIRIN_OBSERVATORY_REQUIRE (a == repeated.getPixelAt (x, y));
                        error += std::abs (static_cast<int> (a.getRed()) - b.getRed());
                        error += std::abs (static_cast<int> (a.getGreen()) - b.getGreen());
                        error += std::abs (static_cast<int> (a.getBlue()) - b.getBlue());
                    }
                KIRIN_OBSERVATORY_REQUIRE (
                    error / (3.0 * expected.getWidth() * expected.getHeight()) < 0.25);
            }
}
}
