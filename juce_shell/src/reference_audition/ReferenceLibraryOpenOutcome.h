#pragma once

#include <cstdint>

// What became of an "open in Kirin OS" request. Kirin OS removes the request file when it opened the
// library, but also when the request expired (from 15 s on) or its open came too late, without
// opening it. So a file gone before the deadline is an open; at or after it, it is not.
namespace hypha::reference_audition
{
enum class LibraryOpenOutcome { pending, opened, timedOut };
inline constexpr std::int64_t libraryOpenDeadlineMs = 15000;

constexpr LibraryOpenOutcome libraryOpenOutcome (bool fileGone, std::int64_t elapsedMs) noexcept
{
    if (elapsedMs >= libraryOpenDeadlineMs) return LibraryOpenOutcome::timedOut;
    return fileGone ? LibraryOpenOutcome::opened : LibraryOpenOutcome::pending;
}
}
