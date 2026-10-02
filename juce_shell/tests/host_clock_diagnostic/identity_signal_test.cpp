#include "IdentitySignal.h"
#include "IdentityAudit.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void require (bool value, const char* reason)
{ if (! value) { std::fprintf (stderr, "%s\n", reason); std::abort(); } }

static std::uint32_t decode (float left, float right)
{
    const auto l = static_cast<std::uint32_t> (static_cast<int> (left * 4194304.0f) + 32768);
    const auto r = static_cast<std::uint32_t> (static_cast<int> (right * 4194304.0f) + 32768);
    // Multiplicative inverse mod 2^32, checked below. Decoder is not used by signal production.
    return (((r << 16) | l) ^ 0x80008000u) * 0x0e8b2f51u;
}

int main()
{
    static_assert (0x9e3779b1u * 0x0e8b2f51u == 1u);
    for (std::uint32_t id = 1; id <= 1'000'000; ++id)
    {
        float left, right;
        hypha::clock_diagnostic::IdentitySignal::sample (id, left, right);
        require (decode (left, right) == id, "identity is not reversible");
        require (std::abs (left) <= 1.0f / 128 && std::abs (right) <= 1.0f / 128,
                 "signal exceeds -42.14 dBFS peak");
        require (left != 0 || right != 0, "nonzero identity aliases silence");
    }
    for (const auto id : { 0u, 0x80000000u, 0xfffffffeu, 0xffffffffu })
    {
        float left, right;
        hypha::clock_diagnostic::IdentitySignal::sample (id, left, right);
        require (decode (left, right) == id, "identity boundary wrap");
    }
    hypha::clock_diagnostic::IdentitySignal signal;
    std::array<float, 17> left {}, right {};
    float* out[] { left.data(), right.data() };
    signal.render (out, 2, 17, false);
    require (left[0] == 0 && right[16] == 0, "unarmed signal is not silent");
    signal.render (out, 2, 17, true);
    require (decode (left[0], right[0]) == 1 && decode (left[16], right[16]) == 17,
             "first block emission identity");
    signal.render (out, 2, 17, false);
    signal.render (out, 2, 17, true);
    require (decode (left[0], right[0]) == 18, "pause must not repeat old identities");
    using Audit = hypha::clock_diagnostic::IdentityAudit;
    const auto checked = Audit::inspect (left.data(), right.data(), 17);
    require (checked.first == 18 && checked.last == 34 && checked.frames == 17
        && checked.errors == 0 && checked.silentPrefix == 0, "full frame audit");
    const auto interiorLeft = left[8], interiorRight = right[8];
    hypha::clock_diagnostic::IdentitySignal::sample (18 + 8 + 24000, left[8], right[8]);
    require (Audit::inspect (left.data(), right.data(), 17).errors == 1,
             "one-lap interior error cannot hide behind correct boundaries");
    left[8] = std::numeric_limits<float>::quiet_NaN();
    require (Audit::inspect (left.data(), right.data(), 17).errors != 0, "interior NaN rejected");
    left[8] = interiorLeft; right[8] = interiorRight;
    left[0] = right[0] = left[1] = right[1] = 0;
    const auto startup = Audit::inspect (left.data(), right.data(), 17);
    require (startup.silentPrefix == 2 && startup.frames == 15 && startup.first == 20
        && startup.errors == 0, "split initial delay silence is explicit");
    left[8] = right[8] = 0;
    require (Audit::inspect (left.data(), right.data(), 17).errors != 0,
             "interior silence is not initial delay silence");
    for (const float invalid : { std::numeric_limits<float>::infinity(), 0.25f, 1.0e-20f })
    { std::uint32_t id = 0; require (! Audit::decode (invalid, 0, id), "invalid grid value refused"); }
    signal.render (out, 1, 17, true);
    require (left[0] == 0, "mono must refuse to generate incomplete identities");
    std::puts ("identity source: 1000000 exact identities, peak, stop/resume, full frame audit and interior faults PASS");
}
