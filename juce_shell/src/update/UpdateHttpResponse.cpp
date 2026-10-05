#include "UpdateHttpResponse.h"

namespace hypha::update
{
namespace
{
std::string_view trim (std::string_view value) noexcept
{
    while (! value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix (1);
    while (! value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix (1);
    return value;
}
char lower (char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char> (c + ('a' - 'A')) : c; }
bool equal (std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (lower (a[i]) != lower (b[i])) return false;
    return true;
}
bool number (std::string_view value, unsigned base, std::size_t limit, std::size_t& result) noexcept
{
    if (value.empty()) return false;
    result = 0;
    for (const char c : value)
    {
        const unsigned digit = c >= '0' && c <= '9' ? static_cast<unsigned> (c - '0')
                             : c >= 'a' && c <= 'f' ? static_cast<unsigned> (c - 'a' + 10)
                             : c >= 'A' && c <= 'F' ? static_cast<unsigned> (c - 'A' + 10) : base;
        if (digit >= base || digit > limit || result > (limit - digit) / base) return false;
        result = result * base + digit;
    }
    return true;
}
bool field (std::string_view text, std::string_view& name, std::string_view& value) noexcept
{
    const auto colon = text.find (':');
    if (colon == std::string_view::npos || colon == 0) return false;
    name = text.substr (0, colon);
    for (const auto c : name)
        if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || std::string_view ("!#$%&'*+-.^_`|~").find (c) != std::string_view::npos)) return false;
    value = trim (text.substr (colon + 1));
    for (const auto c : value) if ((static_cast<unsigned char> (c) < 32 && c != '\t') || c == 127) return false;
    return true;
}
}

bool HttpResponse::parseHeaders() noexcept
{
    std::string_view all (header.data(), headerUsed);
    const auto firstEnd = all.find ("\r\n");
    if (firstEnd == std::string_view::npos) return fail();
    const auto status = all.substr (0, firstEnd);
    if (status.size() < 13 || (status.substr (0, 9) != "HTTP/1.1 " && status.substr (0, 9) != "HTTP/1.0 ")
        || status.substr (9, 4) != "200 ") return fail();
    for (const char c : status) if (static_cast<unsigned char> (c) < 32 || c == 127) return fail();
    all.remove_prefix (firstEnd + 2);
    bool mime = false, length = false, transfer = false, encoding = false;
    while (all.size() > 2)
    {
        const auto end = all.find ("\r\n");
        if (end == std::string_view::npos) return fail();
        std::string_view name, value;
        if (! field (all.substr (0, end), name, value)) return fail();
        if (equal (name, "Content-Type"))
        {
            if (mime || ! equal (trim (value.substr (0, value.find (';'))), "application/json")) return fail();
            mime = true;
        }
        else if (equal (name, "Content-Length"))
        {
            if (length || ! number (value, 10, maximumManifestBytes, remaining)) return fail();
            length = true;
        }
        else if (equal (name, "Transfer-Encoding"))
        {
            if (transfer || ! equal (value, "chunked")) return fail();
            transfer = true;
        }
        else if (equal (name, "Content-Encoding"))
        {
            if (encoding || ! equal (value, "identity")) return fail();
            encoding = true;
        }
        all.remove_prefix (end + 2);
    }
    if (! mime || (length && transfer) || all != "\r\n") return fail();
    phase = transfer ? Phase::chunkSize : length ? (remaining == 0 ? Phase::done : Phase::fixed) : Phase::closed;
    return true;
}

bool HttpResponse::parseChunkSize() noexcept
{
    const std::string_view text (line.data(), lineUsed - 2);
    if (! number (trim (text.substr (0, text.find (';'))), 16, body.size() - bodyUsed, remaining)) return fail();
    for (const char c : text) if (static_cast<unsigned char> (c) < 32 || c == 127) return fail();
    lineUsed = 0;
    phase = remaining == 0 ? Phase::trailers : Phase::chunkData;
    return true;
}

bool HttpResponse::byte (char c) noexcept
{
    switch (phase)
    {
        case Phase::headers:
            if (headerUsed == header.size() || c == 0) return fail();
            header[headerUsed++] = c;
            if (headerUsed >= 4 && std::string_view (header.data() + headerUsed - 4, 4) == "\r\n\r\n")
                return parseHeaders();
            return true;
        case Phase::fixed:
        case Phase::closed:
        case Phase::chunkData:
            if (bodyUsed == body.size()) return fail();
            body[bodyUsed++] = c;
            if (phase != Phase::closed && --remaining == 0)
                phase = phase == Phase::fixed ? Phase::done : Phase::chunkCR;
            return true;
        case Phase::chunkSize:
        case Phase::trailers:
            if (lineUsed == line.size() || c == 0) return fail();
            if (phase == Phase::trailers && ++trailerBytes > maximumHeaderBytes) return fail();
            line[lineUsed++] = c;
            if (lineUsed >= 2 && line[lineUsed - 2] == '\r' && c == '\n')
            {
                if (phase == Phase::chunkSize) return parseChunkSize();
                if (lineUsed == 2) phase = Phase::done;
                else
                {
                    std::string_view name, value;
                    if (! field ({ line.data(), lineUsed - 2 }, name, value)
                        || equal (name, "Content-Length") || equal (name, "Transfer-Encoding")
                        || equal (name, "Content-Type") || equal (name, "Content-Encoding")) return fail();
                }
                lineUsed = 0;
            }
            return true;
        case Phase::chunkCR:
            if (c != '\r') return fail();
            phase = Phase::chunkLF; return true;
        case Phase::chunkLF:
            if (c != '\n') return fail();
            phase = Phase::chunkSize; return true;
        case Phase::done:
        case Phase::error: return fail();
    }
    return fail();
}

bool HttpResponse::feed (const void* bytes, std::size_t count) noexcept
{
    const auto* text = static_cast<const char*> (bytes);
    for (std::size_t i = 0; i < count; ++i) if (! byte (text[i])) return false;
    return ! failed();
}

bool HttpResponse::finish() noexcept
{
    if (phase == Phase::closed) phase = Phase::done;
    return complete() || fail();
}
}
