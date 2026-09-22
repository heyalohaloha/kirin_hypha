#pragma once

#include "../src/PreparedFormat.h"
#include "../src/ChannelRoles.h"

#include <cstdlib>
#include <iostream>
#include <vector>

/**
    Format binding: reuse, rebuild, or hold (P-3 gate).

    Role codes are written as literals here. Deriving them from `kirin::channelRoles` would test
    that the mapping agrees with itself; these numbers come from `kirin_hypha_channels.h`
    (試験規律 §9.1).
*/
namespace hypha::tests::prepared_format_contract
{
constexpr uint8_t kC = 0, kL = 1, kR = 2, kLfe = 3, kLs = 4, kRs = 5;
constexpr uint8_t kLss = 6, kRss = 7, kLsr = 8, kRsr = 9;
constexpr uint8_t kTfl = 10, kTfr = 11, kTrl = 12, kTrr = 13;

inline void require (bool condition, const char* what, int line)
{
    if (condition)
        return;
    std::cerr << "Prepared format contract failed at line " << line << ": " << what << '\n';
    std::exit (EXIT_FAILURE);
}
#define KIRIN_PF_REQUIRE(expr) require ((expr), #expr, __LINE__)

inline void verify()
{
    using kirin::PrepareAction;
    const std::vector<uint8_t> stereo { kL, kR };
    const std::vector<uint8_t> fiveOne { kL, kR, kC, kLfe, kLs, kRs };
    // Both ten channels. Only the roles tell them apart.
    const std::vector<uint8_t> sevenOneTwo { kL, kR, kC, kLfe, kLss, kRss, kLsr, kRsr, kTfl, kTfr };
    const std::vector<uint8_t> fiveOneFour { kL, kR, kC, kLfe, kLs, kRs, kTfl, kTfr, kTrl, kTrr };
    // Both eight channels.
    const std::vector<uint8_t> sevenOne { kL, kR, kC, kLfe, kLss, kRss, kLsr, kRsr };
    const std::vector<uint8_t> fiveOneTwo { kL, kR, kC, kLfe, kLs, kRs, kTfl, kTfr };

    KIRIN_PF_REQUIRE (sevenOneTwo.size() == fiveOneFour.size());
    KIRIN_PF_REQUIRE (sevenOne.size() == fiveOneTwo.size());
    KIRIN_PF_REQUIRE (kirin::isSurround51Roles (fiveOne));
    KIRIN_PF_REQUIRE (! kirin::supportsStereoWorkflows (fiveOne));
    KIRIN_PF_REQUIRE (kirin::supportsStereoWorkflows (stereo));
    KIRIN_PF_REQUIRE (kirin::productModeForRoles (stereo)
                      == kirin::PreparedProductMode::stereoWorkflows);
    KIRIN_PF_REQUIRE (kirin::productModeForRoles (fiveOne)
                      == kirin::PreparedProductMode::surroundMeasurementOnly);
    KIRIN_PF_REQUIRE (kirin::productModeForRoles ({})
                      == kirin::PreparedProductMode::none);
    KIRIN_PF_REQUIRE (std::string_view (kirin::channelRoleShortName (kLfe)) == "LFE");

    const kirin::PreparedFormat preparedStereo { 48'000.0, stereo };
    KIRIN_PF_REQUIRE (preparedStereo.matches (48'000.0, stereo));
    KIRIN_PF_REQUIRE (! preparedStereo.matches (44'100.0, stereo));
    // Same roles, different order: L and R swapped is not the same binding.
    KIRIN_PF_REQUIRE (! preparedStereo.matches (48'000.0, std::vector<uint8_t> { kR, kL }));

    // The P-3 gate: a count-only comparison would call these two the same format and keep an
    // engine built for the other one.
    const kirin::PreparedFormat prepared712 { 48'000.0, sevenOneTwo };
    KIRIN_PF_REQUIRE (! prepared712.matches (48'000.0, fiveOneFour));
    const kirin::PreparedFormat prepared71 { 48'000.0, sevenOne };
    KIRIN_PF_REQUIRE (! prepared71.matches (48'000.0, fiveOneTwo));

    // No engine yet: nothing to reuse and nothing Record could be holding.
    KIRIN_PF_REQUIRE (kirin::decidePrepare (false, false, 48'000.0, stereo, {})
                      == PrepareAction::rebuild);
    KIRIN_PF_REQUIRE (kirin::decidePrepare (false, true, 48'000.0, stereo, {})
                      == PrepareAction::rebuild);

    // Same format: the engine and whatever Record is doing with it are left alone. This is what
    // keeps a Studio One offline re-prepare from throwing away a live take (B-141).
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, false, 48'000.0, stereo, preparedStereo)
                      == PrepareAction::reuse);
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, true, 48'000.0, stereo, preparedStereo)
                      == PrepareAction::reuse);

    // Different format while free: replace.
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, false, 96'000.0, stereo, preparedStereo)
                      == PrepareAction::rebuild);
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, false, 48'000.0, sevenOne, preparedStereo)
                      == PrepareAction::rebuild);

    // Different format while Record owns the engine: held, never applied here. B-334 keeps Stop
    // authority with the user; B-961 keeps the request from being forgotten.
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, true, 96'000.0, stereo, preparedStereo)
                      == PrepareAction::holdForRecord);
    KIRIN_PF_REQUIRE (kirin::decidePrepare (true, true, 48'000.0, fiveOneFour, prepared712)
                      == PrepareAction::holdForRecord);

    // Holding is a state that ends, not a value that decays.
    kirin::HeldFormat held;
    KIRIN_PF_REQUIRE (! held.held);
    held.hold (96'000.0, 1024);
    KIRIN_PF_REQUIRE (held.held && held.sampleRate == 96'000.0 && held.maxBlockFrames == 1024);
    held.release();
    KIRIN_PF_REQUIRE (! held.held);

    std::cout << "Prepared format: PASS (reuse, rebuild, hold; 10ch and 8ch layouts distinguished)"
              << '\n';
}
#undef KIRIN_PF_REQUIRE

} // namespace hypha::tests::prepared_format_contract
