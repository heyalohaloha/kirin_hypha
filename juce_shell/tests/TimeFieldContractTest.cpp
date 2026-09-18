#include "TimeFieldContractTest.h"

#include "../src/HyphaTimeFieldImage.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace hypha::tests
{
namespace
{
void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Time field contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_TIME_FIELD_REQUIRE(expression) require ((expression), #expression, __LINE__)

constexpr int kRows = 180;
constexpr int kColumns = 8;
constexpr double kSpanSeconds = 6.0;

// One value per column, so a built row is unmistakable and its column is checkable.
juce::Image buildFrom (const std::vector<double>& agesOldestFirst)
{
    return time_field::build (
        { kRows, kColumns, kSpanSeconds },
        agesOldestFirst.size(),
        [&agesOldestFirst] (size_t index) { return agesOldestFirst[index]; },
        [] (size_t, int column) { return column == 3 ? 1.0f : 0.0f; },
        juce::Colours::white,
        [] (float value) -> uint8_t { return value > 0.5f ? 255u : 0u; });
}

// Ages for observations spaced `intervalSeconds` apart, oldest first, newest at age 0.
std::vector<double> evenCadence (double intervalSeconds, size_t count)
{
    std::vector<double> ages;
    for (size_t index = 0u; index < count; ++index)
        ages.push_back ((double) (count - 1u - index) * intervalSeconds);
    return ages;
}

std::vector<bool> rowsWithInk (const juce::Image& image)
{
    std::vector<bool> inked;
    for (int y = 0; y < image.getHeight(); ++y)
    {
        bool any = false;
        for (int x = 0; x < image.getWidth() && ! any; ++x)
            any = image.getPixelAt (x, y).getAlpha() > 0;
        inked.push_back (any);
    }
    return inked;
}

int longestEmptyRunFromFirstInk (const std::vector<bool>& inked)
{
    const auto first = std::find (inked.begin(), inked.end(), true);
    int longest = 0;
    int run = 0;
    for (auto row = first; row != inked.end(); ++row)
    {
        run = *row ? 0 : run + 1;
        longest = std::max (longest, run);
    }
    return longest;
}

// An observation's age decides its height: newest along the bottom edge, oldest along the top.
void ageDecidesTheRow()
{
    const auto image = buildFrom (evenCadence (kSpanSeconds / (double) kRows, (size_t) kRows));
    const auto inked = rowsWithInk (image);
    KIRIN_TIME_FIELD_REQUIRE ((int) inked.size() == kRows);
    for (const auto row : inked)
        KIRIN_TIME_FIELD_REQUIRE (row);

    // Only the column the value was given for carries ink.
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kColumns; ++x)
            KIRIN_TIME_FIELD_REQUIRE ((image.getPixelAt (x, y).getAlpha() > 0) == (x == 3));

    // A single observation at age zero occupies the bottom row and nothing above it.
    const auto single = buildFrom ({ 0.0 });
    KIRIN_TIME_FIELD_REQUIRE (single.isValid());
    KIRIN_TIME_FIELD_REQUIRE (single.getPixelAt (3, kRows - 1).getAlpha() > 0);
    KIRIN_TIME_FIELD_REQUIRE (single.getPixelAt (3, 0).getAlpha() == 0);
}

// The source publishes on its own cadence, not one observation per row. The field stays
// continuous at every cadence a host can produce.
void anyCadenceStaysContinuous()
{
    for (const auto intervalSeconds : { 0.032, 0.0333, 0.0427, 0.0533, 0.0667, 0.0853, 0.1707 })
    {
        const auto count = (size_t) (kSpanSeconds / intervalSeconds) + 1u;
        const auto inked = rowsWithInk (buildFrom (evenCadence (intervalSeconds, count)));
        KIRIN_TIME_FIELD_REQUIRE (longestEmptyRunFromFirstInk (inked) <= 1);
    }
}

// A real break is not filled in. Half a second of missing observations stays empty.
void arealBreakStaysEmpty()
{
    const double interval = kSpanSeconds / (double) kRows;
    std::vector<double> ages;
    for (const auto age : evenCadence (interval, (size_t) kRows))
        if (! (age >= 0.5 && age <= 1.0))
            ages.push_back (age);

    const auto inked = rowsWithInk (buildFrom (ages));
    const auto longest = longestEmptyRunFromFirstInk (inked);
    const int expected = (int) (0.5 / kSpanSeconds * (double) kRows);
    KIRIN_TIME_FIELD_REQUIRE (longest >= expected - 2);
    KIRIN_TIME_FIELD_REQUIRE (longest <= expected + 2);
}

// Two lone observations are not a cadence and must not be spread over the whole span.
void twoObservationsDoNotBecomeACadence()
{
    const auto inked = rowsWithInk (buildFrom ({ 5.0, 0.0 }));
    int filled = 0;
    for (const auto row : inked)
        filled += row ? 1 : 0;
    KIRIN_TIME_FIELD_REQUIRE (filled < kRows / 3);
}

// Nothing to draw returns an invalid image, so the caller skips the blit instead of covering the
// plot with a transparent rectangle.
void nothingToDrawReturnsNoImage()
{
    KIRIN_TIME_FIELD_REQUIRE (! buildFrom ({}).isValid());
    // Every observation outside the window.
    KIRIN_TIME_FIELD_REQUIRE (! buildFrom ({ 40.0, 30.0, 20.0 }).isValid());
    // Observations inside the window whose values all map to nothing.
    const auto blank = time_field::build (
        { kRows, kColumns, kSpanSeconds }, 2u,
        [] (size_t index) { return index == 0u ? 1.0 : 0.0; },
        [] (size_t, int) { return 0.0f; },
        juce::Colours::white,
        [] (float) -> uint8_t { return 0u; });
    KIRIN_TIME_FIELD_REQUIRE (! blank.isValid());
    // A degenerate geometry is refused rather than allocating.
    KIRIN_TIME_FIELD_REQUIRE (! time_field::build (
        { 0, kColumns, kSpanSeconds }, 1u, [] (size_t) { return 0.0; },
        [] (size_t, int) { return 1.0f; }, juce::Colours::white,
        [] (float) -> uint8_t { return 255u; }).isValid());
}
}

void verifyTimeFieldContract()
{
    ageDecidesTheRow();
    anyCadenceStaysContinuous();
    arealBreakStaysEmpty();
    twoObservationsDoNotBecomeACadence();
    nothingToDrawReturnsNoImage();
    std::cout << "Time field: PASS (age to row, seven cadences, real break, two observations)\n";
}
}
