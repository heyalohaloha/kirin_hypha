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

static bool waitUntil (const std::function<bool()>& predicate,
                       std::chrono::milliseconds timeout = std::chrono::seconds (2))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
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
    std::optional<CaptureOwnerView> failure;
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
          [&] (const ExactCaptureRequest& exact, CaptureOwnerView failure)
              {
                  std::lock_guard<std::mutex> guard (transport.lock);
                  if (exact != request || failure.phase != CaptureOwnerPhase::failed)
                      return false;
                  transport.failure = failure;
                  return true;
              },
          {}, {}, {},
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
          {}, {},
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
          [&] (const ExactCaptureRequest& exact, CaptureOwnerView& failure)
              {
                  std::lock_guard<std::mutex> guard (transport.lock);
                  if (exact != request || ! transport.failure)
                      return false;
                  failure = *transport.failure;
                  return true;
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
#if JUCE_DEBUG
    const auto comparison = post.capturePairComparison();
    require (comparison.valid && ! comparison.exactAtZero
             && comparison.generation == request.captureGeneration
             && comparison.start == request.nativeStart
             && comparison.frames == request.frames);
#endif
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
    require (waitUntil ([&] { return post.view().phase == CaptureOwnerPhase::failed; }));
    require (post.view().failure == CaptureOwnerFailure::stalePair);
    post.requestReset();
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
          {}, {},
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
          {},
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
    require (waitUntil ([&] { return rejectingPost.view().phase == CaptureOwnerPhase::failed; }));
    require (rejectingPost.view().failure == CaptureOwnerFailure::receiptRejected);
    require (! rejectingPost.capturePairReady() && ! badReceiptAcknowledged.load());
    rejectingPost.requestReset();
    require (waitUntil ([&] { return rejectingPost.view().phase == CaptureOwnerPhase::idle; }));
    require (rejectingPost.reservePostRequest());
    rejectingPost.abandonPostRequest();
    rejectingPost.stop();

    // A completed POST also has a separate finalization bound when PRE returns neither success
    // nor failure. The request admission deadline remains unrelated to this wait.
    const ExactCaptureRequest absentPreRequest {
        "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
        { 66, "project-a", "pre-absent" }, 67, 68, 1, 0, 48000, 1,
        0, 12, juce::Time::currentTimeMillis() + 5'000
    };
    LocalBlindCaptureService absentPrePost (
        CaptureSide::post,
        { {}, {},
          [&] (const std::string& id) { return id == absentPreRequest.requestId; },
          [&] (ExactPairBinding& out) { out = absentPreRequest.pair; return true; },
          {}, {}, {}, {}, {}, {}, {} },
        250);
    absentPrePost.start (48000, 1);
    require (absentPrePost.reservePostRequest());
    require (absentPrePost.commitPostRequest (absentPreRequest));
    require (waitUntil ([&]
    {
        return absentPrePost.view().phase == CaptureOwnerPhase::capturing;
    }));
    require (absentPrePost.process (postPointers, 1, clockAt (0, 12), 48000));
    require (waitUntil ([&] { return absentPrePost.view().phase == CaptureOwnerPhase::failed; }));
    require (absentPrePost.view().failure == CaptureOwnerFailure::receiptRejected);
    absentPrePost.stop();

    // A PRE lane failure is a terminal result. POST must surface it instead of remaining in
    // `complete` forever while polling for PCM that can never be published.
    const ExactCaptureRequest failedRequest {
        "fedcba98-7654-4321-8765-abcdefabcdef",
        { 71, "project-a", "pre-failure" }, 72, 73, 1, 0, 48000, 1,
        0, 12, juce::Time::currentTimeMillis() + 5'000
    };
    std::mutex failureLock;
    std::optional<CaptureOwnerView> publishedFailure;
    LocalBlindCaptureService failingPre (
        CaptureSide::pre,
        { [&] (ExactCaptureRequest& out) { out = failedRequest; return true; },
          [&] (const std::string& id) { return id == failedRequest.requestId; },
          {}, {}, {},
          [&] (const ExactCaptureRequest& exact, CaptureOwnerView failure)
              {
                  std::lock_guard<std::mutex> guard (failureLock);
                  if (exact != failedRequest) return false;
                  publishedFailure = failure;
                  return true;
              },
          {}, {}, {}, {}, {} });
    failingPre.start (48000, 1);
    require (waitUntil ([&] { return failingPre.view().phase == CaptureOwnerPhase::capturing; }));
    require (failingPre.process (prePointers, 1, clockAt (0, 6), 48000));
    require (failingPre.process (prePointers, 1, clockAt (7, 5), 48000));
    require (waitUntil ([&]
    {
        std::lock_guard<std::mutex> guard (failureLock);
        return publishedFailure.has_value();
    }));

    LocalBlindCaptureService observingPost (
        CaptureSide::post,
        { {}, {},
          [&] (const std::string& id) { return id == failedRequest.requestId; },
          [&] (ExactPairBinding& out) { out = failedRequest.pair; return true; },
          {}, {}, {},
          [&] (const ExactCaptureRequest& exact, CaptureOwnerView& failure)
              {
                  std::lock_guard<std::mutex> guard (failureLock);
                  if (exact != failedRequest || ! publishedFailure) return false;
                  failure = *publishedFailure;
                  return true;
              },
          {}, {}, {} });
    observingPost.start (48000, 1);
    require (observingPost.reservePostRequest());
    require (observingPost.commitPostRequest (failedRequest));
    require (waitUntil ([&] { return observingPost.view().phase == CaptureOwnerPhase::failed; }));
    require (observingPost.view().failure == CaptureOwnerFailure::captureFailed);
    require (observingPost.view().captureFailure == CaptureFailure::clock);
    observingPost.stop();
    failingPre.stop();

    // A completed PRE whose immutable PCM cannot be published must also become terminal. This
    // bounds non-RT publication retries instead of leaving POST in `complete` indefinitely.
    const ExactCaptureRequest unpublishableRequest {
        "11111111-2222-4333-8444-555555555555",
        { 81, "project-a", "pre-unpublishable" }, 82, 83, 1, 0, 48000, 1,
        0, 12, juce::Time::currentTimeMillis() + 5'000
    };
    std::atomic<unsigned int> publishAttempts { 0 };
    std::atomic<bool> publicationFailurePublished { false };
    LocalBlindCaptureService unpublishablePre (
        CaptureSide::pre,
        { [&] (ExactCaptureRequest& out) { out = unpublishableRequest; return true; },
          [&] (const std::string& id) { return id == unpublishableRequest.requestId; },
          {}, {},
          [&] (const ExactCaptureRequest&, const CaptureReceipt&,
               const std::vector<float>&, std::string&)
              { publishAttempts.fetch_add (1); return false; },
          [&] (const ExactCaptureRequest& exact, CaptureOwnerView failure)
              {
                  const bool expected = exact == unpublishableRequest
                      && failure.phase == CaptureOwnerPhase::failed
                      && failure.failure == CaptureOwnerFailure::receiptRejected;
                  publicationFailurePublished.store (expected);
                  return expected;
              },
          {}, {}, {}, {}, {} });
    unpublishablePre.start (48000, 1);
    require (waitUntil ([&]
    {
        return unpublishablePre.view().phase == CaptureOwnerPhase::capturing;
    }));
    require (unpublishablePre.process (prePointers, 1, clockAt (0, 12), 48000));
    require (waitUntil ([&] { return publicationFailurePublished.load(); },
                        std::chrono::seconds (6)));
    require (publishAttempts.load() == 20);
    require (unpublishablePre.view().failure == CaptureOwnerFailure::receiptRejected);
    unpublishablePre.stop();

    std::cout << "Local Blind capture service: PASS (exact PRE transfer, terminal failure, pair barrier, rejection, retirement, stop race)\n";
}
