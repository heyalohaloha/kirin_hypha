#include "UpdateTransport.h"
#include "UpdateManifest.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string_view>

namespace hypha::update
{
namespace
{
static_assert (std::string_view (manifestEndpoint)
               == "https://kirinmastering.com/updates/hypha-stable.v1.json");
constexpr auto host = L"kirinmastering.com";
constexpr auto route = L"/updates/hypha-stable.v1.json";
constexpr auto totalTimeout = std::chrono::seconds (3);
using Clock = std::chrono::steady_clock;

// Values/ABI from Microsoft's winhttp.h. Older SDKs may not name these options;
// unsupported OS implementations fail BEFORE sending, not via a weaker fallback.
constexpr DWORD failedConnectionRetriesOption = 162;
constexpr DWORD disableGlobalPoolingOption = 195;
struct FailedConnectionRetries { DWORD dwMaxRetries, dwAllowedRetryConditions; };

struct CallbackState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool sent = false, headers = false, read = false, failed = false;
    DWORD readBytes = 0;
    std::array<char, maximumManifestBytes + 1> bytes {};
};

void CALLBACK onStatus (HINTERNET, DWORD_PTR context, DWORD status, LPVOID, DWORD length) noexcept
{
    auto* state = reinterpret_cast<CallbackState*> (context);
    if (state == nullptr) return;
    try
    {
        // Callbacks only signal. All WinHTTP API transitions are made by the worker,
        // avoiding inline callback recursion and concurrent handle-close/API races.
        const std::lock_guard<std::mutex> lock (state->mutex);
        switch (status)
        {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: state->sent = true; break;
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: state->headers = true; break;
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: state->readBytes = length; state->read = true; break;
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            case WINHTTP_CALLBACK_STATUS_REDIRECT: state->failed = true; break;
            default: break;
        }
        state->changed.notify_one();
    }
    catch (...) { /* No exception crosses a Windows callback boundary; deadline cancels. */ }
}

struct Handles
{
    HINTERNET session = nullptr, connection = nullptr, request = nullptr;
    HANDLE unloaded = nullptr;
    bool unloadNotification = false;

    ~Handles()
    {
        // Closing cancels pending asynchronous work. CloseHandle alone does NOT prove
        // callbacks have returned. Keep CallbackState/read buffers/module code alive
        // until this session's last callback has finished (official unload event).
        if (request != nullptr) WinHttpCloseHandle (request);
        if (connection != nullptr) WinHttpCloseHandle (connection);
        if (session != nullptr) WinHttpCloseHandle (session);
        if (unloadNotification) WaitForSingleObject (unloaded, INFINITE);
        if (unloaded != nullptr) CloseHandle (unloaded);
    }

