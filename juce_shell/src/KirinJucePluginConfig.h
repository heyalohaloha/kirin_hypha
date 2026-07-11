#pragma once

// CMake command lines are a brittle place for JUCE's brace-heavy preferred
// channel list. Force-include this header so the AU wrapper receives the exact
// mono/stereo map Logic needs.
#ifndef JucePlugin_PreferredChannelConfigurations
#define JucePlugin_PreferredChannelConfigurations {1, 1}, {2, 2}
#endif

// Expose whether JUCE's AU playhead used the host transport timeline or the mandatory
// AudioUnit render timestamp fallback. This is metadata only; it does not alter JUCE timing.
#ifndef KIRIN_HYPHA_AU_CLOCK_PROVENANCE
#define KIRIN_HYPHA_AU_CLOCK_PROVENANCE 1
#endif
