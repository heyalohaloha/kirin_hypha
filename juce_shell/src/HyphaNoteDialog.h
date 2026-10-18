#pragma once

#include "HyphaTheme.h"

#include <functional>
#include <memory>

// The NOTE window of a Keep recording: a memo field, ADD (Return) and CANCEL (Escape), at the size
// its contents need in the language shown (2026-10-18: it was forced to 360x170 after JUCE had laid
// it out, which cut ADD and CANCEL in half at the bottom).
namespace hypha::note_dialog
{
std::unique_ptr<juce::AlertWindow> create (juce::Component& owner, juce::LookAndFeel& lookAndFeel,
                                           const presentation::Context& context);
}