    bool open()
    {
        unloaded = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        if (unloaded == nullptr) return false;
        // No PAC/proxy discovery, proxy failover, cookies or shared authentication state.
        session = WinHttpOpen (L"Kirin-Hypha-Update/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (session == nullptr) return false;
        if (! WinHttpSetOption (session, WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT, &unloaded, sizeof (unloaded)))
            return false;
        unloadNotification = true;
        DWORD attempts = 1; // One IP connection attempt, not the default five.
        DWORD noAdvancedHttp = 0; // Fresh HTTP/1.1 only; no HTTP/2 stream recovery.
        DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        BOOL disablePooling = TRUE;
        FailedConnectionRetries retries { 0, 0 };
        return WinHttpSetTimeouts (session, 3000, 3000, 3000, 3000)
            && WinHttpSetOption (session, WINHTTP_OPTION_CONNECT_RETRIES, &attempts, sizeof (attempts))
            && WinHttpSetOption (session, failedConnectionRetriesOption, &retries, sizeof (retries))
            && WinHttpSetOption (session, disableGlobalPoolingOption, &disablePooling, sizeof (disablePooling))
            && WinHttpSetOption (session, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof (secureProtocols))
            && WinHttpSetOption (session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &noAdvancedHttp, sizeof (noAdvancedHttp));
    }

    bool prepare (CallbackState& state)
    {
        connection = WinHttpConnect (session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connection == nullptr) return false;
        const wchar_t* accepted[] = { L"application/json", nullptr };
        request = WinHttpOpenRequest (connection, L"GET", route, nullptr,
                                      WINHTTP_NO_REFERER, accepted, WINHTTP_FLAG_SECURE);
        if (request == nullptr) return false;
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES
                       | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_KEEP_ALIVE;
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        DWORD headerLimit = maximumManifestBytes;
        DWORD_PTR context = reinterpret_cast<DWORD_PTR> (&state);
        if (! WinHttpSetOption (request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof (disabled))
            || ! WinHttpSetOption (request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof (redirects))
            || ! WinHttpSetOption (request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof (headerLimit))
            || ! WinHttpSetOption (request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof (context))) return false;
        const auto callback = WinHttpSetStatusCallback (request, onStatus,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES
            | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | WINHTTP_CALLBACK_FLAG_REDIRECT, 0);
        return callback != WINHTTP_INVALID_STATUS_CALLBACK;
    }
};

bool waitFor (CallbackState& state, bool CallbackState::* completed,
              const std::atomic<bool>& cancelled, Clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock (state.mutex);
    while (! (state.*completed) && ! state.failed && ! cancelled.load() && Clock::now() < deadline)
        state.changed.wait_until (lock, std::min (deadline, Clock::now() + std::chrono::milliseconds (50)));
    return (state.*completed) && ! state.failed && ! cancelled.load() && Clock::now() < deadline;
}

bool validHeaders (HINTERNET request)
{
    DWORD status = 0, size = sizeof (status);
    if (! WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)
        || status != 200) return false;
    std::array<wchar_t, 128> type {};
    size = sizeof (type);
    if (! WinHttpQueryHeaders (request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                               type.data(), &size, WINHTTP_NO_HEADER_INDEX)
        || ! juce::String (type.data()).upToFirstOccurrenceOf (";", false, false)
            .trim().equalsIgnoreCase ("application/json")) return false;
    std::array<wchar_t, 128> encoding {};
    size = sizeof (encoding);
    if (WinHttpQueryHeaders (request, WINHTTP_QUERY_CONTENT_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX,
                              encoding.data(), &size, WINHTTP_NO_HEADER_INDEX))
    {
        if (! juce::String (encoding.data()).trim().equalsIgnoreCase ("identity")) return false;
    }
    else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) return false;
    DWORD length = 0;
    size = sizeof (length);
    if (WinHttpQueryHeaders (request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
    {
        if (length == 0 || length > maximumManifestBytes) return false;
    }
    else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) return false;
    return true;
}
}

std::optional<juce::String> fetchOfficialManifestWindows (const std::atomic<bool>& cancelled)
{
    // Worker only. The checker reserves the persisted 24h budget BEFORE calling us.
    // Never resend on ERROR_WINHTTP_RESEND_REQUEST, auth, redirect, TLS or connection error.
    if (cancelled.load()) return {};
    const auto deadline = Clock::now() + totalTimeout;
    CallbackState state;
    Handles handles; // Destroy/drain before state; no callback survives this function/module.
    if (! handles.open() || ! handles.prepare (state) || cancelled.load() || Clock::now() >= deadline)
        return {};
    // Exactly one request submission. No loop, fallback, redirect or retry can reach this call.
    if (! WinHttpSendRequest (handles.request, L"Accept-Encoding: identity\r\n", static_cast<DWORD> (-1),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR> (&state))
        || ! waitFor (state, &CallbackState::sent, cancelled, deadline)
        || ! WinHttpReceiveResponse (handles.request, nullptr)
        || ! waitFor (state, &CallbackState::headers, cancelled, deadline)
        || ! validHeaders (handles.request)) return {};
    DWORD used = 0;
    for (;;)
    {
        if (cancelled.load() || Clock::now() >= deadline || used > maximumManifestBytes) return {};
        { const std::lock_guard<std::mutex> lock (state.mutex); state.read = false; state.readBytes = 0; }
        const auto capacity = static_cast<DWORD> (state.bytes.size()) - used;
        if (! WinHttpReadData (handles.request, state.bytes.data() + used, capacity, nullptr)
            || ! waitFor (state, &CallbackState::read, cancelled, deadline)) return {};
        DWORD amount = 0;
        { const std::lock_guard<std::mutex> lock (state.mutex); amount = state.readBytes; }
        if (amount > capacity) return {};
        if (amount == 0) break;
        used += amount;
    }
    if (used == 0 || used > maximumManifestBytes || cancelled.load() || Clock::now() >= deadline
        || std::memchr (state.bytes.data(), 0, used) != nullptr
        || ! juce::CharPointer_UTF8::isValidString (state.bytes.data(), static_cast<int> (used))) return {};
    return juce::String::fromUTF8 (state.bytes.data(), static_cast<int> (used));
}
}
#endif
