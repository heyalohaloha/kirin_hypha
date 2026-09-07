#include "../src/local_blind/LocalBlindCaptureService.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

using namespace hypha::local_blind;
static void require (bool value) { if (! value) std::abort(); }

static bool waitUntil (const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return predicate();
}

int main()
{
    const auto now = juce::Time::currentTimeMillis();
    const ExactCaptureRequest preRequest {
        "12345678-1234-4234-8234-123456789abc",
        { 51, "project-a", "pre-unnamed" }, 52, 53, 48000, 1,
        0, 31, 12, now + 5'000
    };
    std::atomic<bool> preRequestAvailable { true };
    std::atomic<bool> preAcknowledged { false };
    LocalBlindCaptureService pre (
        CaptureSide::pre,
        { [&] (ExactCaptureRequest& out)
              {
                  if (! preRequestAvailable.load()) return false;
                  out = preRequest;
                  return true;
              },
          [&] (const std::string& requestId)
              {
                  const bool matches = requestId == preRequest.requestId;
                  preAcknowledged.store (matches);
                  return matches;
              },
          {}, {} });
    pre.start (48000, 1);
    require (waitUntil ([&] { return preAcknowledged.load(); }));
    require (pre.view().phase == CaptureOwnerPhase::capturing);

    std::array<float, 12> input { 0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f,
                                 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, -1.0f };
    const float* pointers[] = { input.data() };
    require (pre.process (pointers, 1, 12, 0, true, true, false, true, 48000));
    require (waitUntil ([&] { return pre.view().phase == CaptureOwnerPhase::complete; }));
    preRequestAvailable.store (false);
    require (waitUntil ([&] { return pre.view().phase == CaptureOwnerPhase::idle; }));

    const ExactCaptureRequest postRequest {
        "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", preRequest.pair, 54, 55, 48000, 1,
        0, 31, 12, juce::Time::currentTimeMillis() + 5'000
    };
    std::atomic<bool> peerArmed { false };
    LocalBlindCaptureService post (
        CaptureSide::post,
        { {}, {},
          [&] (const std::string& requestId)
              { return peerArmed.load() && requestId == postRequest.requestId; },
          [&] (ExactPairBinding& out)
              {
                  out = postRequest.pair;
                  return true;
              } });
    post.start (48000, 1);
    require (post.reservePostRequest());
    require (! post.reservePostRequest());
    require (post.commitPostRequest (postRequest));
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::awaitingPeer; }));
    peerArmed.store (true);
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::capturing; }));
    require (post.process (pointers, 1, 12, 31, true, true, false, true, 48000));
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::complete; }));

    std::atomic<bool> wakeRunning { true };
    std::thread concurrentWake ([&]
    {
        while (wakeRunning.load())
            post.requestReset();
    });
    post.stop();
    wakeRunning.store (false);
    concurrentWake.join();
    pre.stop();
    require (! post.running() && ! pre.running());
    std::cout << "Local Blind capture service: PASS (shared scheduler, PRE ack, POST arm, stop race)\n";
}
