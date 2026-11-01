// Private processor methods. The implementation feeds original input; it never writes output.
bool feedMeasurementInput (const juce::AudioBuffer<float>&, const hypha::HostProcessClock&,
                           int windowStartFrame, int windowEndFrame, int64_t windowPositionSamples,
                           uint64_t windowNumFrames, bool forceTakeStartEpoch);
void feedNoMeasurement (uint32_t channels) noexcept;
void pushPairedAnalysis (uint64_t capturedFrames, uint32_t channels, int windowStartFrame) noexcept;
