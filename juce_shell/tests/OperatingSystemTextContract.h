#pragma once

#include "../src/HyphaOperatingSystemText.h"

#include <cassert>

// The information menu states the reported OS, never the compatibility value as the installed
// macOS (a host built with an old SDK sees 10.16 from every version source inside its process).
inline void verifyOperatingSystemTextContract()
{
    namespace os = hypha::operating_system;
    const auto normal = os::describe ({ "15.7.7", "10.16", "24G720" });
    assert (normal.line == "macOS 15.7.7 (24G720)");
    assert (normal.note.empty());

    const os::MacFacts compatibilityHost { "10.16", "10.16", "24G720" };
    assert (os::hostWithholdsVersion (compatibilityHost));
    const auto compatibility = os::describe (compatibilityHost);
    assert (compatibility.line == "macOS (24G720)");
    assert (compatibility.line.find ("10.16") == std::string::npos);
    assert (compatibility.note == "This host reports macOS 10.16 for compatibility");

    const auto noBuild = os::describe ({ "10.16", "10.16", "" });
    assert (noBuild.line == "macOS" && ! noBuild.note.empty());
    const auto unread = os::describe ({ "", "", "24G720" });
    assert (unread.line == "macOS (24G720)");
    assert (unread.note == "This host does not report the macOS version");
    // A system without the compatibility key still reports its own version as read.
    assert (os::describe ({ "10.15.7", "", "19H2" }).line == "macOS 10.15.7 (19H2)");
    // macOS 26 gives hosts built before it 16.0, and still 10.16 to hosts built before macOS 11; neither is
    // shown as the installed macOS, whichever value the compatibility key names.
    assert (os::describe ({ "26.1", "16.0", "25B78" }).line == "macOS 26.1 (25B78)");
    for (const auto* reported : { "16.0", "10.16" })
    {
        const auto tahoe = os::describe ({ reported, "16.0", "25B78" });
        assert (tahoe.line == "macOS (25B78)" && tahoe.line.find (reported) == std::string::npos);
        assert (tahoe.note == std::string ("This host reports macOS ") + reported + " for compatibility");
    }
}
