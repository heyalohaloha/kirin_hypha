#pragma once
#include "ReferenceACaptureModel.h"
#include "ReferenceAnalysis.h"
#include "ReferencePersistedState.h"
#include "ReferenceTonalRepository.h"
#include "ReferenceVisualTimeline.h"
#include <juce_audio_formats/juce_audio_formats.h>
namespace hypha::reference_audition
{
class ACaptureProjection final : private juce::Thread
{
public:
    ACaptureProjection(std::shared_ptr<ACaptureAccess>,std::function<VisualBinding()>,
        std::shared_ptr<ReferenceAnalysis> = std::make_shared<ReferenceAnalysis>(),
        std::function<TonalDisplayState()> = {},juce::File transportRoot = {},
        std::function<VisualBinding()> tonalReferenceBinding = {});
    ~ACaptureProjection() override;
    void setPresented(bool value) { presented=value; }
    std::shared_ptr<const VisualTimeline> snapshot() const;
private:
    std::shared_ptr<ReferenceAnalysis> analysis;
    std::shared_ptr<ACaptureAccess> access;
    std::function<VisualBinding()> binding;
    std::function<TonalDisplayState()> tonalSelection;
    std::function<VisualBinding()> tonalReferenceBinding;
    juce::File transportRoot;
    ReferenceTonalRepository tonalRepository;
    std::atomic<bool> presented{false};
    mutable juce::CriticalSection mutex;
    std::shared_ptr<const VisualTimeline> published;
    void run() override;
};
}
