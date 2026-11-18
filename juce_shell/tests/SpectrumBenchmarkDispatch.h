#pragma once
#include "SpectrumPacedBenchmark.h"
#include "SpectrumPairedPixelReplay.h"
#include "SpectrumEmptyTipPixelFixture.h"
#include "SpectrumBatchCadenceProfile.h"
#include <string_view>
namespace hypha::tests {
// -1 is not handled. Errors are nonzero and never fall through to another test entry.
inline int dispatchSpectrumBenchmark(int argc,char** argv) {
    if(argc<2) return -1;
    const std::string_view mode(argv[1]);
    const bool paced=mode=="--freq-paced-benchmark",profile=mode=="--freq-phase-profile";
    const bool pixels=mode=="--freq-paired-pixel-replay";
    const bool awake=mode=="--freq-awake-benchmark";
    const bool cadence=mode=="--freq-batch-cadence-profile";
    const bool emptyTips=mode=="--freq-empty-tip-pixels";
    if(!paced && !profile && !pixels && !awake && !emptyTips && !cadence) return -1;
    try {
        if(freq_paced::phaseProfileBuild && !profile && !cadence)
            throw std::runtime_error("instrumented binary accepts diagnostic profile modes only; use non-profile binary for CPU/pixels");
        if(!freq_paced::phaseProfileBuild && (profile || cadence))
            throw std::runtime_error("diagnostic profiles require compiler define KIRIN_FREQ_PHASE_PROFILE=1 in a separate build directory");
        if(argc!=4) throw std::runtime_error("usage: frequency mode NEW_OUTPUT_DIR SOURCE_ID");
        const auto path=juce::String::fromUTF8(argv[2]);
        if(!juce::File::isAbsolutePath(path)) throw std::runtime_error("output directory must be absolute");
        if(cadence) return freq_cadence_test::main(juce::File(path),juce::String::fromUTF8(argv[3]));
        if(emptyTips) return freq_empty_tip::main(juce::File(path),juce::String::fromUTF8(argv[3]));
        if(pixels) return freq_pixels::main(juce::File(path),juce::String::fromUTF8(argv[3]));
        if(awake) {
            // Scoped process activity only; no persistent settings, display or priority changes.
            const juce::ScopedLowPowerModeDisabler activity;
            return freq_paced::main(juce::File(path),juce::String::fromUTF8(argv[3]),true);
        }
        return freq_paced::main(juce::File(path),juce::String::fromUTF8(argv[3]));
    } catch(const std::exception& error) {
        std::cerr<<"FREQ_BENCHMARK_ERROR "<<error.what()<<std::endl; return 1;
    }
}
}
