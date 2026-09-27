#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "HyphaSpectrumPresentation.h"
#include "kirin_hypha_ffi.h"

namespace hypha::spectrum_delta
{
using Bins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;
using Validity = std::array<uint8_t, KIRIN_SPECTRUM_BAND_COUNT>;

struct Selection
{
    Bins values {};
    Validity valid {};
    bool anyValid = false;
};

inline Selection select (const KirinSpectrumView& view, bool shape,
                         const Bins& calmWeights) noexcept
{
    Selection result;
    if (view.has_data == 0 || view.status != KIRIN_SPECTRUM_ACTIVE)
        return result;
    if (! shape)
    {
        result.values = spectrum_presentation::calmLowFrequencies (
            view.display_db, calmWeights);
        result.valid.fill (1u);
        result.anyValid = true;
        return result;
    }
    if (view.shape_has_energy == 0 || ! std::isfinite (view.shape_energy_delta_db))
        return result;
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        if (view.shape_valid[index] == 0 || ! std::isfinite (view.shape_db[index]))
            continue;
        result.values[index] = view.shape_db[index];
        result.valid[index] = 1u;
        result.anyValid = true;
    }
    return result;
}
}
