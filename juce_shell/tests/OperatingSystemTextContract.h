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
}
