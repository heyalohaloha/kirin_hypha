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

// Surface host-supplied cumulative input/output presentation latency through JUCE's
// PositionInfo. The wrapper stores host callbacks atomically; processBlock only reads them.
#ifndef KIRIN_HYPHA_PRESENTATION_CLOCK
#define KIRIN_HYPHA_PRESENTATION_CLOCK 1
#endif

// Target-specific CMake wiring enables the independent PRE display controller only in PRE.
// POST compiles the established processor/editor without the controller, filesystem scan, or UI.
#ifndef KIRIN_HYPHA_PRE_DISPLAY
#define KIRIN_HYPHA_PRE_DISPLAY 0
#endif

#ifndef KIRIN_HYPHA_GUIDE_TRANSPORT
#define KIRIN_HYPHA_GUIDE_TRANSPORT 0
#endif
