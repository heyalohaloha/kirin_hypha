#include "FixedValidationDelay.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

using hypha::pdc_validation::FixedValidationDelay;
static void require (bool value) { if (! value) std::abort(); }

int main()
{
    constexpr int totalFrames = FixedValidationDelay::latencySamples + 257;
    std::array<std::vector<float>, 2> input {
        std::vector<float> (totalFrames), std::vector<float> (totalFrames)
    };
    for (int frame = 0; frame < totalFrames; ++frame)
    {
        input[0][static_cast<std::size_t> (frame)] = static_cast<float> (frame + 1);
        input[1][static_cast<std::size_t> (frame)] = static_cast<float> (-frame - 1);
    }
    const auto original = input;

    FixedValidationDelay delay;
    delay.prepare (2);
    constexpr std::array<int, 5> blockPattern { 17, 255, 64, 513, 31 };
    int position = 0, block = 0;
    while (position < totalFrames)
    {
        const auto frames = std::min (blockPattern[static_cast<std::size_t> (block
            % static_cast<int> (blockPattern.size()))], totalFrames - position);
        float* channels[] { input[0].data() + position, input[1].data() + position };
        require (delay.process (channels, 2, frames));
        position += frames;
        ++block;
    }
    for (int frame = 0; frame < FixedValidationDelay::latencySamples; ++frame)
        require (input[0][static_cast<std::size_t> (frame)] == 0.0f
                 && input[1][static_cast<std::size_t> (frame)] == 0.0f);
    for (int frame = FixedValidationDelay::latencySamples; frame < totalFrames; ++frame)
    {
        const auto source = static_cast<std::size_t> (frame - FixedValidationDelay::latencySamples);
        require (input[0][static_cast<std::size_t> (frame)] == original[0][source]
                 && input[1][static_cast<std::size_t> (frame)] == original[1][source]);
    }

    delay.reset();
    std::vector<float> zeroes (FixedValidationDelay::latencySamples, 0.0f);
    float* resetChannels[] { zeroes.data(), zeroes.data() };
    require (delay.process (resetChannels, 2, FixedValidationDelay::latencySamples));
    require (! delay.process (resetChannels, 1, 1));
    require (! delay.process (nullptr, 2, 1));

    delay.prepare (1);
    float impulse[] { 1.0f };
    float* mono[] { impulse };
    require (delay.process (mono, 1, 1) && impulse[0] == 0.0f);
    require (delay.process (mono, 1, 0));

    std::cout << "PDC validation delay: exact 4096-sample mono/stereo delay PASS\n";
}
