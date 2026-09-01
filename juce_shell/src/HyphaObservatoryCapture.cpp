#include "HyphaObservatoryView.h"

namespace hypha::observatory
{
namespace
{
SizePreset capturePreset (int pixelWidth, int pixelHeight)
{
    const int width = juce::jmax (
        1, juce::roundToInt ((float) pixelWidth / captureRenderScale));
    const int height = juce::jmax (
        1, juce::roundToInt ((float) pixelHeight / captureRenderScale));
    const auto density = width < 338 ? Density::compact
                       : width < 413 ? Density::focused
                       : width < 525 ? Density::standard : Density::observatory;
    return { width, height, density, "CAPTURE" };
}

juce::Rectangle<int> scaled (Rect rect)
{
    return {
        juce::roundToInt ((float) rect.x * captureRenderScale),
        juce::roundToInt ((float) rect.y * captureRenderScale),
        juce::roundToInt ((float) rect.width * captureRenderScale),
        juce::roundToInt ((float) rect.height * captureRenderScale),
    };
}
}

juce::Image View::createCaptureImage (int pixelWidth, int pixelHeight,
                                      bool includeGuide,
                                      juce::String capturedAt,
                                      juce::String productVersion,
                                      capture::DisplayMetadata metadata,
                                      const std::vector<KirinMeterHistoryEntry>* historySnapshot) const
{
    const auto preset = capturePreset (pixelWidth, pixelHeight);
    View frame (role);
    frame.selectedDomain = selectedDomain;
    frame.selectedTarget = selectedTarget;
    frame.timeRange = timeRange;
    frame.observatoryFrame = observatoryFrame;
    frame.frameAvailable = frameAvailable;
    frame.watchDisplay = watchDisplay;
    frame.watchDisplayAvailable = watchDisplayAvailable;
    frame.selectedShortTermLoudness = selectedShortTermLoudness;
    frame.compactShowsMaximum = compactShowsMaximum;
    frame.connectionText = connectionText;
    frame.connectionColour = connectionColour;
    frame.connectionState = connectionState;
    if (includeGuide)
    {
        frame.guidePrimary = guidePrimary;
        frame.guideDetail = guideDetail;
        frame.guideEmphasized = guideEmphasized;
    }
    frame.history = historySnapshot != nullptr ? *historySnapshot : history;
    frame.captureFrame = true;
    frame.captureTimestamp = capturedAt.isNotEmpty()
        ? std::move (capturedAt)
        : juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
    frame.captureVersion = std::move (productVersion);
    frame.captureMetadata = metadata.normalized();
    frame.onCapture = [] {};
    frame.updateControls();
    frame.setSize (preset.width, preset.height);

    juce::Image image (juce::Image::RGB, pixelWidth, pixelHeight, false);
    juce::Graphics graphics (image);
    graphics.addTransform (juce::AffineTransform::scale (captureRenderScale));
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
