#include "OsAccessUiContractTest.h"

#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "OS access UI contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_OS_ACCESS_REQUIRE(expression) require ((expression), #expression, __LINE__)
}

void verifyOsAccessUiContract()
{
    observatory::View observatory (observatory::Role::post);
    observatory.setReferenceOwned (false);
    KIRIN_OS_ACCESS_REQUIRE (! observatory.isReferenceOwned());
    auto* reference = observatory.findChildWithID ("observatory-reference");
    KIRIN_OS_ACCESS_REQUIRE (reference != nullptr && reference->isEnabled());
    observatory.setReferenceOwned (true);
    KIRIN_OS_ACCESS_REQUIRE (observatory.isReferenceOwned());
    observatory.setSize (300, 200);
    auto* note = observatory.findChildWithID ("observatory-note");
    auto* menu = observatory.findChildWithID ("observatory-menu");
    auto* stop = observatory.findChildWithID ("observatory-stop");
    KIRIN_OS_ACCESS_REQUIRE (note != nullptr && ! note->isVisible());
    KIRIN_OS_ACCESS_REQUIRE (menu != nullptr && menu->isVisible());
    KIRIN_OS_ACCESS_REQUIRE (stop != nullptr && ! stop->isVisible());
    observatory.setSize (600, 400);
    observatory.setNoteAvailability (false, false);
    KIRIN_OS_ACCESS_REQUIRE (! note->isEnabled() && ! note->isVisible());
    observatory.setNoteAvailability (true, false);
    KIRIN_OS_ACCESS_REQUIRE (! note->isEnabled() && ! note->isVisible());
    observatory.setNoteAvailability (true, true);
    KIRIN_OS_ACCESS_REQUIRE (note->isEnabled() && note->isVisible());
    observatory.setKeepActive (true);
    KIRIN_OS_ACCESS_REQUIRE (stop->isVisible());
    observatory.setKeepActive (false);
    KIRIN_OS_ACCESS_REQUIRE (! stop->isVisible());
}
}
