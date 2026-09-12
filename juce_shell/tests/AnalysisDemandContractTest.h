#pragma once

#include "../src/HyphaAnalysisDemand.h"
#include "../src/HyphaAnalysisFfiAdapter.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace hypha::tests::analysis_demand_contract
{
using Kind = analysis::Kind;

struct RecordingAdapter
{
    enum class Route { none, spectrum, midSide, absolute, perceptual, attack };
    int enabledCalls = 0;
    int spectrumStops = 0;
    int attackStops = 0;
    int channelMode = -1;
    Route enabledRoute = Route::none;
    bool acceptChannel = true;
    bool acceptEnable = true;

    bool setSpectrumVisible (bool value)
    {
        if (value) { ++enabledCalls; enabledRoute = Route::spectrum; }
        else ++spectrumStops;
        return ! value || acceptEnable;
    }
    bool setAttackEnabled (bool value)
    {
        if (value) { ++enabledCalls; enabledRoute = Route::attack; }
        else ++attackStops;
        return ! value || acceptEnable;
    }
    bool setChannelMode (std::uint8_t value)
    { channelMode = value; return acceptChannel; }
    bool setMidSideSpectrumVisible (bool)
    { ++enabledCalls; enabledRoute = Route::midSide; return acceptEnable; }
    bool setAbsoluteVisible (bool)
    { ++enabledCalls; enabledRoute = Route::absolute; return acceptEnable; }
    bool setPerceptualVisible (bool)
    { ++enabledCalls; enabledRoute = Route::perceptual; return acceptEnable; }
};

constexpr RecordingAdapter::Route expectedRoute (Kind kind) noexcept
{
    using Route = RecordingAdapter::Route;
    switch (kind)
    {
        case Kind::spectrum: return Route::spectrum;
        case Kind::midSideSpectrum: return Route::midSide;
        case Kind::psbAbsolute:
        case Kind::sharpAbsolute:
        case Kind::liveAbsolute: return Route::absolute;
        case Kind::psbDelta:
        case Kind::sharpDelta: return Route::perceptual;
        case Kind::attack: return Route::attack;
        case Kind::none: return Route::none;
    }
    return Route::none;
}

struct CAbiRecorder
{
    RecordingAdapter::Route route = RecordingAdapter::Route::none;
    int channelMode = -1;
};

inline CAbiRecorder& cAbiRecorder (KirinHypha* handle)
{
    return *reinterpret_cast<CAbiRecorder*> (handle);
}

inline bool recordSpectrum (KirinHypha* handle, bool)
{ cAbiRecorder (handle).route = RecordingAdapter::Route::spectrum; return true; }
inline bool recordAttack (KirinHypha* handle, bool)
{ cAbiRecorder (handle).route = RecordingAdapter::Route::attack; return true; }
inline bool recordChannel (KirinHypha* handle, std::uint8_t value)
{ cAbiRecorder (handle).channelMode = value; return true; }
inline bool recordMidSide (KirinHypha* handle, bool)
{ cAbiRecorder (handle).route = RecordingAdapter::Route::midSide; return true; }
inline bool recordAbsolute (KirinHypha* handle, bool)
{ cAbiRecorder (handle).route = RecordingAdapter::Route::absolute; return true; }
inline bool recordPerceptual (KirinHypha* handle, bool)
{ cAbiRecorder (handle).route = RecordingAdapter::Route::perceptual; return true; }

using RecordingFfiAdapter = analysis::BasicFfiAdapter<
    &recordSpectrum, &recordAttack, &recordChannel, &recordMidSide,
    &recordAbsolute, &recordPerceptual>;

using ExpectedShippingFfiAdapter = analysis::BasicFfiAdapter<
    &kirin_hypha_set_spectrum_visible,
    &kirin_hypha_set_attack_enabled,
    &kirin_hypha_set_spectrum_channel_mode,
    &kirin_hypha_set_mid_side_spectrum_visible,
    &kirin_hypha_set_absolute_visible,
    &kirin_hypha_set_perceptual_visible>;
static_assert (std::is_same_v<analysis::ShippingFfiAdapter,
                              ExpectedShippingFfiAdapter>);

inline void require (bool value)
{
    if (! value)
    {
        std::cerr << "Analysis demand contract failed\n";
        std::exit (EXIT_FAILURE);
    }
}

inline void verify()
{
    using analysis::Demand;
    using analysis::Kind;
    constexpr std::array demands {
        Demand { Kind::none, 0u },
        Demand { Kind::spectrum, 0u },
        Demand { Kind::spectrum, 1u },
        Demand { Kind::spectrum, 2u },
        Demand { Kind::midSideSpectrum, 0u },
        Demand { Kind::psbAbsolute, 0u },
        Demand { Kind::psbDelta, 0u },
        Demand { Kind::sharpAbsolute, 0u },
        Demand { Kind::sharpAbsolute, 1u },
        Demand { Kind::sharpAbsolute, 2u },
        Demand { Kind::sharpDelta, 0u },
        Demand { Kind::sharpDelta, 1u },
        Demand { Kind::sharpDelta, 2u },
        Demand { Kind::liveAbsolute, 0u },
        Demand { Kind::attack, 0u },
    };
    for (std::size_t index = 0; index < demands.size(); ++index)
    {
        require (analysis::valid (demands[index]));
        require (analysis::decode (analysis::encode (demands[index])) == demands[index]);
        for (std::size_t other = index + 1; other < demands.size(); ++other)
            require (analysis::encode (demands[index]) != analysis::encode (demands[other]));
    }
    require (! analysis::valid ({ Kind::none, 1u }));
    require (! analysis::valid ({ Kind::midSideSpectrum, 1u }));
    require (! analysis::valid ({ Kind::liveAbsolute, 1u }));
    require (! analysis::valid ({ Kind::attack, 1u }));
    require (! analysis::valid ({ Kind::spectrum, 3u }));
    require (! analysis::valid ({ static_cast<Kind> (255u), 0u }));

    for (const auto demand : demands)
    {
        if (! analysis::active (demand)) continue;
        RecordingAdapter adapter;
        require (analysis::apply (Demand {}, demand, adapter));
        require (adapter.enabledCalls == 1);
        require (adapter.enabledRoute == expectedRoute (demand.kind));
        if (analysis::usesChannelMode (demand.kind)
            || demand.kind == Kind::psbAbsolute || demand.kind == Kind::psbDelta)
            require (adapter.channelMode == static_cast<int> (demand.channelMode));
    }
    RecordingAdapter spectrumStop;
    require (analysis::apply ({ Kind::psbAbsolute, 0u }, {}, spectrumStop));
    require (spectrumStop.spectrumStops == 1 && spectrumStop.attackStops == 0);
    RecordingAdapter attackStop;
    require (analysis::apply ({ Kind::attack, 0u }, {}, attackStop));
    require (attackStop.spectrumStops == 0 && attackStop.attackStops == 1);
    RecordingAdapter unchanged;
    require (analysis::apply ({ Kind::spectrum, 2u }, { Kind::spectrum, 2u }, unchanged));
    require (unchanged.enabledCalls == 0 && unchanged.spectrumStops == 0
             && unchanged.attackStops == 0 && unchanged.channelMode == -1);

    // Every active-to-active transition must route to the requested analysis kind. The expected
    // table above is independent from production apply(), so a swapped FFI call fails this test.
    for (const auto previous : demands)
        for (const auto requested : demands)
        {
            if (! analysis::active (requested) || previous == requested) continue;
            RecordingAdapter adapter;
            require (analysis::apply (previous, requested, adapter));
            require (adapter.enabledCalls == 1);
            require (adapter.enabledRoute == expectedRoute (requested.kind));
        }
    RecordingAdapter rejectedChannel;
    rejectedChannel.acceptChannel = false;
    require (! analysis::apply ({}, { Kind::spectrum, 2u }, rejectedChannel));
    require (rejectedChannel.enabledCalls == 0);
    RecordingAdapter rejectedEnable;
    rejectedEnable.acceptEnable = false;
    require (! analysis::apply ({}, { Kind::midSideSpectrum, 0u }, rejectedEnable));

    CAbiRecorder cAbi;
    RecordingFfiAdapter ffiAdapter { reinterpret_cast<KirinHypha*> (&cAbi) };
    require (ffiAdapter.setSpectrumVisible (true)
             && cAbi.route == RecordingAdapter::Route::spectrum);
    require (ffiAdapter.setChannelMode (2u) && cAbi.channelMode == 2);
    require (ffiAdapter.setMidSideSpectrumVisible (true)
             && cAbi.route == RecordingAdapter::Route::midSide);
    require (ffiAdapter.setAbsoluteVisible (true)
             && cAbi.route == RecordingAdapter::Route::absolute);
    require (ffiAdapter.setPerceptualVisible (true)
             && cAbi.route == RecordingAdapter::Route::perceptual);
    require (ffiAdapter.setAttackEnabled (true)
             && cAbi.route == RecordingAdapter::Route::attack);

    analysis::ApplicationState application;
    application.engineCreated();
    require (application.generation() == 1 && ! application.isReady());
    require (! application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    application.engineReady();
    require (application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    application.applicationSucceeded ({ Kind::midSideSpectrum, 0u });
    require (application.isApplied ({ Kind::midSideSpectrum, 0u }));
    require (! application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    application.engineDestroyed();
    application.engineCreated();
    require (application.generation() == 2 && ! application.isReady());
    application.engineReady();
    require (application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    application.applicationFailed();
    require (! application.isApplied ({ Kind::midSideSpectrum, 0u }));
    for (int skipped = 0; skipped < 3; ++skipped)
        require (! application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    require (application.shouldApply ({ Kind::midSideSpectrum, 0u }));
    application.requestChanged();
    require (application.shouldApply ({ Kind::liveAbsolute, 0u }));

    using Page = analysis_navigation::Page;
    analysis::SurfaceState surface { true, Page::spectrum, false, true, false, false, 2u };
    require (analysis::forSurface (surface) == Demand { Kind::spectrum, 2u });
    surface.midSideSpectrum = true;
    require (analysis::forSurface (surface) == Demand { Kind::midSideSpectrum, 0u });
    surface.psb = true;
    require (analysis::forSurface (surface) == Demand { Kind::psbAbsolute, 0u });
    surface.absoluteTarget = false;
    require (analysis::forSurface (surface) == Demand { Kind::psbDelta, 0u });
    surface = { true, Page::perceptual, false, true, false, true, 1u };
    require (analysis::forSurface (surface) == Demand { Kind::sharpAbsolute, 1u });
    surface.sharpAbsolute = false;
    require (analysis::forSurface (surface) == Demand { Kind::sharpDelta, 1u });
    surface.presented = false;
    require (analysis::forSurface (surface) == Demand {});
    surface.sharpAbsolute = true; // hidden Pair changes cannot reacquire an Analysis slot
    require (analysis::forSurface (surface) == Demand {});
    require (analysis::forSurface ({ true, Page::absolute, false, true, false, false, 0u })
             == Demand { Kind::liveAbsolute, 0u });
    require (analysis::forSurface ({ true, Page::attack, false, true, false, false, 0u })
             == Demand { Kind::attack, 0u });
    require (analysis::forSurface (
        { true, Page::attack, false, true, false, false, 0u, false }) == Demand {});

    analysis::OwnerState owners;
    const auto firstOwner = owners.begin();
    require (owners.isCurrent (firstOwner));
    require (owners.set (firstOwner, { Kind::spectrum, 1u }));
    require (owners.requested() == Demand { Kind::spectrum, 1u });
    const auto replacementOwner = owners.begin();
    require (replacementOwner != firstOwner && owners.requested() == Demand {});
    require (! owners.set (firstOwner, { Kind::attack, 0u }));
    require (! owners.end (firstOwner));
    require (owners.set (replacementOwner, { Kind::attack, 0u }));
    require (owners.end (replacementOwner));
    require (owners.currentOwner() == 0 && owners.requested() == Demand {});
    std::cout << "Analysis demand: PASS (routes, transitions, failures, engine generations, owners)\n";
}
}
