#pragma once

#include <array>
#include <cstddef>

#include "HyphaAnalysisNavigation.h"
#include "HyphaObservatoryContract.h"
#include "HyphaTypographyContract.h"

namespace hypha::surface_presentation
{
enum class Id
{
    level,
    history,
    runSummary,
    attack,
    sharp,
    live,
    spectrum,
    psb,
    space,
    reference,
    referenceAccess,
    localBlind,
    hybridVu,
    capture,
    count,
};

struct Descriptor
{
    Id id = Id::level;
    const char* label = "";
    typography::Composition composition = typography::Composition::shell;
};

constexpr std::array descriptors {
    Descriptor { Id::level, "LEVEL", typography::Composition::facts },
    Descriptor { Id::history, "HISTORY", typography::Composition::visualization },
    Descriptor { Id::runSummary, "RUN", typography::Composition::visualization },
    Descriptor { Id::attack, "DRUM", typography::Composition::visualization },
    Descriptor { Id::sharp, "SHARP", typography::Composition::visualization },
    Descriptor { Id::live, "LIVE", typography::Composition::facts },
    Descriptor { Id::spectrum, "FREQ", typography::Composition::visualization },
    Descriptor { Id::psb, "PSB", typography::Composition::visualization },
    Descriptor { Id::space, "SPACE", typography::Composition::visualization },
    Descriptor { Id::reference, "REF", typography::Composition::information },
    Descriptor { Id::referenceAccess, "REFERENCE ACCESS", typography::Composition::information },
    Descriptor { Id::localBlind, "PRE / POST BLIND", typography::Composition::information },
    Descriptor { Id::hybridVu, "VU", typography::Composition::instrument },
    Descriptor { Id::capture, "CAPTURE", typography::Composition::visualization },
};

constexpr const Descriptor& descriptor (Id id) noexcept
{
    return descriptors[static_cast<std::size_t> (id)];
}

constexpr Id forTimePage (analysis_navigation::Page page) noexcept
{
    using analysis_navigation::Page;
    switch (page)
    {
        case Page::meters: return Id::history;
        case Page::run: return Id::runSummary;
        case Page::attack: return Id::attack;
        case Page::perceptual: return Id::sharp;
        case Page::absolute: return Id::live;
        case Page::spectrum: return Id::spectrum;
    }
    return Id::history;
}

constexpr Id forDomain (observatory::Domain domain,
                        analysis_navigation::Page timePage = analysis_navigation::Page::meters) noexcept
{
    using observatory::Domain;
    switch (domain)
    {
        case Domain::level: return Id::level;
        case Domain::time: return forTimePage (timePage);
        case Domain::frequency: return Id::spectrum;
        case Domain::space: return Id::space;
        case Domain::reference: return Id::reference;
    }
    return Id::level;
}

static_assert (descriptors.size() == static_cast<std::size_t> (Id::count));
static_assert (descriptor (forTimePage (analysis_navigation::Page::attack)).id == Id::attack);
static_assert (descriptor (forDomain (observatory::Domain::reference)).composition
               == typography::Composition::information);
}
