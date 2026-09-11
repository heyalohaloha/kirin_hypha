#include "JungleAppearanceContractTest.h"

#include <array>
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
    std::cerr << "Jungle appearance contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_JUNGLE_REQUIRE(expression) require ((expression), #expression, __LINE__)

juce::Image render (observatory::View& view)
{
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
    juce::Graphics graphics (image);
    view.paintEntireComponent (graphics, true);
    return image;
}

int differentPixels (const juce::Image& left, const juce::Image& right)
{
    KIRIN_JUNGLE_REQUIRE (left.getBounds() == right.getBounds());
    int count = 0;
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            count += left.getPixelAt (x, y).getARGB()
                != right.getPixelAt (x, y).getARGB();
    return count;
}

void writePreview (const juce::String& directory,
                   const juce::String& name,
                   const juce::Image& image)
{
    if (directory.isEmpty())
        return;
    auto output = juce::File (directory).getChildFile (name).createOutputStream();
    KIRIN_JUNGLE_REQUIRE (output != nullptr);
    KIRIN_JUNGLE_REQUIRE (
        juce::PNGImageFormat().writeImageToStream (image, *output));
}

void configure (observatory::View& view,
                observatory::Role role,
                const KirinMeterSession& meter,
                const KirinWatchDisplay& watch,
                const std::vector<KirinMeterHistoryEntry>& history)
{
    view.setConnection (role == observatory::Role::post ? "PAIR DRUM" : "SOURCE PRE",
                        COL_LED_BLUE,
                        role == observatory::Role::post
                            ? observatory::ConnectionState::paired
                            : observatory::ConnectionState::source);
    view.setGuide ("MASKING 03:18", "3150-3700 HZ", true);
    view.setMeterSnapshot (meter, true);
    view.setWatchDisplay (watch, true);
    view.setHistory (history);
}

void verifyRole (observatory::Role role,
                 const KirinMeterSession& meter,
                 const KirinWatchDisplay& watch,
                 const std::vector<KirinMeterHistoryEntry>& history)
{
    const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
        "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    const auto prefix = juce::String (role == observatory::Role::pre ? "pre" : "post");
    for (const auto preset : observatory::sizePresets)
    {
        observatory::View view (role);
        view.setSize (preset.width, preset.height);
        configure (view, role, meter, watch, history);
        for (const auto domain : {
                 observatory::Domain::level, observatory::Domain::time,
                 observatory::Domain::frequency, observatory::Domain::space,
                 observatory::Domain::reference })
        {
            view.setDomain (domain);
            const auto normal = render (view);
            view.setJungleAppearance (true);
            KIRIN_JUNGLE_REQUIRE (view.jungleAppearanceEnabledForTest());
            const auto jungle = render (view);
            KIRIN_JUNGLE_REQUIRE (differentPixels (normal, jungle) > 8);
            writePreview (previewDirectory,
                          prefix + "-jungle-domain-"
                              + juce::String (static_cast<int> (domain)) + "-"
                              + juce::String (preset.width) + ".png",
                          jungle);
            view.setJungleAppearance (false);
            KIRIN_JUNGLE_REQUIRE (differentPixels (normal, render (view)) == 0);
        }

        view.setDomain (observatory::Domain::level);
        view.setManualHybridVuVisible (true);
        const auto normalVu = render (view);
        view.setJungleAppearance (true);
        const auto jungleVu = render (view);
        KIRIN_JUNGLE_REQUIRE (differentPixels (normalVu, jungleVu) > 40);
        writePreview (previewDirectory,
                      prefix + "-vu-" + juce::String (preset.width) + ".png", normalVu);
        writePreview (previewDirectory,
                      prefix + "-jungle-vu-" + juce::String (preset.width) + ".png", jungleVu);
        view.setJungleAppearance (false);
        KIRIN_JUNGLE_REQUIRE (differentPixels (normalVu, render (view)) == 0);
    }
}
}

void verifyJungleAppearanceContract (
    const KirinMeterSession& meter,
    const KirinWatchDisplay& watch,
    const std::vector<KirinMeterHistoryEntry>& history,
    const KirinObservatoryFrame& frame)
{
    verifyRole (observatory::Role::pre, meter, watch, history);
    verifyRole (observatory::Role::post, meter, watch, history);

    observatory::View post (observatory::Role::post);
    post.setSize (600, 400);
    configure (post, observatory::Role::post, meter, watch, history);
    post.setObservatoryFrame (frame, true);
    const auto normalCapture = post.createCaptureImage (1200, 630, true);
    post.setJungleAppearance (true);
    KIRIN_JUNGLE_REQUIRE (
        differentPixels (normalCapture, post.createCaptureImage (1200, 630, true)) > 80);
    post.setJungleAppearance (false);
    KIRIN_JUNGLE_REQUIRE (
        differentPixels (normalCapture, post.createCaptureImage (1200, 630, true)) == 0);
}
}
