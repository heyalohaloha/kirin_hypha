#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <optional>
#include <vector>
// Only linked into KirinAppearanceContractTests. Product classes/layouts are unchanged.
namespace hypha::appearance::test_write_guard
{
bool permitMutation (const juce::File& destination);
struct FixturePolicy
{
    juce::String allowedRoot;
    std::vector<juce::File> additionalProtectedRoots;
};
// Test-only policy injection can add protected roots; it never removes real protection.
class ScopedFixturePolicy
{
public:
    explicit ScopedFixturePolicy (FixturePolicy);
    ~ScopedFixturePolicy();
    ScopedFixturePolicy (const ScopedFixturePolicy&) = delete;
    ScopedFixturePolicy& operator= (const ScopedFixturePolicy&) = delete;
private:
    std::optional<FixturePolicy> previous;
};
std::uint64_t permittedMutationEntries();
}
