#include "HyphaPairPreview.h"

namespace hypha::pair_preview
{
namespace
{
struct ModuleLifetime { ~ModuleLifetime() { kirin_hypha_pair_preview_shutdown(); } };
}
bool request (const Ticket& ticket)
{
    // Construct before spawning: the module's non-RT unload drains its worker before unmapping code.
    // This guard has module-local linkage. Ticket/engine destruction never joins discovery.
    static ModuleLifetime lifetime;
    return ticket && kirin_hypha_pair_preview_request (ticket.get());
}
}
