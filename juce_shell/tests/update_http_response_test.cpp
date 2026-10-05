#include "../src/update/UpdateHttpResponse.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void require (bool value, const char* text)
{
    if (! value) { std::cerr << "FAIL: " << text << '\n'; std::exit (1); }
}
}

int main()
{
    using namespace hypha::update;
    struct Fixture { std::string wire, body; bool accepted; };
    const std::string fixed = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
    const std::string chunked = fixed + "Transfer-Encoding: chunked\r\n\r\n";
    const std::vector<Fixture> fixtures {
        { fixed + "Content-Length: 2\r\n\r\n{}", "{}", true },
        { fixed + "\r\n{}", "{}", true },
        { "HTTP/1.0 200 OK\r\nContent-Type: APPLICATION/JSON ; charset=utf-8\r\n\r\n{}", "{}", true },
        { chunked + "1\r\n{\r\n1;fixture=yes\r\n}\r\n0\r\n\r\n", "{}", true },
        { chunked + "2\r\n{}\r\n0\r\nX-Optional: value\r\n\r\n", "{}", true },
        { "HTTP/1.1 302 Found\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 3\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 1\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 16385\r\n\r\n", "", false },
        { fixed + "Content-Length: 999999999999999999999999999999\r\n\r\n", "", false },
        { fixed + "Content-Length: -1\r\n\r\n", "", false },
        { fixed + "Content-Length: +2\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 2, 2\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { fixed + "Content-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}", "", false },
        { fixed + "Transfer-Encoding: gzip, chunked\r\n\r\n", "", false },
        { fixed + "Content-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { fixed + "Content-Length : 2\r\n\r\n{}", "", false },
        { fixed + " Content-Length: 2\r\n\r\n{}", "", false },
        { fixed + "X-Fold: value\r\n more\r\nContent-Length: 2\r\n\r\n{}", "", false },
        { chunked + "4001\r\n", "", false },
        { chunked + "ffffffffffffffffffffffff\r\n", "", false },
        { chunked + "-1\r\n", "", false },
        { chunked + "2\r\n{}x\n0\r\n\r\n", "", false },
        { chunked + "2\r\n{}\r\n0\r\nContent-Type: text/html\r\n\r\n", "", false },
        { chunked + "2\r\n{}\r\n0\r\nContent-Length: 2\r\n\r\n", "", false },
        { chunked + "2\r\n{}\r\n0\r\n", "", false },
        { fixed + "X-Flood: " + std::string (HttpResponse::maximumHeaderBytes, 'x'), "", false },
        { chunked + std::string (257, '0') + "\r\n", "", false },
        { fixed + "\r\n" + std::string (maximumManifestBytes, ' '), std::string (maximumManifestBytes, ' '), true },
        { fixed + "\r\n" + std::string (maximumManifestBytes + 1, ' '), "", false }
    };
    std::size_t checks = 0;
    for (const auto& fixture : fixtures)
    {
        // Cover every framing split for small messages and boundaries for large ones.
        std::vector<std::size_t> cuts;
        if (fixture.wire.size() < 512)
            for (std::size_t cut = 0; cut <= fixture.wire.size(); ++cut) cuts.push_back (cut);
        else cuts = { 0, 1, 64, fixture.wire.size() / 2, fixture.wire.size() - 1, fixture.wire.size() };
        for (const auto cut : cuts)
        {
            HttpResponse parser;
            parser.feed (fixture.wire.data(), cut);
            parser.feed (fixture.wire.data() + cut, fixture.wire.size() - cut);
            const auto accepted = parser.finish();
            require (accepted == fixture.accepted, "streaming parser accepts/rejects every framing split consistently");
            require (parser.size() <= maximumManifestBytes && parser.headerSize() <= HttpResponse::maximumHeaderBytes,
                     "body/header storage remains bounded on every error path");
            if (accepted) require (std::string (parser.data(), parser.size()) == fixture.body, "exact decoded body bytes");
            ++checks;
        }
    }
    std::cout << "PASS: bounded HTTP parser fixtures=" << fixtures.size() << " split_checks=" << checks << '\n';
}
