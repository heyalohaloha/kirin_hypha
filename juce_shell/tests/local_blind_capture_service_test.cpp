#include "../src/local_blind/LocalBlindCaptureService.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

using namespace hypha::local_blind;
static void require (bool value) { if (! value) std::abort(); }

static CaptureClockObservation clockAt (std::int64_t position, int frames)
{
    return { position, frames, 1, 1, 0, 96, true, true, false, true, false, true };
}

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

struct MockPreTransport
{
    std::mutex lock;
    std::optional<CaptureReceipt> receipt;
    std::vector<float> pcm;
    const std::string sha256 = std::string (64, 'a');
    std::atomic<bool> consumed { false };
    std::atomic<bool> retired { false };
};

int main()
{
    const auto now = juce::Time::currentTimeMillis();
    const ExactCaptureRequest request {
        "12345678-1234-4234-8234-123456789abc",
        { 51, "project-a", "pre-unnamed" }, 52, 53, 1, 0, 48000, 1,
        0, 12, now + 5'000
    };
    std::atomic<bool> preRequestAvailable { true };
    std::atomic<bool> preAcknowledged { false };
    std::atomic<std::uint64_t> pairGeneration { request.pair.generation };
    MockPreTransport transport;

    LocalBlindCaptureService pre (
        CaptureSide::pre,
        { [&] (ExactCaptureRequest& out)
              {
                  if (! preRequestAvailable.load()) return false;
                  out = request;
                  return true;
              },
          [&] (const std::string& requestId)
              {
                  const bool matches = requestId == request.requestId;
                  preAcknowledged.store (matches);
                  return matches;
              },
          {}, {},
          [&] (const ExactCaptureRequest& exact, const CaptureReceipt& receipt,
               const std::vector<float>& pcm, std::string& sha256)
              {
                  std::lock_guard<std::mutex> guard (transport.lock);
                  if (exact != request || receipt.side != CaptureSide::pre)
                      return false;
                  transport.receipt = receipt;
                  transport.pcm = pcm;
                  sha256 = transport.sha256;
                  return true;
              },
          {}, {},
          [&] (const ExactCaptureRequest& exact, const std::string& sha256)
              { return exact == request && sha256 == transport.sha256
                    && transport.consumed.load(); },
          [&] (const ExactCaptureRequest& exact)
              { transport.retired.store (exact == request); } });

    LocalBlindCaptureService post (
        CaptureSide::post,
        { {}, {},
          [&] (const std::string& requestId)
              { return preAcknowledged.load() && requestId == request.requestId; },
          [&] (ExactPairBinding& out)
              {
                  out = request.pair;
                  out.generation = pairGeneration.load();
                  return true;
              },
          {},
          [&] (const ExactCaptureRequest& exact,
               CaptureServiceHooks::ImportedPreCapture& imported)
              {
                  std::lock_guard<std::mutex> guard (transport.lock);
                  if (exact != request || ! transport.receipt)
                      return false;
                  imported.receipt = *transport.receipt;
                  imported.capture = ExactRangeCapture::fromCompletedInterleaved (
                      imported.receipt.range, transport.pcm,
                      transport.pcm.size() * sizeof (float));
                  imported.pcmSha256 = transport.sha256;
                  return imported.capture != nullptr;
              },
          [&] (const ExactCaptureRequest& exact, const std::string& sha256)
              {
                  const bool matches = exact == request && sha256 == transport.sha256;
                  transport.consumed.store (matches);
                  return matches;
              },
          {}, {} });

    pre.start (48000, 1);
    post.start (48000, 1);
    require (waitUntil ([&] { return preAcknowledged.load(); }));
    require (pre.view().phase == CaptureOwnerPhase::capturing);
    require (post.reservePostRequest());
    require (! post.reservePostRequest());
    require (post.commitPostRequest (request));
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::capturing; }));

    std::array<float, 12> preInput { 0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f,
                                    0.6f, 0.7f, 0.8f, 0.9f, 1.0f, -1.0f };
    std::array<float, 12> postInput { -0.4f, 0.3f, 0.2f, -0.1f, 0.0f, 0.1f,
                                     0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f };
    const float* prePointers[] = { preInput.data() };
    const float* postPointers[] = { postInput.data() };
    require (pre.process (prePointers, 1, clockAt (0, 12), 48000));
    require (post.process (postPointers, 1, clockAt (0, 12), 48000));
    require (waitUntil ([&] { return post.capturePairReady(); }));
    require (post.view().phase == CaptureOwnerPhase::paired);
    require (waitUntil ([&] { return pre.view().phase == CaptureOwnerPhase::retired; }));
    require (transport.consumed.load() && transport.retired.load());
    {
        std::lock_guard<std::mutex> guard (transport.lock);
        require (transport.pcm == std::vector<float> (preInput.begin(), preInput.end()));
    }

    // Completion remains bound to the exact pair. A later generation invalidates it and releases
    // the one-request owner rather than silently re-targeting the captured PCM.
    pairGeneration.fetch_add (1);
    preRequestAvailable.store (false);
    require (waitUntil ([&] { return ! post.capturePairReady(); }));
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::idle; }));
    require (post.reservePostRequest());
    post.abandonPostRequest();

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

    const ExactCaptureRequest rejectedRequest {
        "abcdefab-1234-4234-8234-123456789abc",
        { 61, "project-a", "pre-unnamed" }, 62, 63, 1, 41, 48000, 1,
        41, 12, juce::Time::currentTimeMillis() + 5'000
    };
    std::atomic<bool> badReceiptRead { false };
    std::atomic<bool> badReceiptAcknowledged { false };
    LocalBlindCaptureService rejectingPost (
        CaptureSide::post,
        { {}, {},
          [&] (const std::string& requestId)
              { return requestId == rejectedRequest.requestId; },
          [&] (ExactPairBinding& out)
              {
                  out = rejectedRequest.pair;
                  return true;
              },
          {},
          [&] (const ExactCaptureRequest& exact,
               CaptureServiceHooks::ImportedPreCapture& imported)
              {
                  if (exact != rejectedRequest)
                      return false;
                  imported.receipt = {
                      exact.pair, exact.clockGeneration + 1, CaptureSide::pre,
                      { exact.captureGeneration, exact.sampleRate, exact.channels,
                        exact.nativeStart, exact.frames },
                      CaptureState::complete, CaptureFailure::none
                  };
                  imported.capture = ExactRangeCapture::fromCompletedInterleaved (
                      imported.receipt.range, std::vector<float> (12, 0.25f),
                      12 * sizeof (float));
                  imported.pcmSha256 = std::string (64, 'b');
                  badReceiptRead.store (true);
                  return imported.capture != nullptr;
              },
          [&] (const ExactCaptureRequest&, const std::string&)
              {
                  badReceiptAcknowledged.store (true);
                  return true;
              },
          {}, {} });
    rejectingPost.start (48000, 1);
    require (rejectingPost.reservePostRequest());
    require (rejectingPost.commitPostRequest (rejectedRequest));
    require (waitUntil ([&]
    {
        return rejectingPost.view().phase == CaptureOwnerPhase::capturing;
    }));
    require (rejectingPost.process (postPointers, 1, clockAt (41, 12), 48000));
    require (waitUntil ([&] { return badReceiptRead.load(); }));
    require (waitUntil ([&] { return rejectingPost.view().phase == CaptureOwnerPhase::idle; }));
    require (! rejectingPost.capturePairReady() && ! badReceiptAcknowledged.load());
    require (rejectingPost.reservePostRequest());
    rejectingPost.abandonPostRequest();
    rejectingPost.stop();

    std::cout << "Local Blind capture service: PASS (exact PRE transfer, pair barrier, rejection, retirement, stop race)\n";
}
