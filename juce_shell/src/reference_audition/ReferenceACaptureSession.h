#pragma once
#include "kirin_hypha_reference_capture_ffi.h"
#include "ReferenceACaptureModel.h"
#include <array>
#include <functional>
namespace hypha::reference_audition
{
class ACaptureSession final : private juce::Thread
{
public:
    explicit ACaptureSession(std::function<bool(bool)>, std::function<ACaptureReceipt()> = {});
    ~ACaptureSession() override;
    void shutdown();
    void configure(juce::String, double, int);
    void restore(juce::String,bool shown=true);
    void setPresented(bool);
    void pauseObservation();
    void useAuditionAdmission(bool);
    bool observe(const juce::AudioBuffer<float>&,std::int64_t,bool,bool,bool,int) noexcept;
    std::shared_ptr<ACaptureAccess> access = std::make_shared<ACaptureAccess>();
private:
    struct Block { std::array<float,512> pcm {}; std::int64_t position=0; int frames=0,channels=0,clock=0; std::uint64_t config=0,epoch=0; };
    static constexpr size_t slots=480;
    static_assert(sizeof(Block)*slots<=1024*1024,"Capture and visual queues together remain below 2 MiB");
    std::unique_ptr<std::array<Block,slots>> queue=std::make_unique<std::array<Block,slots>>();
    std::atomic<size_t> writeIndex{0},readIndex{0};
    std::atomic<std::uint64_t> accepting{0},configuration{0},heartbeat{0};
    std::atomic<int> writers{0},terminal{0};
    std::atomic<bool> started{false};
    std::function<bool(bool)> gate;
    std::function<ACaptureReceipt()> receipt;
    KirinReferenceVisualAdmission* observationAdmission=nullptr;
    bool presented=false,borrowed=false,paused=false;
    juce::CriticalSection control;
    juce::String receiver, restoreText;
    bool restorePending=false,restoreShown=true, ownsGate=false, measurementFailed=false;
    int rate=0,channels=0;
    std::uint64_t epoch=0,activeConfig=0,pendingFrames=0,binOffset=0,pendingHash=0;
    std::int64_t revisitExpected=0;
    std::uint64_t revisitHash=0,revisitFrames=0,matchingFrames=0;
    size_t revisitIndex=0;
    std::shared_ptr<ACaptureData> draft;
    ACaptureState state;
    KirinReferenceVisualMeter* meter=nullptr;
    void run() override;
    void consumeRevisit(const Block&);
    void prepareObservation();
    void stampReceipt(ACaptureData&);
    void begin();
    void consume(const Block&);
    void finishBin();
    void close(int);
    void publish();
};
}
