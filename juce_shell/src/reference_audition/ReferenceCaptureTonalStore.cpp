#include "ReferenceCaptureTonalStore.h"

#include "ReferenceRuntimeEventTransport.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceWorkflowStorage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace hypha::reference_audition
{
namespace
{
using namespace runtime_repository_parsing;
constexpr std::int64_t artifactLimit = 16 * 1024 * 1024;
constexpr int magic = 0x43544131;
constexpr auto method = "kirin_multires_relative_power.v1";

int planeForBand (int band)
{
    const auto center = 20.0 * std::pow (1000.0, (static_cast<double> (band) + 0.5) / 60.0);
    return center < 160.0 ? 0 : center < 640.0 ? 1 : 2;
}

float percentileDb (std::vector<double>& power, double quantile)
{
    std::sort (power.begin(), power.end());
    const auto at = quantile * static_cast<double> (power.size() - 1);
    const auto lower = static_cast<size_t> (std::floor (at));
    const auto upper = static_cast<size_t> (std::ceil (at));
    const auto value = power[lower] + (power[upper] - power[lower]) * (at - lower);
    return static_cast<float> (10.0 * std::log10 (value));
}

bool flushFile (const juce::File& file)
{
   #if JUCE_WINDOWS
    const auto handle = ::CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = ::FlushFileBuffers (handle) != 0;
    ::CloseHandle (handle);
    return ok;
   #else
    const auto handle = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY);
    if (handle < 0) return false;
    const bool ok = ::fsync (handle) == 0;
    ::close (handle);
    return ok;
   #endif
}

bool validReceipt (const CaptureTonalSummary& value)
{
    return value.valid() && sha256 (value.artifactSha256)
        && sha256 (value.recoveryKey) && value.artifactBytes > 0
        && value.artifactBytes <= artifactLimit;
}

juce::File artifactFile (const juce::File& root, const juce::String& sha)
{
    return root.getChildFile ("capture-tonal-v1").getChildFile ("artifacts")
        .getChildFile (sha + ".bin");
}
}

