#include "UpdateTransport.h"
#include "UpdateHttpResponse.h"

#if JUCE_MAC
#import <Network/Network.h>
#import <Security/SecProtocolOptions.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string_view>

namespace hypha::update
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr auto totalTimeout = std::chrono::seconds (3);
static_assert (std::string_view (manifestEndpoint)
               == "https://kirinmastering.com/updates/hypha-stable.v1.json");
constexpr auto officialRequest = "GET /updates/hypha-stable.v1.json HTTP/1.1\r\n"
    "Host: kirinmastering.com\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";

struct CallbackState
{
    explicit CallbackState (const std::atomic<bool>& cancel) : cancelled (cancel) {}
    const std::atomic<bool>& cancelled;
    const Clock::time_point deadline = Clock::now() + totalTimeout;
    std::mutex mutex;
    std::condition_variable changed;
    HttpResponse response;
    nw_connection_t connection = nullptr;
    dispatch_queue_t queue = nullptr;
    dispatch_data_t request = nullptr;
    std::size_t outstandingSends = 0, outstandingReceives = 0, sends = 0, dataCallbacks = 0;
    bool sent = false, stopping = false, failed = false, done = false, connectionCancelled = false;
    bool callbacksDrained = false, handlesReleased = false;

    bool aborting() const { return cancelled.load() || Clock::now() >= deadline; }
};

// Called only on the private serial queue. Network.framework documents that
// cancelled is its LAST callback and outstanding send/receive callbacks have
// already delivered errors. The worker additionally verifies their counters.
void cancelConnection (CallbackState* state)
{
    {
        const std::lock_guard<std::mutex> lock (state->mutex);
        if (state->stopping) return;
        state->stopping = true;
    }
    nw_connection_cancel (state->connection);
}

void receive (CallbackState* state)
{
    {
        const std::lock_guard<std::mutex> lock (state->mutex);
        if (state->stopping || state->failed || state->done || state->aborting()) return;
        ++state->outstandingReceives;
    }
    // One bounded receive in flight. No whole-response collection, including
    // before the application sees the first byte or an unknown/chunked length.
    nw_connection_receive (state->connection, 1, 4096,
        ^(dispatch_data_t content, nw_content_context_t, bool end, nw_error_t error)
        {
            bool again = false;
            {
                const std::lock_guard<std::mutex> lock (state->mutex);
                --state->outstandingReceives;
                ++state->dataCallbacks;
                if (! state->stopping)
                {
                    if (content != nullptr)
                        dispatch_data_apply (content, ^bool (dispatch_data_t, size_t, const void* bytes, size_t length)
                        { return state->response.feed (bytes, length); });
                    state->failed = state->failed || error != nullptr || state->response.failed() || state->aborting();
                    if (end && ! state->failed) state->failed = ! state->response.finish();
                    state->done = ! state->failed && state->response.complete();
                    again = ! state->failed && ! state->done;
                }
                state->changed.notify_one();
            }
            if (again) receive (state);
            else cancelConnection (state);
        });
}

void sendOnce (CallbackState* state)
{
    {
        const std::lock_guard<std::mutex> lock (state->mutex);
        if (state->sent || state->stopping || state->failed || state->aborting()) return;
        state->sent = true;
        ++state->sends;
        ++state->outstandingSends;
    }
    // A normal completion callback is intentional. The idempotent fast-open
    // marker permits replay; it is never used. Only a ready connection sends.
    nw_connection_send (state->connection, state->request, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true,
        ^(nw_error_t error)
        {
            bool read = false;
            {
                const std::lock_guard<std::mutex> lock (state->mutex);
                --state->outstandingSends;
                state->failed = state->failed || error != nullptr || state->aborting();
                read = ! state->failed && ! state->stopping;
                state->changed.notify_one();
            }
            if (read) receive (state);
            else cancelConnection (state);
        });
}

class NativeRequest
{
public:
    explicit NativeRequest (CallbackState& callback) : state (callback) {}
    ~NativeRequest()
    {
        if (started)
        {
            auto* active = &state;
            dispatch_sync (state.queue, ^{ cancelConnection (active); });
            {
                std::unique_lock<std::mutex> lock (state.mutex);
                state.changed.wait (lock, [&]
                { return state.connectionCancelled && state.outstandingSends == 0 && state.outstandingReceives == 0; });
            }
            // The final state callback may still be returning when it signals.
            // A serial queue barrier waits for its code to finish, then removes
            // the block that references this module before releasing handles.
            dispatch_sync (state.queue, ^{ nw_connection_set_state_changed_handler (active->connection, nullptr); });
            state.callbacksDrained = true;
        }
        if (state.connection != nullptr) nw_release (state.connection);
        if (state.request != nullptr) dispatch_release (state.request);
        if (state.queue != nullptr) dispatch_release (state.queue);
        state.connection = nullptr; state.request = nullptr; state.queue = nullptr;
        state.handlesReleased = true;
    }

