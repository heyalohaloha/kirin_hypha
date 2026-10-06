#include "../src/update/UpdateTransport.h"
#include "../src/update/UpdateManifest.h"
#include "../src/update/UpdateHttpResponse.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace hypha::update;
using Clock = std::chrono::steady_clock;
void require (bool value, const char* text)
{
    if (! value) { std::cerr << "FAIL: " << text << '\n'; std::exit (1); }
}

bool sendBytes (int socket, const std::string& bytes)
{
    std::size_t sent = 0;
    while (sent < bytes.size())
    {
        const auto amount = ::send (socket, bytes.data() + sent, bytes.size() - sent, 0);
        if (amount <= 0) return false;
        sent += static_cast<std::size_t> (amount);
    }
    return true;
}

// Disposable loopback only. No disk, user configuration or external endpoint.
class Server
{
public:
    using Reply = std::function<void (int, const std::string&, Server&)>;
    explicit Server (Reply reply) : response (std::move (reply))
    {
        listener = ::socket (AF_INET, SOCK_STREAM, 0);
        require (listener >= 0, "create disposable loopback socket");
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        require (::bind (listener, reinterpret_cast<sockaddr*> (&address), sizeof (address)) == 0
            && ::listen (listener, 4) == 0, "bind only 127.0.0.1 on an ephemeral port");
        socklen_t length = sizeof (address);
        require (::getsockname (listener, reinterpret_cast<sockaddr*> (&address), &length) == 0,
                 "read fixture port");
        port = ntohs (address.sin_port);
        worker = std::thread ([this] { run(); });
    }

    ~Server()
    {
        stop = true;
        ::shutdown (listener, SHUT_RDWR);
        {
            const std::lock_guard<std::mutex> lock (sockets);
            if (client >= 0) ::shutdown (client, SHUT_RDWR);
        }
        worker.join();
        ::close (listener);
    }

