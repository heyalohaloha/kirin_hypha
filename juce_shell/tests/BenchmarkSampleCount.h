#pragma once
#include <cstdlib>
#include <iostream>

namespace hypha::test
{
// An explicit diagnostic may increase sampling precision; default CTests retain 1200 blocks.
// Numeric acceptance limits are not read here or changed. Parse before creating audio threads.
inline int benchmarkSampleCount()
{
    const auto* text = std::getenv ("KIRIN_BENCHMARK_BLOCKS");
    if (text == nullptr) return 1200;
    char* end = nullptr;
    const auto blocks = std::strtol (text, &end, 10);
    if (end == text || *end != '\0' || blocks < 1200 || blocks > 12000)
    {
        std::cerr << "Benchmark samples: explicit count must be 1200..12000\n";
        std::exit (EXIT_FAILURE);
    }
    return static_cast<int> (blocks);
}
}