CaptureTonalSummary storeCaptureTonalArtifact (
    const juce::File& transportRoot, const CaptureTonalArtifactInput& input,
    const CaptureTonalSummary& wholeCapture)
{
    CaptureTonalSummary failed;
    if (! wholeCapture.valid() || ! safeUuid (input.captureId)
        || ! sha256 (input.baseCaptureSha256) || input.values == nullptr
        || input.sampleRate != wholeCapture.sampleRate || input.channels != wholeCapture.channels
        || input.frames != wholeCapture.frames) return failed;
    const auto recoverySeed = input.captureId + ":" + input.baseCaptureSha256 + ":" + method;
    const auto recoveryKey = juce::SHA256 (
        recoverySeed.toRawUTF8(), static_cast<size_t> (recoverySeed.getNumBytesAsUTF8())).toHexString();
    const auto directory = transportRoot.getChildFile ("capture-tonal-v1").getChildFile ("artifacts");
    if (! directory.createDirectory() || directory.isSymbolicLink()) return failed;
    const auto temporary = directory.getNonexistentChildFile (".capture-tonal", ".tmp", false);
    {
        auto output = temporary.createOutputStream();
        if (output == nullptr || ! output->openedOk()) return failed;
        output->writeInt (magic);
        output->writeInt (2);
        output->writeString (input.captureId);
        output->writeString (input.baseCaptureSha256);
        output->writeString (method);
        output->writeInt (input.sampleRate);
        output->writeInt (input.channels);
        output->writeInt64 (static_cast<juce::int64> (input.frames));
        output->writeInt64 (input.hostStart);
        for (int plane = 0; plane < 3; ++plane)
        {
            std::vector<int> bands;
            for (int band = 0; band < 60; ++band)
                if (planeForBand (band) == plane) bands.push_back (band);
            const auto windows = input.values->at (static_cast<size_t> (bands.front())).size();
            if (input.fftSize[size_t (plane)] < 256 || input.hopSamples[size_t (plane)] < 1
                || windows > 72'000) { temporary.deleteFile(); return failed; }
            for (const auto band : bands)
                if (input.values->at (static_cast<size_t> (band)).size() != windows)
                { temporary.deleteFile(); return failed; }
            output->writeInt (static_cast<int> (input.fftSize[size_t (plane)]));
            output->writeInt (static_cast<int> (input.hopSamples[size_t (plane)]));
            output->writeInt64 (static_cast<juce::int64> (windows));
            output->writeInt (static_cast<int> (bands.size()));
            for (const auto band : bands) output->writeByte (static_cast<char> (band));
            const auto cells = windows * bands.size();
            std::vector<std::uint8_t> validity ((cells + 7) / 8, 0);
            for (size_t window = 0; window < windows; ++window)
                for (size_t localBand = 0; localBand < bands.size(); ++localBand)
                {
                    const auto band = bands[localBand];
                    const auto value = input.values->at (static_cast<size_t> (band))[window];
                    const bool valid = std::isfinite (value) && value >= -120.0f && value <= 0.001f;
                    const auto cell = window * bands.size() + localBand;
                    if (valid) validity[cell / 8] |= static_cast<std::uint8_t> (1u << (cell % 8));
                }
            output->write (validity.data(), validity.size());
            for (size_t window = 0; window < windows; ++window)
                for (const auto band : bands)
                {
                    const auto value = input.values->at (static_cast<size_t> (band))[window];
                    output->writeFloat (std::isfinite (value) && value >= -120.0f
                        && value <= 0.001f ? value : 0.0f);
                }
        }
        output->flush();
        if (output->getStatus().failed()) { output.reset(); temporary.deleteFile(); return failed; }
    }
    if (temporary.getSize() < 1 || temporary.getSize() > artifactLimit || ! flushFile (temporary))
    { temporary.deleteFile(); return failed; }
    const auto hash = juce::SHA256 (temporary).toHexString();
    const auto target = artifactFile (transportRoot, hash);
    if (target.existsAsFile())
    {
        if (target.isSymbolicLink() || target.getSize() != temporary.getSize()
            || juce::SHA256 (target).toHexString() != hash)
        { temporary.deleteFile(); return failed; }
        temporary.deleteFile();
    }
    else if (! temporary.moveFileTo (target)) { temporary.deleteFile(); return failed; }

    CaptureTonalSummary receipt = wholeCapture;
    receipt.artifactSha256 = hash;
    receipt.recoveryKey = recoveryKey;
    receipt.artifactBytes = target.getSize();
    const auto verified = loadCaptureTonalArtifact (
        transportRoot, receipt, input.captureId, 0, input.frames);
    if (! validReceipt (verified) || verified.validBits != wholeCapture.validBits) return failed;

    auto artifact = new juce::DynamicObject();
    artifact->setProperty ("relative_path",
        "capture-tonal-v1/artifacts/" + hash + ".bin");
    artifact->setProperty ("sha256", hash);
    artifact->setProperty ("bytes", receipt.artifactBytes);
    auto head = new juce::DynamicObject();
    head->setProperty ("format", "kirin_hypha_capture_tonal_recovery");
    head->setProperty ("version", "1.0");
    head->setProperty ("capture_id", input.captureId);
    head->setProperty ("base_capture_sha256", input.baseCaptureSha256);
    head->setProperty ("method_version", method);
    head->setProperty ("artifact", juce::var (artifact));
   #if JUCE_WINDOWS
    head->setProperty ("durability_tier", "process_crash_safe");
   #else
    head->setProperty ("durability_tier", "power_loss_safe");
   #endif
    head->setProperty ("updated_at", juce::Time::getCurrentTime().toISO8601 (true));
    const auto body = RuntimeEventTransport::canonicalJson (juce::var (head));
    const auto recovery = transportRoot.getChildFile ("capture-tonal-v1").getChildFile ("recovery")
        .getChildFile (recoveryKey).getChildFile ("head.json");
    if (! writeWorkflowDurable (transportRoot, recovery, body, true)) return failed;
    return receipt;
}

CaptureTonalSummary loadCaptureTonalArtifact (
    const juce::File& transportRoot, const CaptureTonalSummary& receipt,
    const juce::String& captureId, std::uint64_t rangeStart, std::uint64_t rangeEnd,
    const std::function<bool()>& cancelled)
{
    CaptureTonalSummary result;
    if (! validReceipt (receipt) || ! safeUuid (captureId) || rangeEnd <= rangeStart
        || rangeEnd > receipt.frames) return result;
    const auto file = artifactFile (transportRoot, receipt.artifactSha256);
    if (! file.isAChildOf (transportRoot) || file.isSymbolicLink()
        || file.getSize() != receipt.artifactBytes
        || juce::SHA256 (file).toHexString() != receipt.artifactSha256) return result;
    auto input = file.createInputStream();
    if (input == nullptr || input->readInt() != magic)
        return result;
    const auto version = input->readInt();
    if ((version != 1 && version != 2)
        || input->readString() != captureId || ! sha256 (input->readString())
        || input->readString() != method) return result;
    result.sampleRate = input->readInt();
    result.channels = input->readInt();
    result.frames = static_cast<std::uint64_t> (input->readInt64());
    input->readInt64();
    if (result.sampleRate != receipt.sampleRate || result.channels != receipt.channels
        || result.frames != receipt.frames) return {};
    std::array<std::vector<double>, 60> powers;
    for (int plane = 0; plane < 3; ++plane)
    {
        const auto fft = input->readInt();
        const auto hop = input->readInt();
        const auto windows = input->readInt64();
        const auto bandCount = input->readInt();
        if (fft < 256 || fft > 262'144 || hop < 1 || windows < 0 || windows > 72'000
            || bandCount < 1 || bandCount > 60) return {};
        std::vector<int> bands (static_cast<size_t> (bandCount));
        for (auto& band : bands)
        {
            band = static_cast<unsigned char> (input->readByte());
            if (band < 0 || band >= 60 || planeForBand (band) != plane) return {};
        }
        const auto cells = static_cast<std::uint64_t> (windows)
            * static_cast<std::uint64_t> (bandCount);
        std::vector<std::uint8_t> validity;
        if (version == 2)
        {
            const auto validityBytes = static_cast<size_t> ((cells + 7) / 8);
            if (cells > std::uint64_t (std::numeric_limits<std::int64_t>::max() / 4)
                || input->getNumBytesRemaining()
                    < static_cast<std::int64_t> (validityBytes + static_cast<size_t> (cells) * 4)) return {};
            validity.resize (validityBytes);
            const auto validityBytesInt = static_cast<int> (validityBytes);
            if (input->read (validity.data(), validityBytesInt) != validityBytesInt) return {};
            if (cells % 8 != 0 && (validity.back() & static_cast<std::uint8_t> (0xffu << (cells % 8))) != 0)
                return {};
        }
        for (std::int64_t window = 0; window < windows; ++window)
        {
            if ((window & 255) == 0 && cancelled && cancelled()) return {};
            const auto windowEnd = static_cast<std::uint64_t> (fft)
                + static_cast<std::uint64_t> (window) * static_cast<std::uint64_t> (hop);
            const bool inside = windowEnd >= static_cast<std::uint64_t> (fft)
                && windowEnd - static_cast<std::uint64_t> (fft) >= rangeStart
                && windowEnd <= rangeEnd;
            for (size_t localBand = 0; localBand < bands.size(); ++localBand)
            {
                const auto band = bands[localBand];
                const auto value = input->readFloat();
                const auto cell = static_cast<size_t> (window) * bands.size() + localBand;
                const bool valid = version == 2
                    ? (validity[cell / 8] & static_cast<std::uint8_t> (1u << (cell % 8))) != 0
                    : input->readBool();
                if ((! valid && value != 0.0f) || (valid && (! std::isfinite (value)
                    || value < -120.0f || value > 0.001f))) return {};
                if (inside && valid)
                    powers[size_t (band)].push_back (
                        std::pow (10.0, static_cast<double> (value) / 10.0));
            }
        }
    }
    if (! input->isExhausted()) return {};
    for (size_t band = 0; band < powers.size(); ++band)
        if (! powers[band].empty())
        {
            result.p10[band] = percentileDb (powers[band], 0.10);
            result.median[band] = percentileDb (powers[band], 0.50);
            result.p90[band] = percentileDb (powers[band], 0.90);
            result.validBits |= std::uint64_t (1) << band;
        }
    result.artifactSha256 = receipt.artifactSha256;
    result.recoveryKey = receipt.recoveryKey;
    result.artifactBytes = receipt.artifactBytes;
    return result.valid() ? result : CaptureTonalSummary {};
}
}
