#pragma once
// Compiler identity for test manifests. __VERSION__ is GCC/Clang only; MSVC reports _MSC_FULL_VER.
#include <juce_core/juce_core.h>

namespace hypha::tests
{
inline juce::String compilerVersionString()
{
#if defined(_MSC_VER) && ! defined(__clang__)
    return "MSVC " + juce::String (_MSC_FULL_VER);
#else
    return __VERSION__;
#endif
}
}
