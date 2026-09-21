#pragma once
#include "kirin_hypha_pair_preview_ffi.h"
#include <memory>

namespace hypha::pair_preview
{
struct Delete { void operator() (KirinPairPreview* value) const { kirin_hypha_pair_preview_destroy (value); } };
using Ticket = std::unique_ptr<KirinPairPreview, Delete>;

bool request (const Ticket& ticket);
}
