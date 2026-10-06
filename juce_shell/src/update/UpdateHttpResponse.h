#pragma once
#include "UpdateManifest.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace hypha::update
{
// Deliberately restricted HTTP/1.x response parser. No allocation, redirects,
// authentication, compression or retry machinery. RFC 9112 framing only.
class HttpResponse
{
public:
    static constexpr std::size_t maximumHeaderBytes = 8 * 1024;
    bool feed (const void*, std::size_t) noexcept;
    bool finish() noexcept;
    bool failed() const noexcept { return phase == Phase::error; }
    bool complete() const noexcept { return phase == Phase::done; }
    const char* data() const noexcept { return body.data(); }
    std::size_t size() const noexcept { return bodyUsed; }
    std::size_t headerSize() const noexcept { return headerUsed; }

private:
    enum class Phase { headers, fixed, closed, chunkSize, chunkData, chunkCR, chunkLF,
                       trailers, done, error };
    bool parseHeaders() noexcept;
    bool parseChunkSize() noexcept;
    bool byte (char) noexcept;
    bool fail() noexcept { phase = Phase::error; return false; }
    std::array<char, maximumHeaderBytes> header {};
    std::array<char, maximumManifestBytes> body {};
    std::array<char, 256> line {};
    std::size_t headerUsed = 0, bodyUsed = 0, lineUsed = 0, remaining = 0, trailerBytes = 0;
    Phase phase = Phase::headers;
};
}
