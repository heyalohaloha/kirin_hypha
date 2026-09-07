#pragma once

#include <cstdint>

namespace hypha::reference_audition
{
    struct ReferenceSessionIdentity
    {
        std::uint64_t sequence = 0;
        std::uint64_t auditionEpoch = 0;
        std::uint64_t outputGateToken = 0;

        bool valid() const noexcept
        {
            return sequence != 0 && auditionEpoch != 0;
        }
    };

    struct ReferenceSessionRetirement
    {
        std::uint64_t sequence = 0;
        std::uint64_t outputGateToken = 0;

        bool valid() const noexcept { return sequence != 0; }
    };
}
