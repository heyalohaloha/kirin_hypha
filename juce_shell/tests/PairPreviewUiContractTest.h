#pragma once
#include "../src/HyphaWidgets.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyPairPreviewUiContract()
{
    const auto require = [] (bool ok, const char* message)
    { if (! ok) { std::cerr << "Pair preview UI: " << message << '\n'; std::exit (EXIT_FAILURE); } };
    EditableName name;
    name.setPresentationContext (presentation::forEditor (900,600)); name.setSize (300,30);
    name.setModelName ({}); name.setFallback ("SELECT PRE");
    bool selected = false, demanded = false;
    name.onSelect = [&] { selected = true; }; name.onPreviewDemand = [&] { demanded = true; };
    require (name.setSelectionPreview ("CONNECT PRE 01234567",7), "the full explicit identity label fits");
    require (name.paintedSelectionGeneration() == 0 && !selected, "receiving a result cannot authorize an unseen label or connect");
    juce::Image image (juce::Image::ARGB,300,30,true); juce::Graphics graphics (image); name.paint (graphics);
    require (name.paintedSelectionGeneration() == 7,"painting acknowledges this exact preview generation");
    name.focusGained (juce::Component::focusChangedByTabKey);
    require (demanded && !selected,"keyboard focus requests evidence but never connects");
    require (name.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)) && selected,"keyboard selection has a direct action");
    name.setSelectionPreview ("CONNECT PRE 76543210",8);
    require (name.paintedSelectionGeneration() == 0,"changed generation invalidates the painted receipt");
    name.setSize (70,30);
    name.paint (graphics); require (name.paintedSelectionGeneration() == 0,"resize invalidates a preview before the next timer update");
    require (!name.setSelectionPreview ("CONNECT PRE 76543210",8),"narrow labels retain the ordinary menu instead of truncating identity");
    name.paint (graphics); require (name.paintedSelectionGeneration() == 0,"a clipped preview cannot be selected as displayed");
}
}
