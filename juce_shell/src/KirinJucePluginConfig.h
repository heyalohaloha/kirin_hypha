#pragma once

// CMake command lines are a brittle place for JUCE's brace-heavy preferred
// channel list. Force-include this header so the AU wrapper receives the exact
// mono/stereo map Logic needs.
#ifndef JucePlugin_PreferredChannelConfigurations
#define JucePlugin_PreferredChannelConfigurations {1, 1}, {2, 2}
#endif
