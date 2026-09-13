#pragma once
#include <juce_events/juce_events.h>
#include <functional>
namespace hypha
{
// A completed worker-owned snapshot must mark the DAW session dirty even with no editor.
// No notification is posted from processBlock. Destruction cancels queued host callbacks.
class CaptureStateNotification final : private juce::AsyncUpdater
{
public:
    explicit CaptureStateNotification(std::function<void()> value):callback(std::move(value)) {}
    ~CaptureStateNotification() override { cancelPendingUpdate(); }
    void changed() { triggerAsyncUpdate(); }
private:
    std::function<void()> callback;
    void handleAsyncUpdate() override { if(callback) callback(); }
};
}
