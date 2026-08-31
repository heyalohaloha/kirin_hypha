#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
namespace
{
constexpr float captureScale = 2.0f;

SizePreset capturePreset (int pixelWidth, int pixelHeight)
{
    const int width = juce::jmax (1, juce::roundToInt ((float) pixelWidth / captureScale));
    const int height = juce::jmax (1, juce::roundToInt ((float) pixelHeight / captureScale));
    const auto density = width < 338 ? Density::compact
                       : width < 413 ? Density::focused
                       : width < 525 ? Density::standard : Density::observatory;
    return { width, height, density, "CAPTURE" };
}

juce::Rectangle<int> scaled (Rect rect)
{
    return {
        juce::roundToInt ((float) rect.x * captureScale),
        juce::roundToInt ((float) rect.y * captureScale),
        juce::roundToInt ((float) rect.width * captureScale),
        juce::roundToInt ((float) rect.height * captureScale),
    };
}
}

juce::Image View::createCaptureImage (int pixelWidth, int pixelHeight,
                                      bool includeGuide,
                                      juce::String capturedAt,
                                      juce::String productVersion) const
{
    const auto preset = capturePreset (pixelWidth, pixelHeight);
    View frame (role);
    frame.selectedDomain = selectedDomain;
    frame.selectedTarget = selectedTarget;
    frame.timeRange = timeRange;
    frame.observatoryFrame = observatoryFrame;
    frame.frameAvailable = frameAvailable;
    frame.connectionText = connectionText;
    frame.connectionColour = connectionColour;
    if (includeGuide)
    {
        frame.guidePrimary = guidePrimary;
        frame.guideDetail = guideDetail;
        frame.guideEmphasized = guideEmphasized;
    }
    frame.history = history;
    frame.captureFrame = true;
    frame.captureTimestamp = capturedAt.isNotEmpty()
        ? std::move (capturedAt)
        : juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
    frame.captureVersion = std::move (productVersion);
    frame.onCapture = [] {};
    frame.updateControls();
    frame.setSize (preset.width, preset.height);

    juce::Image image (juce::Image::RGB, pixelWidth, pixelHeight, false);
    juce::Graphics graphics (image);
    graphics.addTransform (juce::AffineTransform::scale (captureScale));
    frame.paintEntireComponent (graphics, true);
    return image;
}

juce::Rectangle<int> View::captureBodyBounds (int pixelWidth, int pixelHeight,
                                              bool includeGuide) const
{
    const auto preset = capturePreset (pixelWidth, pixelHeight);
    const auto presence = includeGuide && guidePresence() == GuidePresence::present
        ? GuidePresence::present : GuidePresence::absent;
    return scaled (shellLayout (role, preset, presence).body);
}
}
