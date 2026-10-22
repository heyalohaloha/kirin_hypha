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
