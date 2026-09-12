#pragma once

#include "kirin_hypha_ffi.h"

namespace hypha::analysis
{
template <auto SpectrumVisible,
          auto AttackEnabled,
          auto ChannelMode,
          auto MidSideVisible,
          auto AbsoluteVisible,
          auto PerceptualVisible>
struct BasicFfiAdapter
{
    KirinHypha* handle = nullptr;

    bool setSpectrumVisible (bool value) const
    { return SpectrumVisible (handle, value); }

    bool setAttackEnabled (bool value) const
    { return AttackEnabled (handle, value); }

    bool setChannelMode (std::uint8_t value) const
    { return ChannelMode (handle, value); }

    bool setMidSideSpectrumVisible (bool value) const
    { return MidSideVisible (handle, value); }

    bool setAbsoluteVisible (bool value) const
    { return AbsoluteVisible (handle, value); }

    bool setPerceptualVisible (bool value) const
    { return PerceptualVisible (handle, value); }
};

using ShippingFfiAdapter = BasicFfiAdapter<
    &kirin_hypha_set_spectrum_visible,
    &kirin_hypha_set_attack_enabled,
    &kirin_hypha_set_spectrum_channel_mode,
    &kirin_hypha_set_mid_side_spectrum_visible,
    &kirin_hypha_set_absolute_visible,
    &kirin_hypha_set_perceptual_visible>;
}