    bool start (const char* host, const char* port, const char* request, bool tls)
    {
        state.queue = dispatch_queue_create ("KirinHyphaUpdateTransport", DISPATCH_QUEUE_SERIAL);
        if (state.queue == nullptr) return false;
        auto* endpoint = nw_endpoint_create_host (host, port);
        if (endpoint == nullptr) return false;
        const auto configureTLS = ^(nw_protocol_options_t options)
        {
            auto* secure = nw_tls_copy_sec_protocol_options (options);
            sec_protocol_options_set_tls_server_name (secure, "kirinmastering.com");
            sec_protocol_options_set_peer_authentication_required (secure, true);
            sec_protocol_options_set_min_tls_protocol_version (secure, tls_protocol_version_TLSv12);
            sec_protocol_options_add_tls_application_protocol (secure, "http/1.1");
            sec_protocol_options_set_tls_tickets_enabled (secure, false);
            sec_protocol_options_set_tls_resumption_enabled (secure, false);
            sec_protocol_options_set_tls_false_start_enabled (secure, false);
            nw_release (secure);
        };
        auto* parameters = nw_parameters_create_secure_tcp (tls ? configureTLS : NW_PARAMETERS_DISABLE_PROTOCOL,
            ^(nw_protocol_options_t options)
            {
                nw_tcp_options_set_enable_fast_open (options, false);
                nw_tcp_options_set_enable_keepalive (options, false);
            });
        if (parameters == nullptr) { nw_release (endpoint); return false; }
        nw_parameters_set_fast_open_enabled (parameters, false);
        state.connection = nw_connection_create (endpoint, parameters);
        nw_release (parameters);
        nw_release (endpoint);
        if (state.connection == nullptr) return false;
        state.request = dispatch_data_create (request, std::strlen (request), state.queue, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        if (state.request == nullptr) return false;
        auto* active = &state;
        nw_connection_set_queue (state.connection, state.queue);
        nw_connection_set_state_changed_handler (state.connection, ^(nw_connection_state_t phase, nw_error_t)
        {
            if (phase == nw_connection_state_ready) { sendOnce (active); return; }
            bool cancel = false;
            {
                const std::lock_guard<std::mutex> lock (active->mutex);
                if (phase == nw_connection_state_cancelled) active->connectionCancelled = true;
                if (phase == nw_connection_state_failed || phase == nw_connection_state_waiting)
                {
                    active->failed = true;
                    cancel = true; // Waiting must not retry on a later network change.
                }
                active->changed.notify_one();
            }
            if (cancel) cancelConnection (active);
        });
        started = true;
        nw_connection_start (state.connection);
        return true;
    }
private:
    CallbackState& state;
    bool started = false;
};

std::optional<juce::String> fetch (CallbackState& state, const char* host, const char* port,
                                 const char* requestBytes, bool tls)
{
    if (state.cancelled.load()) return {};
    bool accepted = false;
    {
        NativeRequest request (state);
        if (request.start (host, port, requestBytes, tls))
        {
            std::unique_lock<std::mutex> lock (state.mutex);
            while (! state.done && ! state.failed && ! state.aborting())
                state.changed.wait_until (lock, std::min (state.deadline, Clock::now() + std::chrono::milliseconds (50)));
            accepted = state.done && ! state.failed && ! state.aborting();
        }
    }
    if (! accepted || state.cancelled.load() || state.response.size() == 0
        || std::memchr (state.response.data(), 0, state.response.size()) != nullptr
        || ! juce::CharPointer_UTF8::isValidString (state.response.data(), static_cast<int> (state.response.size()))) return {};
    return juce::String::fromUTF8 (state.response.data(), static_cast<int> (state.response.size()));
}
}

std::optional<juce::String> fetchOfficialManifestMac (const std::atomic<bool>& cancelled)
{
    CallbackState state (cancelled);
    return fetch (state, "kirinmastering.com", "443", officialRequest, true);
}

#if defined (HYPHA_UPDATE_TRANSPORT_TESTING)
std::optional<juce::String> fetchManifestMacForTest (const std::atomic<bool>& cancelled,
                                                 unsigned short loopbackPort, MacTransportProbe& probe)
{
    CallbackState state (cancelled);
    const auto port = juce::String (loopbackPort);
    const auto request = "GET /manifest HTTP/1.1\r\nHost: 127.0.0.1:" + port
                      + "\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    auto result = fetch (state, "127.0.0.1", port.toRawUTF8(), request.toRawUTF8(), false);
    probe = { state.response.size(), state.response.headerSize(), state.dataCallbacks, state.sends,
              state.connectionCancelled, state.callbacksDrained, state.handlesReleased };
    return result;
}
#endif
}
#endif
