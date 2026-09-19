#include "../src/HyphaPairPreview.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#if defined(_WIN32)
 #define PROBE_EXPORT extern "C" __declspec (dllexport)
#else
 #define PROBE_EXPORT extern "C" __attribute__ ((visibility ("default")))
#endif

static_assert (offsetof (KirinPairPreviewValue, candidate) == 10);

namespace
{
hypha::pair_preview::Ticket ticket;

struct Lifetime
{
    std::atomic<int>* count;

    ~Lifetime()
    {
        count->fetch_add (1);
    }
};
}

PROBE_EXPORT bool startPairPreviewProbe(std::atomic<int>* unloaded)
{
    static Lifetime lifetime { unloaded }; // destructed after the module's discovery guard
    constexpr uint8_t channelRoles[] {
        KIRIN_CHANNEL_ROLE_LEFT,
        KIRIN_CHANNEL_ROLE_RIGHT,
    };
    auto* engine = kirin_hypha_create (48'000, channelRoles, 2);
    if (engine == nullptr)
        return false;
    kirin_hypha_set_identity (engine, "module-preview-post", "module-preview-project",
                              "module-preview-session", "");
    ticket.reset (kirin_hypha_pair_preview_create (engine));
    kirin_hypha_destroy (engine); // discovery owns no engine pointer
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (hypha::pair_preview::request (ticket))
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}

PROBE_EXPORT void cancelPairPreviewProbe()
{
    ticket.reset();
}

PROBE_EXPORT void drainPairPreviewProbe()
{
    ticket.reset();
    kirin_hypha_pair_preview_shutdown();
}
