#include "../src/reference_audition/ReferenceContentCorrelation.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace ref = hypha::reference_audition;
void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}
int main()
{
    for (const auto rate : { 8000, 44100, 48000, 96000 })
    {
        const auto count = static_cast<size_t> (rate * 2);
        std::vector<float> a (count), b (count), unrelated (count);
        std::uint32_t random = 913;
        const auto sample = [&random] {
            random = random * 1664525u + 1013904223u;
            return static_cast<float> ((random >> 8) / 16777216.0 - 0.5);
        };
        for (size_t index = 0; index < count; ++index) { a[index] = sample(); unrelated[index] = sample(); }
        for (const auto offset : { -317, 0, 317 })
        {
            for (size_t index = 0; index < count; ++index)
            {
                const auto source = static_cast<std::int64_t> (index) - offset;
                b[index] = source >= 0 && source < static_cast<std::int64_t> (count)
                    ? std::tanh (a[static_cast<size_t> (source)] * 1.2f) * -0.5f : 0.0f;
            }
            const auto match = ref::correlateReferenceContent (a, b, rate, 1000);
            require (match.accepted && match.offsetSamples == offset, "signed offset, gain, polarity and nonlinear variant must align");
        }
        require (!ref::correlateReferenceContent (a, unrelated, rate, 1000).accepted, "unrelated content must be rejected");
        b.assign (count, 0.0f);
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "silence must be rejected");
        for (size_t index = 0; index < count; ++index)
            a[index] = b[index] = static_cast<float> (0.1 * std::sin (index * 6.283185307179586 * 997.0 / rate));
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "periodic tone cannot establish unique position");
        b[13] = std::numeric_limits<float>::quiet_NaN();
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "nonfinite audio must be rejected");
    }
    std::cout << "content correlation: signed offsets, nonlinear gain/polarity, unrelated, silence, periodic and nonfinite cases pass at four rates\n";
}
