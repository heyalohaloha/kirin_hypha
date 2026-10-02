#pragma once

#include <juce_core/juce_core.h>

namespace hypha::host_identity
{
// Retain both native facts. AAX's certificate was measured against the fixed
// file version (all of its four components fit WORDs); VST3 Studio Pro needs
// the textual build number, which cannot fit in the fourth fixed WORD.
struct Identity { juce::String name, version, fileVersion; };

// Non-RT only. Read the main process executable, never the plugin DLL/bundle.
// macOS versions live in the containing app. Windows fixed WORDs can omit a
// build number: keep the complete FileVersion text separate from that fact.
Identity read (const juce::File& executable);
Identity current();
}
