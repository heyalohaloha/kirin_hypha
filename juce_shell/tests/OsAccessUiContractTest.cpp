#include "OsAccessUiContractTest.h"

#include "../src/HyphaObservatoryView.h"
#include "../src/PostControls.h"

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
    observatory.setReferenceEnabled (false);
    KIRIN_OS_ACCESS_REQUIRE (! observatory.isReferenceEnabled());
    observatory.setReferenceEnabled (true);
    KIRIN_OS_ACCESS_REQUIRE (observatory.isReferenceEnabled());
    observatory.setSize (300, 200);
    auto* note = observatory.findChildWithID ("observatory-note");
    KIRIN_OS_ACCESS_REQUIRE (note != nullptr && note->isVisible());
    observatory.setNoteAvailability (false, false);
    KIRIN_OS_ACCESS_REQUIRE (! note->isEnabled());
    observatory.setNoteAvailability (true, false);
    KIRIN_OS_ACCESS_REQUIRE (! note->isEnabled());
    observatory.setNoteAvailability (true, true);
    KIRIN_OS_ACCESS_REQUIRE (note->isEnabled());

    PostControls controls;
    controls.setSize (300, 28);
    auto* keep = controls.findChildWithID ("post-keep");
    auto* stop = controls.findChildWithID ("post-stop");
    auto* osInfo = controls.findChildWithID ("post-os-info");
    KIRIN_OS_ACCESS_REQUIRE (keep != nullptr && stop != nullptr && osInfo != nullptr);

    controls.update (false, 2, true);
    KIRIN_OS_ACCESS_REQUIRE (keep->isVisible() && ! keep->isEnabled());
    KIRIN_OS_ACCESS_REQUIRE (osInfo->isVisible() && ! stop->isVisible());
    controls.update (false, 0, false);
    KIRIN_OS_ACCESS_REQUIRE (keep->isVisible() && ! keep->isEnabled());
    KIRIN_OS_ACCESS_REQUIRE (! osInfo->isVisible());
    controls.update (false, 0, true);
    KIRIN_OS_ACCESS_REQUIRE (keep->isEnabled());
    controls.update (true, 1, true);
    KIRIN_OS_ACCESS_REQUIRE (! keep->isVisible() && ! osInfo->isVisible());
    KIRIN_OS_ACCESS_REQUIRE (stop->isVisible());
}
}
