#include "CaptureWorkAttachment.h"

#include <set>

#include <juce_cryptography/juce_cryptography.h>

#include "pre_display/PreDisplayProtocol.h"

namespace hypha::capture
{
namespace
{
constexpr std::int64_t requestLeaseMs = 60 * 1000;
constexpr std::int64_t maximumRequestBytes = 16 * 1024;
constexpr std::size_t maximumCaptureBytes = 16 * 1024 * 1024;

bool supportedDimensions (int width, int height) noexcept
{
    return (width == 1'200 && height == 630)
        || (width == 1'080 && height == 1'080)
        || (width == 1'080 && height == 1'350);
}

bool supportedDomain (const juce::String& value)
{
    return value == "level" || value == "time"
        || value == "frequency" || value == "space";
}

bool supportedTarget (const juce::String& value)
{
    return value == "absolute" || value == "delta";
}

bool validReference (const pre_display::WorkReference& value)
{
    return value.valid() && value.targetRole == pre_display::GuideTargetRole::post
        && pre_display::safeId (value.workId) && pre_display::safeId (value.bindingId)
        && pre_display::safeId (value.runtimeInstanceId);
}

bool exactProperties (const juce::DynamicObject& object,
                      std::initializer_list<const char*> names)
{
    std::set<std::string> expected;
    for (const auto* name : names)
        expected.emplace (name);
    const auto& properties = object.getProperties();
    if (properties.size() != static_cast<int> (expected.size()))
        return false;
    for (int index = 0; index < properties.size(); ++index)
        if (expected.find (properties.getName (index).toString().toStdString()) == expected.end())
            return false;
    return true;
}

bool supportedRejectionCode (const juce::String& value)
{
    return value == "artifact_invalid" || value == "destination_write_failed"
        || value == "os_authority_unavailable" || value == "request_expired"
        || value == "request_invalid" || value == "work_binding_changed"
        || value == "work_not_found" || value == "work_record_invalid"
        || value == "work_write_failed" || value == "workspace_unavailable";
}

bool writeMemoryAtomically (const juce::File& target, const juce::MemoryBlock& bytes)
{
    if (! target.getParentDirectory().createDirectory())
        return false;
    juce::TemporaryFile temporary (target);
    {
        auto stream = temporary.getFile().createOutputStream();
        if (stream == nullptr || ! stream->openedOk()
            || ! stream->write (bytes.getData(), bytes.getSize()))
            return false;
        stream->flush();
        if (stream->getStatus().failed())
            return false;
    }
    return temporary.overwriteTargetFileWithTemporary();
}

bool writeJsonAtomically (const juce::File& target, const juce::var& value)
{
    const auto json = juce::JSON::toString (value, true) + "\n";
    const juce::MemoryBlock bytes (json.toRawUTF8(), static_cast<std::size_t> (json.getNumBytesAsUTF8()));
    return writeMemoryAtomically (target, bytes);
}

juce::var readBoundedJson (const juce::File& file)
{
    if (! file.existsAsFile() || file.isSymbolicLink())
        return {};
    const auto size = file.getSize();
    if (size <= 0 || size > maximumRequestBytes)
        return {};
    auto stream = file.createInputStream();
    if (stream == nullptr || ! stream->openedOk())
        return {};
    juce::MemoryBlock bytes;
    if (stream->readIntoMemoryBlock (bytes, maximumRequestBytes + 1)
            != static_cast<std::size_t> (size)
        || bytes.getSize() != static_cast<std::size_t> (size))
        return {};
    return juce::JSON::parse (juce::String::fromUTF8 (
        static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize())));
}
}

struct WorkAttachmentController::Job
{
    pre_display::WorkReference work;
    juce::MemoryBlock png;
    WorkAttachmentDescriptor descriptor;
    juce::String requestId;
    std::int64_t requestedAtMs = 0;
    std::int64_t expiresAtMs = 0;
    bool published = false;
};

bool WorkAttachmentDescriptor::valid() const noexcept
{
    return supportedDimensions (pixelWidth, pixelHeight)
        && supportedDomain (domain) && supportedTarget (observationTarget)
        && capturedAtMs > 0;
}

WorkAttachmentController::WorkAttachmentController (juce::File transportRootIn)
    : juce::Thread ("Kirin Hypha CAPTURE attachment"), root (std::move (transportRootIn))
{
}

WorkAttachmentController::~WorkAttachmentController()
{
    signalThreadShouldExit();
    notify();
    if (! stopThread (-1))
        jassertfalse;
}

WorkAttachmentSubmit WorkAttachmentController::submit (
    const pre_display::WorkReference& expectedWork,
    juce::MemoryBlock pngBytes,
    WorkAttachmentDescriptor descriptor)
{
    if (! validReference (expectedWork))
        return WorkAttachmentSubmit::invalidReference;
    if (! descriptor.valid() || pngBytes.getSize() < 32
        || pngBytes.getSize() > maximumCaptureBytes || root.getFullPathName().isEmpty())
        return WorkAttachmentSubmit::invalidCapture;
    {
        const juce::ScopedLock lock (stateLock);
        if (inFlight || completion.terminal())
            return WorkAttachmentSubmit::busy;
        auto job = std::make_unique<Job>();
        job->work = expectedWork;
        job->png = std::move (pngBytes);
        job->descriptor = std::move (descriptor);
        pending = std::move (job);
        completion = {};
        inFlight = true;
    }
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    notify();
    return WorkAttachmentSubmit::accepted;
}

WorkAttachmentResult WorkAttachmentController::takeResult()
{
    const juce::ScopedLock lock (stateLock);
    auto result = completion;
    completion = {};
    return result;
}

bool WorkAttachmentController::publish (Job& job)
{
    job.requestId = juce::Uuid().toString();
    job.requestedAtMs = juce::Time::currentTimeMillis();
    job.expiresAtMs = job.requestedAtMs + requestLeaseMs;
    const auto artifactFile = job.requestId + ".png";
    const auto artifact = root.getChildFile ("artifacts").getChildFile (artifactFile);
    if (! writeMemoryAtomically (artifact, job.png))
        return false;

    auto request = new juce::DynamicObject();
    request->setProperty ("format", "kirin_hypha_capture_attachment_request");
    request->setProperty ("version", "1.0");
    request->setProperty ("intent", "attach_to_work");
    request->setProperty ("user_action", "capture_attach");
    request->setProperty ("request_id", job.requestId);
    request->setProperty ("target_role", "post");
    request->setProperty ("work_id", job.work.workId);
    request->setProperty ("binding_id", job.work.bindingId);
    request->setProperty ("runtime_instance_id", job.work.runtimeInstanceId);
    request->setProperty ("artifact_file", artifactFile);
    request->setProperty ("artifact_sha256",
                          juce::SHA256 (job.png.getData(), job.png.getSize()).toHexString());
    request->setProperty ("byte_length", static_cast<juce::int64> (job.png.getSize()));
    request->setProperty ("pixel_width", job.descriptor.pixelWidth);
    request->setProperty ("pixel_height", job.descriptor.pixelHeight);
    request->setProperty ("capture_domain", job.descriptor.domain);
    request->setProperty ("observation_target", job.descriptor.observationTarget);
    request->setProperty ("captured_at_ms", static_cast<juce::int64> (job.descriptor.capturedAtMs));
    request->setProperty ("requested_at_ms", static_cast<juce::int64> (job.requestedAtMs));
    request->setProperty ("expires_at_ms", static_cast<juce::int64> (job.expiresAtMs));
    const auto requestFile = root.getChildFile ("requests").getChildFile (job.requestId + ".json");
    if (! writeJsonAtomically (requestFile, juce::var (request)))
    {
        artifact.deleteFile();
        return false;
    }
    job.png.reset();
    job.published = true;
    return true;
}

WorkAttachmentResult WorkAttachmentController::readReceipt (const Job& job) const
{
    const auto receiptFile = root.getChildFile ("receipts").getChildFile (job.requestId + ".json");
    const auto value = readBoundedJson (receiptFile);
    const auto* receipt = value.getDynamicObject();
    WorkAttachmentResult result;
    if (receipt == nullptr
        || ! exactProperties (*receipt, { "format", "version", "request_id", "target_role",
                                          "work_id", "binding_id", "runtime_instance_id",
                                          "status", "code", "observed_at_ms" })
        || pre_display::objectString (*receipt, "format")
            != "kirin_hypha_capture_attachment_receipt"
        || pre_display::objectString (*receipt, "version") != "1.0"
        || pre_display::objectString (*receipt, "request_id") != job.requestId
        || pre_display::objectString (*receipt, "target_role") != "post"
        || pre_display::objectString (*receipt, "work_id") != job.work.workId
        || pre_display::objectString (*receipt, "binding_id") != job.work.bindingId
        || pre_display::objectString (*receipt, "runtime_instance_id")
            != job.work.runtimeInstanceId)
        return result;
    std::int64_t observedAt = 0;
    if (! pre_display::objectInteger (*receipt, "observed_at_ms", 0,
                                      pre_display::maxSafeJsonInteger, observedAt))
        return {};
    result.requestId = job.requestId;
    const auto status = pre_display::objectString (*receipt, "status");
    if (status == "attached" && receipt->getProperty ("code").isVoid())
        result.state = WorkAttachmentResultState::attached;
    else if (status == "rejected")
    {
        result.code = pre_display::objectString (*receipt, "code");
        if (supportedRejectionCode (result.code))
            result.state = WorkAttachmentResultState::rejected;
    }
    return result;
}

void WorkAttachmentController::finish (WorkAttachmentResult result)
{
    const juce::ScopedLock lock (stateLock);
    completion = std::move (result);
    inFlight = false;
}

void WorkAttachmentController::cleanup (const Job& job, bool removeReceipt) const
{
    root.getChildFile ("requests").getChildFile (job.requestId + ".json").deleteFile();
    root.getChildFile ("artifacts").getChildFile (job.requestId + ".png").deleteFile();
    if (removeReceipt)
        root.getChildFile ("receipts").getChildFile (job.requestId + ".json").deleteFile();
}

void WorkAttachmentController::run()
{
    std::unique_ptr<Job> active;
    while (! threadShouldExit())
    {
        if (active == nullptr)
        {
            const juce::ScopedLock lock (stateLock);
            active = std::move (pending);
        }
        if (active == nullptr)
        {
            wait (500);
            continue;
        }
        if (! active->published)
        {
            if (! publish (*active))
            {
                finish ({ WorkAttachmentResultState::rejected,
                          active->requestId, "request_write_failed" });
                active.reset();
                continue;
            }
        }
        const auto receipt = readReceipt (*active);
        if (receipt.terminal())
        {
            cleanup (*active, true);
            finish (receipt);
            active.reset();
            continue;
        }
        if (juce::Time::currentTimeMillis() > active->expiresAtMs)
        {
            cleanup (*active, false);
            finish ({ WorkAttachmentResultState::rejected,
                      active->requestId, "kirin_os_unavailable" });
            active.reset();
            continue;
        }
        wait (100);
    }
}

juce::File WorkAttachmentController::transportRoot()
{
   #if JUCE_WINDOWS
    auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (local.isEmpty())
        local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getFullPathName();
    if (local.isEmpty())
    {
        const auto profile = juce::SystemStats::getEnvironmentVariable ("USERPROFILE", {});
        if (profile.isNotEmpty())
            local = juce::File (profile).getChildFile ("AppData").getChildFile ("Local")
                                        .getFullPathName();
    }
    if (local.isEmpty())
        return {};
    return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data")
                             .getChildFile ("hypha_capture").getChildFile ("v1");
   #else
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    if (home.getFullPathName().isEmpty())
        return {};
    return home.getChildFile ("Library").getChildFile ("Application Support")
               .getChildFile ("Kirin OS").getChildFile ("plugin_data")
               .getChildFile ("hypha_capture").getChildFile ("v1");
   #endif
}
}
