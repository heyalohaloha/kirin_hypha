#pragma once

namespace hypha::tests
{
// The TIME hypha specimen is drawn on the TIME body and nowhere else.
inline void verifyObservatorySpecimenContract (const observatory_world::Backdrop& backdrop)
{
    juce::Image specimenImage (juce::Image::ARGB, 300, 200, true);
    specimenImage.clear (specimenImage.getBounds(), BG);
    const auto specimenBlank = specimenImage.createCopy();
    {
        juce::Graphics graphics (specimenImage);
        observatory_world::State state;
        state.domain = observatory::Domain::time;
        state.active = true;
        backdrop.drawHyphaSpecimen (graphics, specimenImage.getBounds(), state);
    }
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (specimenBlank, specimenImage) > 2'000);
    juce::Image levelImage (juce::Image::ARGB, 580, 228, true);
    levelImage.clear (levelImage.getBounds(), BG);
    const auto levelBlank = levelImage.createCopy();
    {
        juce::Graphics graphics (levelImage);
        observatory_world::State state;
        state.domain = observatory::Domain::level;
        state.density = observatory::Density::observatory;
        state.active = true;
        backdrop.drawHyphaSpecimen (graphics, levelImage.getBounds(), state);
    }
    KIRIN_OBSERVATORY_REQUIRE (
        differentPixels (levelBlank, levelImage) == 0);
}
}
