#include "TimePageNavigationContractTest.h"

#include "../src/HyphaTimePageNavigation.h"
#include "../src/HyphaTheme.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "TIME page navigation contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_TIME_NAV_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::Image render (TimePageNavigation& navigation)
{
    juce::Image image (juce::Image::ARGB, navigation.getWidth(), navigation.getHeight(), true);
    image.clear (image.getBounds(), BG);
    juce::Graphics graphics (image);
    navigation.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_TIME_NAV_REQUIRE (left.getBounds() == right.getBounds());
    auto count = 0;
    for (auto y = 0; y < left.getHeight(); ++y)
        for (auto x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB() != right.getPixelAt (x, y).getARGB();
    return count;
}

int visiblePixels (const juce::Image& image)
{
    auto count = 0;
    for (auto y = 0; y < image.getHeight(); ++y)
        for (auto x = 0; x < image.getWidth(); ++x)
            count += image.getPixelAt (x, y).getARGB() != BG.getARGB();
    return count;
}
}

void verifyTimePageNavigationContract()
{
    using Page = analysis_navigation::Page;
    KIRIN_TIME_NAV_REQUIRE (analysis_navigation::timePages.size() == 5u);
    for (const auto page : { Page::meters, Page::run, Page::attack,
                             Page::perceptual, Page::absolute })
        KIRIN_TIME_NAV_REQUIRE (analysis_navigation::isTimePage (page));
    KIRIN_TIME_NAV_REQUIRE (! analysis_navigation::isTimePage (Page::spectrum));
    auto page = Page::meters;
    for (const auto expected : { Page::run, Page::attack, Page::perceptual,
                                 Page::absolute, Page::meters })
    {
        page = analysis_navigation::nextTimePage (page);
        KIRIN_TIME_NAV_REQUIRE (page == expected);
    }
    KIRIN_TIME_NAV_REQUIRE (
        juce::String (analysis_navigation::timePageLabel (Page::spectrum)).isEmpty());

    TimePageNavigation navigation;
    navigation.setSize (322, 24);
    navigation.setDirect (true);
    KIRIN_TIME_NAV_REQUIRE (navigation.visibleDirectTabCount() == 4);
    navigation.setRunAvailable (true);
    KIRIN_TIME_NAV_REQUIRE (navigation.visibleDirectTabCount() == 5);
    navigation.setPage (Page::attack);
    const auto attack = render (navigation);
    navigation.setPage (Page::perceptual);
    KIRIN_TIME_NAV_REQUIRE (differentPixels (attack, render (navigation)) > 20);

    const auto outputPath = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_UI_TIME_NAV_OUTPUT", {});
    if (outputPath.isNotEmpty())
    {
        auto output = juce::File (outputPath).createOutputStream();
        KIRIN_TIME_NAV_REQUIRE (output != nullptr);
        KIRIN_TIME_NAV_REQUIRE (juce::PNGImageFormat().writeImageToStream (attack, *output));
    }

    navigation.setDirect (false);
    navigation.setSize (72, 24);
    KIRIN_TIME_NAV_REQUIRE (navigation.visibleDirectTabCount() == 0);
    KIRIN_TIME_NAV_REQUIRE (visiblePixels (render (navigation)) > 50);
}
}
