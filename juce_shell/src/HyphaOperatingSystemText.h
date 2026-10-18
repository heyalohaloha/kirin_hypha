#pragma once

#include <string>
#include <string_view>

// The operating system line of the information menu states only what the system reported.
// A macOS host built with an old SDK runs in a compatibility mode in which the system version
// file, NSProcessInfo and kern.osproductversion all report 10.16 inside that host process, so
// the version Hypha would show is the host's compatibility value, not the installed macOS.
// The kernel still reports the exact build (kern.osversion) there. When the reported version
// equals the compatibility value, Hypha shows that build and says the host withheld the
// version; it never maps the build or the Darwin release to a version it did not read. Only the build
// is shown then, in parentheses as beside a reported version.
namespace hypha::operating_system
{
struct MacFacts
{
    std::string productVersion;       // kern.osproductversion as this process sees it
    std::string compatibilityVersion; // kern.osproductversioncompat
    std::string build;                // kern.osversion, not redirected by compatibility mode
};

struct Description
{
    std::string line; // after the plugin format in the information menu
    std::string note; // empty unless the host withheld the installed version
};

inline bool hostWithholdsVersion (const MacFacts& facts) noexcept
{
    return facts.productVersion.empty()
        || (! facts.compatibilityVersion.empty()
            && facts.productVersion == facts.compatibilityVersion);
}

inline Description describe (const MacFacts& facts)
{
    if (! hostWithholdsVersion (facts))
        return { "macOS " + facts.productVersion
                     + (facts.build.empty() ? std::string {} : " (" + facts.build + ")"),
                 {} };
    const std::string note = facts.productVersion.empty()
        ? "This host does not report the macOS version"
        : "This host reports macOS " + facts.productVersion + " for compatibility";
    if (facts.build.empty())
        return { "macOS", note };
    const std::string name = "macOS";
    return { name + " (" + facts.build + ")", note };
}
}
