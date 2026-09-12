#pragma once

#include "../src/HyphaAnalysisDemand.h"
#include <array>
#include <cstdlib>
#include <iostream>

namespace hypha::tests::analysis_demand_contract
{
using Kind = analysis::Kind;

struct RecordingAdapter
{
    int enabledCalls = 0;
    int spectrumStops = 0;
    int attackStops = 0;
    int channelMode = -1;
    Kind enabledKind = Kind::none;

    bool setSpectrumVisible (bool value)
    {
        if (value) { ++enabledCalls; enabledKind = Kind::spectrum; }
        else ++spectrumStops;
        return true;
    }
    bool setAttackEnabled (bool value)
    {
        if (value) { ++enabledCalls; enabledKind = Kind::attack; }
        else ++attackStops;
        return true;
    }
    bool setChannelMode (std::uint8_t value)
    { channelMode = value; return true; }
    bool setMidSideSpectrumVisible (bool)
    { ++enabledCalls; enabledKind = Kind::midSideSpectrum; return true; }
    bool setAbsoluteVisible (bool)
    { ++enabledCalls; enabledKind = Kind::liveAbsolute; return true; }
    bool setPerceptualVisible (bool)
    { ++enabledCalls; enabledKind = Kind::sharpDelta; return true; }
};

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
    std::cout << "Analysis demand: PASS (15 exclusive states, owner replacement, stale rejection)\n";
}
}