    unsigned short port = 0;
    std::atomic<int> requests { 0 };
    std::atomic<std::size_t> bodyBytesSent { 0 };
    std::atomic<bool> stop { false };

private:
    void run()
    {
        while (! stop)
        {
            pollfd incoming { listener, POLLIN, 0 };
            if (::poll (&incoming, 1, 50) <= 0) continue;
            const auto connected = ::accept (listener, nullptr, nullptr);
            if (connected < 0) continue;
            {
                const std::lock_guard<std::mutex> lock (sockets);
                client = connected;
            }
            int noSignal = 1;
            ::setsockopt (connected, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof (noSignal));
            timeval timeout { 1, 0 };
            ::setsockopt (connected, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
            ::setsockopt (connected, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
            std::string request;
            std::array<char, 2048> bytes {};
            while (request.size() < 8192 && request.find ("\r\n\r\n") == std::string::npos && ! stop)
            {
                const auto amount = ::recv (connected, bytes.data(), bytes.size(), 0);
                if (amount <= 0) break;
                request.append (bytes.data(), static_cast<std::size_t> (amount));
            }
            if (! request.empty())
            {
                ++requests;
                require (request.rfind ("GET /manifest HTTP/1.1\r\n", 0) == 0,
                         "fixture request has one fixed GET, no query or payload");
                require (request.find ("Authorization:") == std::string::npos
                    && request.find ("Cookie:") == std::string::npos,
                         "no stored credentials/cookies in fixture request");
                response (connected, request, *this);
            }
            {
                const std::lock_guard<std::mutex> lock (sockets);
                ::close (connected);
                client = -1;
            }
        }
    }
    int listener = -1, client = -1;
    std::mutex sockets;
    Reply response;
    std::thread worker;
};

std::string headers (std::size_t length, const std::string& type = "application/json")
{
    return "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
         + std::to_string (length) + "\r\nConnection: close\r\n\r\n";
}

std::optional<juce::String> checkedFetch (Server& server, std::atomic<bool>& cancel, MacTransportProbe& probe)
{
    auto result = fetchManifestMacForTest (cancel, server.port, probe);
    require (probe.maximumRetainedBytes <= maximumManifestBytes, "owned body retained bytes never exceed 16 KiB");
    require (probe.maximumHeaderBytes <= HttpResponse::maximumHeaderBytes, "owned headers never exceed 8 KiB");
    require (probe.connectionCancelled && probe.callbacksDrained && probe.handlesReleased,
             "final connection callback, all send/receive callbacks and handles drain before return");
    require (probe.sends == 1, "raw transport sends GET once with no resubmission machinery");
    if (server.requests != 1) std::cerr << "native HTTP sends observed=" << server.requests << '\n';
    require (server.requests == 1, "one HTTP submission, no application retry");
    return result;
}
}

int main()
{
    using namespace hypha::update;
    std::atomic<bool> cancel { false };
    std::size_t maximumRetained = 0;
    int cases = 0;
    const auto bodyCase = [&] (const std::string& body, bool accepted, const std::string& type = "application/json")
    {
        Server server ([&] (int socket, const std::string&, Server&)
        { sendBytes (socket, headers (body.size(), type) + body); });
        MacTransportProbe probe;
        const auto result = checkedFetch (server, cancel, probe);
        require (result.has_value() == accepted, "native body validation matches fixture expectation");
        if (accepted) require (result->getNumBytesAsUTF8() == body.size(), "received byte count matches exact body");
        if (body.size() > maximumManifestBytes)
            require (probe.maximumRetainedBytes == 0,
                     "oversized declared length is rejected before accepting body data");
        maximumRetained = std::max (maximumRetained, probe.maximumRetainedBytes);
        ++cases;
    };
    bodyCase ("{\"fixture\":true}", true);
    bodyCase (std::string (maximumManifestBytes, ' '), true);
    bodyCase (std::string (maximumManifestBytes + 1, ' '), false);
    bodyCase ("{}", false, "text/html");
    bodyCase ("{}", true, "application/json; charset=utf-8");
    bodyCase ("{}", true, "application/json ; charset=utf-8");
    bodyCase (std::string ("{\0}", 3), false);
    bodyCase (std::string { static_cast<char> (0xff), static_cast<char> (0xfe) }, false);  // invalid UTF-8 bytes, not text
    bodyCase ("", false);

    {
        Server server ([] (int socket, const std::string&, Server&)
        { sendBytes (socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{}"); });
        MacTransportProbe probe;
        const auto result = checkedFetch (server, cancel, probe);
        require (result && *result == "{}", "close-delimited body finishes only on TCP EOF");
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server&)
        { sendBytes (socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Flood: "
            + std::string (HttpResponse::maximumHeaderBytes + 4096, 'x')); });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe) && probe.maximumRetainedBytes == 0
            && probe.maximumHeaderBytes == HttpResponse::maximumHeaderBytes,
                 "native header flood is cancelled at exactly 8 KiB before accepting any body");
        ++cases;
    }
    {
        const auto bound = ::socket (AF_INET, SOCK_STREAM, 0);
        require (bound >= 0, "reserve non-listening fixture socket");
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        require (::bind (bound, reinterpret_cast<sockaddr*> (&address), sizeof (address)) == 0,
                 "reserve an ephemeral refused loopback endpoint");
        socklen_t length = sizeof (address);
        require (::getsockname (bound, reinterpret_cast<sockaddr*> (&address), &length) == 0, "read refused fixture port");
        MacTransportProbe probe;
        require (! fetchManifestMacForTest (cancel, ntohs (address.sin_port), probe) && probe.sends == 0
            && probe.connectionCancelled && probe.callbacksDrained && probe.handlesReleased,
                 "pre-ready connection failure sends no GET and fully drains native callbacks/handles");
        ::close (bound);
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server&)
        { sendBytes (socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}"); });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe) && probe.maximumRetainedBytes == 0,
                 "unsupported compressed content is refused before accepting body");
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server&)
        {
            sendBytes (socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            const std::string chunk = "1000\r\n" + std::string (4096, ' ') + "\r\n";
            for (int i = 0; i < 4; ++i) sendBytes (socket, chunk);
            sendBytes (socket, "0\r\n\r\n");
        });
        MacTransportProbe probe;
        const auto result = checkedFetch (server, cancel, probe);
        require (result && result->getNumBytesAsUTF8() == maximumManifestBytes,
                 "unknown-length chunked body at exactly 16 KiB succeeds");
        ++cases;
    }
    {
        Server server ([] (int, const std::string&, Server&) {});
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe), "dropped fresh connection fails without a new task");
        std::cout << "dropped_connection: requests=" << server.requests << '\n';
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server&)
        { sendBytes (socket, headers (100) + "{}"); });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe), "truncated Content-Length is a failure");
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server& server)
        {
            sendBytes (socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
            const std::string chunk = "1000\r\n" + std::string (4096, 'x') + "\r\n";
            for (int i = 0; i < 4096 && ! server.stop; ++i)
            {
                if (! sendBytes (socket, chunk)) break;
                server.bodyBytesSent += 4096;
            }
        });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe), "chunked unbounded flood is cancelled at the receive boundary");
        maximumRetained = std::max (maximumRetained, probe.maximumRetainedBytes);
        std::cout << "flood: retained=" << probe.maximumRetainedBytes << " callbacks=" << probe.dataCallbacks
                  << " server_sent=" << server.bodyBytesSent << '\n';
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server& server)
        {
            sendBytes (socket, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:"
                + std::to_string (server.port) + "/manifest\r\nContent-Length: 0\r\n\r\n");
        });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe), "redirect is refused");
        ++cases;
    }
    {
        Server server ([] (int socket, const std::string&, Server&)
        { sendBytes (socket, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=fixture\r\nContent-Length: 0\r\n\r\n"); });
        MacTransportProbe probe;
        require (! checkedFetch (server, cancel, probe), "authentication challenge is refused without resubmission");
        ++cases;
    }
    for (const bool bodyStarted : { false, true })
    {
        Server server ([=] (int socket, const std::string&, Server& server)
        {
            if (bodyStarted) sendBytes (socket, headers (100) + "{");
            while (! server.stop) std::this_thread::sleep_for (std::chrono::milliseconds (10));
        });
        MacTransportProbe probe;
        const auto start = Clock::now();
        require (! checkedFetch (server, cancel, probe), "total deadline rejects stalled headers/body");
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds> (Clock::now() - start).count();
        require (milliseconds >= 2800 && milliseconds < 5000, "fixture cancellation plus drain completes near three seconds");
        std::cout << "timeout: body_started=" << bodyStarted << " elapsed_ms=" << milliseconds << '\n';
        ++cases;
    }
    for (int repeat = 0; repeat < 6; ++repeat)
    {
        Server server ([] (int socket, const std::string&, Server& server)
        {
            sendBytes (socket, headers (100) + "{");
            while (! server.stop) std::this_thread::sleep_for (std::chrono::milliseconds (2));
        });
        MacTransportProbe probe;
        std::thread off ([&]
        {
            while (server.requests == 0) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            cancel = true;
        });
        require (! checkedFetch (server, cancel, probe), "OFF cancellation survives repeated native cleanup");
        off.join();
        cancel = false;
        ++cases;
    }
    {
        std::array<std::thread, 4> requests;
        std::atomic<int> complete { 0 };
        for (auto& request : requests)
            request = std::thread ([&]
            {
                Server server ([] (int socket, const std::string&, Server&)
                { sendBytes (socket, headers (2) + "{}"); });
                std::atomic<bool> ownCancel { false };
                MacTransportProbe probe;
                require (checkedFetch (server, ownCancel, probe).has_value(), "concurrent requests use independent native state");
                ++complete;
            });
        for (auto& request : requests) request.join();
        require (complete == 4, "all concurrent connection callback/handle lifetimes completed independently");
        cases += complete;
    }
    cancel = true;
    MacTransportProbe probe;
    require (! fetchManifestMacForTest (cancel, 1, probe) && ! probe.connectionCancelled && probe.dataCallbacks == 0,
             "pre-cancelled request creates no task");
    ++cases;
    std::cout << "PASS: native macOS transport cases=" << cases << " maximum_retained=" << maximumRetained << '\n';
    return 0;
}
