public:
    bool supportsDoublePrecisionProcessing() const override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
private:
    void preparePrecisionScratch (int frames, int channels);
    std::vector<float> precisionScratch;
    std::array<float*, 2> precisionPointers {};
    juce::AudioBuffer<float> precisionView;
    int precisionScratchFrames = 0, precisionScratchChannels = 0;
public:
    // Not precision: the Hybrid VU needle's audio-thread integration (HyphaVuLiveTap.h) is declared in
    // this split because PluginProcessor.h is at its line budget. The editor enables it only while a VU
    // is on screen and reads it at frame rate; prepareToPlay sizes it.
    hypha::hybrid_vu::LiveTap& vuLive() noexcept { return vuLiveTap; }
private:
    hypha::hybrid_vu::LiveTap vuLiveTap;
public:
