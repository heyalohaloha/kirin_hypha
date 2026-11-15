#pragma once
#include "SpectrumPerformanceFixture.h"
#include "../src/HyphaSpectrumComponent.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// Measures one FREQ paint and one presentation tick, with the established display and with every
// opt-in choice on. The time is printed, not judged: it belongs to the machine. What is judged
// holds everywhere: the established picture repeats exactly and the opt-in picture differs.
// Run alone with --freq-paint-cost-only.
inline void measureSpectrumPaintCost()
{
    const auto check = [] (bool ok, const char* what)
    { if (! ok) { std::cerr << "FREQ paint cost failed: " << what << '\n'; std::exit (1); } };
    const auto everyChoice = []
    {
        display_settings::Values settings;
        // SMOOTH 1/12-1/3 oct, PEAK SLOW, FALL 6 dB/s, FILL RICH; older builds keep what they have.
        for (const auto& [field, choice] : { std::pair { 2, 6 }, std::pair { 4, 2 }, std::pair { 6, 3 }, std::pair { 7, 1 } })
            if (display_settings::valid (field, choice)) settings.selected[(size_t) field] = choice;
        return settings;
    }();
    struct Surface { const char* name; bool native; float pixels; };
    for (const bool absolute : { true, false })
        for (const int percent : { 100, 300 })
            for (const auto surface : { Surface { "Native", true, 1.0f }, Surface { "Native 2x", true, 2.0f },
                                        Surface { "Software", false, 1.0f } })
            {
                double milliseconds[2] {};
                uint64_t hashes[2] {};
                for (const bool rich : { false, true })
                {
                    SpectrumComponent view;
                    view.setSize (288 * percent / 100, 150 * percent / 100);
                    view.setAbsoluteObservation (absolute); view.setSignalActive (true);
                    view.setDisplaySettings (rich ? everyChoice : display_settings::Values {});
                    auto frame = spectrumPerformanceFixture();
                    for (auto& value : frame.post_dbfs) value += 9.0f;
                    view.setSnapshot (frame);
                    frame = spectrumPerformanceFixture(); frame.presentation_end_samples += 1'600;
                    view.queueSnapshot (frame);
                    view.presentationTickAt (view.curveUpdateTimeForTest() + 100.0);
                    const auto width = juce::roundToInt ((float) view.getWidth() * surface.pixels);
                    const auto height = juce::roundToInt ((float) view.getHeight() * surface.pixels);
                    juce::Image image (juce::Image::ARGB, width, height, true,
                        surface.native ? static_cast<const juce::ImageType&> (juce::NativeImageType())
                                       : static_cast<const juce::ImageType&> (juce::SoftwareImageType()));
                    const auto paint = [&]
                    {
                        image.clear (image.getBounds());
                        juce::Graphics g (image);
                        g.addTransform (juce::AffineTransform::scale (surface.pixels));
                        const auto started = juce::Time::getMillisecondCounterHiRes();
                        view.paintEntireComponent (g, false);
                        return juce::Time::getMillisecondCounterHiRes() - started;
                    };
                    const auto hash = [] (const juce::Image& image)
                    {
                        uint64_t value = 1469598103934665603ull;
                        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
                        for (int y = 0; y < data.height; ++y)
                            for (int x = 0; x < data.width; ++x)
                                value = (value ^ data.getPixelColour (x, y).getARGB()) * 1099511628211ull;
                        return value;
                    };
                    paint();
                    hashes[rich] = hash (image);
                    paint();
                    check (hashes[rich] == hash (image), "a settled picture repeats exactly");
                    constexpr int rounds = 40;
                    double elapsed = 0;
                    for (int round = 0; round < rounds; ++round) elapsed += paint();
                    milliseconds[rich] = elapsed / rounds;
                }
                if (everyChoice != display_settings::Values {})
                    check (hashes[0] != hashes[1], "the opt-in choices change the picture");
                std::cout << "FREQ paint " << (absolute ? "POST " : "DELTA ") << percent << "% " << surface.name
                          << ": established " << milliseconds[0] << " ms (picture " << std::hex << hashes[0] << std::dec
                          << "), every choice " << milliseconds[1] << " ms\n";
            }
    for (const bool rich : { false, true })
    {
        SpectrumComponent view; view.setSize (864, 450);
        view.setAbsoluteObservation (true); view.setSignalActive (true);
        view.setDisplaySettings (rich ? everyChoice : display_settings::Values {});
        auto frame = spectrumPerformanceFixture();
        view.setSnapshot (frame);
        auto now = view.curveUpdateTimeForTest();
        constexpr int rounds = 4'000;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int round = 0; round < rounds; ++round)
        {
            frame.presentation_end_samples += 1'600;
            frame.post_dbfs[(size_t) round % KIRIN_SPECTRUM_BAND_COUNT] += (round & 1) != 0 ? 3.0f : -3.0f;
            view.queueSnapshot (frame);
            view.presentationTickAt (now += 16.0);
        }
        std::cout << "FREQ receive + tick " << (rich ? "every choice " : "established ")
                  << (juce::Time::getMillisecondCounterHiRes() - started) * 1000.0 / rounds << " us\n";
    }
}
}
