#pragma once
#include "ReferenceACaptureModel.h"
#include "ReferenceVisualTimeline.h"
#include <juce_audio_formats/juce_audio_formats.h>
namespace hypha::reference_audition
{
class ACaptureProjection final : private juce::Thread
{
public:
    ACaptureProjection(std::shared_ptr<ACaptureAccess>,std::function<VisualBinding()>);
    ~ACaptureProjection() override;
    void setPresented(bool value) { presented=value; }
    std::shared_ptr<const VisualTimeline> snapshot() const;
private:
    std::shared_ptr<ACaptureAccess> access;
    std::function<VisualBinding()> binding;
    std::atomic<bool> presented{false};
    mutable juce::CriticalSection mutex;
    std::shared_ptr<const VisualTimeline> published;
    void run() override;
};
}
