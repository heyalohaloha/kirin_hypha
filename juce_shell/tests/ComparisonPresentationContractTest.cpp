#include "ComparisonPresentationContractTest.h"

#include "../src/HyphaComparisonPresentation.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests::comparison_presentation_contract
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Comparison presentation contract failed at line " << line
              << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}
}

#define KIRIN_COMPARISON_REQUIRE(expression) require ((expression), #expression, __LINE__)

void verify()
{
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH).contains ("MATCH PRE / POST BUS"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_PREPARING,
            KIRIN_COMPARISON_REASON_STALE).contains ("WAITING"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::statusText (
            KIRIN_COMPARISON_STATE_HOLDING,
            KIRIN_COMPARISON_REASON_STALE).contains ("HOLDING"));
    KIRIN_COMPARISON_REQUIRE (
        comparison_presentation::notifiesExplicitAction (
            KIRIN_COMPARISON_STATE_REJECTED,
            KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN));
    KIRIN_COMPARISON_REQUIRE (
        ! comparison_presentation::notifiesExplicitAction (
            KIRIN_COMPARISON_STATE_HOLDING,
            KIRIN_COMPARISON_REASON_STALE));
}
}
