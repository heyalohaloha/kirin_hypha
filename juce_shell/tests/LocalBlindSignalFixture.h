#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// A deliberately narrow reader for the checked-in S-1 fixture, not a product audio decoder.
// Require the actual 48 kHz, stereo, extensible IEEE-float PCM instead of generating a substitute.
// DAWs may append valid RIFF metadata after the data chunk, so the container's total byte count is
// not part of the qualified audio fixture.
static std::vector<float> readFixture (const char* path)
{
    std::ifstream input (path, std::ios::binary);
    require (input.good(), "S-1 fixture is required");
    const std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (input)), {});
    require (bytes.size() >= 68, "S-1 RIFF header is truncated");
    const auto u32 = [&bytes] (std::size_t i)
    { return std::uint32_t (bytes[i]) | (std::uint32_t (bytes[i + 1]) << 8)
          | (std::uint32_t (bytes[i + 2]) << 16) | (std::uint32_t (bytes[i + 3]) << 24); };
    require (std::memcmp (bytes.data(), "RIFF", 4) == 0 && u32 (4) + 8 == bytes.size()
        && std::memcmp (bytes.data() + 8, "WAVEfmt ", 8) == 0 && u32 (16) == 40
        && bytes[20] == 0xfe && bytes[21] == 0xff && bytes[22] == 2 && bytes[23] == 0
        && u32 (24) == 48000 && bytes[32] == 8 && bytes[34] == 32 && u32 (40) == 3 && u32 (44) == 3
        && std::memcmp (bytes.data() + 60, "data", 4) == 0 && u32 (64) == 3840000
        && bytes.size() >= 68 + u32 (64),
        "S-1 must remain native stereo IEEE float");
    std::vector<float> mono (48000 * 4);
    for (std::size_t f = 0; f < mono.size(); ++f)
    {
        const auto left = u32 (68 + f * 8), right = u32 (72 + f * 8);
        require (left == right, "S-1 stereo channels must match for mono/stereo fixtures");
        std::memcpy (&mono[f], &left, sizeof (float));
        require (std::isfinite (mono[f]), "fixture must be finite");
    }
    return mono;
}
