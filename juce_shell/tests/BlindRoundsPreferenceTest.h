#pragma once
#include "../src/HyphaBlindResultImage.h"
#include <stdexcept>

namespace hypha::tests
{
// The enclosing test owns and audits cleanup of this fixture root.
inline void runBlindRoundsPreferenceTests (const juce::File& root)
{
    const auto check = [] (bool pass, const char* why) { if (!pass) throw std::runtime_error (why); };
    const auto defaults = root.getChildFile ("blind-rounds-defaults.txt");
    check (!defaults.exists() && blind_image::preferenceRounds (defaults) == 1,
        "missing rounds defaults to one without writing");
    check (!defaults.exists(), "reading defaults never writes preferences");
    const auto legacy = root.getChildFile ("blind-rounds-legacy.txt");
    check (ui_preferences::write (legacy, "blind-guess", "on"), "legacy fixture");
    check (blind_image::preferenceRounds (legacy) == 5, "legacy guess on migrates by reading only");
    check (ui_preferences::read (legacy, "blind-rounds").isEmpty(), "legacy read adds no new key");
    for (const int rounds : {1,5,10}) {
        check (blind_image::preferRounds (legacy, rounds), "round preference persists");
        check (blind_image::preferenceRounds (legacy) == rounds
            && ui_preferences::read (legacy, "blind-rounds") == juce::String (rounds), "round trip exact persisted value");
        check (ui_preferences::read (legacy, "blind-guess") == "on", "legacy key is never overwritten");
    }
    const auto off = root.getChildFile ("blind-rounds-off.txt");
    check (ui_preferences::write (off, "blind-guess", "off"), "legacy off fixture");
    check (blind_image::preferenceRounds (off) == 1, "legacy off is the ordinary one-round listen");
    check (ui_preferences::write (off, "blind-rounds", "0"), "prior preference-off fixture");
    check (blind_image::preferenceRounds (off) == 1, "old zero rounds maps to one");
    check (ui_preferences::write (off, "blind-rounds", "invalid"), "malformed fixture");
    check (blind_image::preferenceRounds (off) == 1, "malformed rounds safely default to one");
    check (blind_image::preferRounds (off, -1) && ui_preferences::read (off, "blind-rounds") == "1",
        "writer only stores one five or ten");
    const auto blocked = root.getChildFile ("blind-rounds-directory");
    check (blocked.createDirectory().wasOk(), "unwritable-as-file fixture");
    check (!blind_image::preferRounds (blocked, 5), "failed persistence is returned for UI toast");
}
}
