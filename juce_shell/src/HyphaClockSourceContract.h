#pragma once

namespace hypha::clock_source_contract
{
    // Only the Audio Unit v2 wrapper supplies Kirin's AU transport-provenance marker.
    // The common Processor.cpp is compiled once for both AU and VST3, so the plugin format must
    // be selected at runtime before this AU-only marker is read.
    constexpr bool audioUnitV2UsesRenderTimeline (bool usesHostTransportTimeline) noexcept
    {
        return ! usesHostTransportTimeline;
    }
}
