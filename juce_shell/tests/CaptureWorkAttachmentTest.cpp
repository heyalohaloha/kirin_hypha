#include "../src/CaptureWorkAttachment.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require (bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "CAPTURE Work attachment contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

juce::MemoryBlock captureBytes()
{
    const char payload[] = "bounded immutable Hypha PNG fixture bytes for transport";
    return { payload, sizeof (payload) };
}

hypha::pre_display::WorkReference postWork()
{
    hypha::pre_display::WorkReference result;
    result.targetRole = hypha::pre_display::GuideTargetRole::post;
    result.workId = "11111111-1111-4111-8111-111111111111";
    result.bindingId = "22222222-2222-4222-8222-222222222222";
    result.runtimeInstanceId = "runtime-post-a";
    result.displayTitle = "Exact Work";
    return result;
}

hypha::capture::WorkAttachmentDescriptor descriptor()
{
    return { 1'200, 630, "level", "absolute", 1'788'256'800'000 };
}

juce::File waitForRequest (const juce::File& requests)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const auto files = requests.findChildFiles (juce::File::findFiles, false, "*.json");
        if (files.size() == 1)
            return files[0];
        juce::Thread::sleep (10);
    }
    return {};
}

bool writeReceipt (const juce::File& target,
                   const juce::DynamicObject& request,
                   const juce::String& status,
                   juce::var code)
{
    auto receipt = new juce::DynamicObject();
    receipt->setProperty ("format", "kirin_hypha_capture_attachment_receipt");
    receipt->setProperty ("version", "1.0");
    receipt->setProperty ("request_id", request.getProperty ("request_id"));
    receipt->setProperty ("target_role", "post");
    receipt->setProperty ("work_id", request.getProperty ("work_id"));
    receipt->setProperty ("binding_id", request.getProperty ("binding_id"));
    receipt->setProperty ("runtime_instance_id", request.getProperty ("runtime_instance_id"));
    receipt->setProperty ("status", status);
    receipt->setProperty ("code", std::move (code));
    receipt->setProperty ("observed_at_ms", static_cast<juce::int64> (1'788'256'800'100));
    return target.replaceWithText (juce::JSON::toString (juce::var (receipt), true) + "\n");
}

hypha::capture::WorkAttachmentResult waitForResult (
    hypha::capture::WorkAttachmentController& controller)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const auto result = controller.takeResult();
        if (result.terminal())
            return result;
        juce::Thread::sleep (10);
    }
    return {};
}
}

int main()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-hypha-capture-attachment", {}, false);
    require (root.createDirectory().wasOk(), "create isolated transport root");

    {
        hypha::capture::WorkAttachmentController controller (root);
        const auto work = postWork();
        require (controller.submit (work, captureBytes(), descriptor())
                    == hypha::capture::WorkAttachmentSubmit::accepted,
                 "accept one explicit POST Work attachment");
        require (controller.submit (work, captureBytes(), descriptor())
                    == hypha::capture::WorkAttachmentSubmit::busy,
                 "do not silently replace an in-flight user action");

        const auto requestFile = waitForRequest (root.getChildFile ("requests"));
        require (requestFile.existsAsFile(), "publish the bounded request after its artifact");
        const auto requestValue = juce::JSON::parse (requestFile);
        const auto* request = requestValue.getDynamicObject();
        require (request != nullptr
                 && request->getProperty ("user_action") == "capture_attach"
                 && request->getProperty ("target_role") == "post"
                 && request->getProperty ("work_id") == work.workId
                 && request->getProperty ("binding_id") == work.bindingId
                 && request->getProperty ("runtime_instance_id") == work.runtimeInstanceId
                 && static_cast<int> (request->getProperty ("pixel_width")) == 1'200
                 && static_cast<int> (request->getProperty ("pixel_height")) == 630,
                 "carry the immutable Work Reference and Capture facts");

        const auto requestId = request->getProperty ("request_id").toString();
        const auto receiptFile = root.getChildFile ("receipts").getChildFile (requestId + ".json");
        require (receiptFile.getParentDirectory().createDirectory().wasOk()
                 && writeReceipt (receiptFile, *request, "attached", juce::var()),
                 "write a matching OS receipt");
        const auto result = waitForResult (controller);
        require (result.state == hypha::capture::WorkAttachmentResultState::attached
                 && result.requestId == requestId,
                 "surface the terminal result of the initiating action");
        require (! requestFile.existsAsFile()
                 && ! root.getChildFile ("artifacts").getChildFile (requestId + ".png").existsAsFile()
                 && ! receiptFile.existsAsFile(),
                 "clean private transport artifacts after a verified receipt");
    }

    {
        hypha::capture::WorkAttachmentController controller (root);
        auto preReference = postWork();
        preReference.targetRole = hypha::pre_display::GuideTargetRole::pre;
        require (controller.submit (preReference, captureBytes(), descriptor())
                    == hypha::capture::WorkAttachmentSubmit::invalidReference,
                 "reject a PRE or unaccepted Work authority");
        auto wrongSize = descriptor();
        wrongSize.pixelWidth = 600;
        require (controller.submit (postWork(), captureBytes(), wrongSize)
                    == hypha::capture::WorkAttachmentSubmit::invalidCapture,
                 "reject dimensions outside the high-resolution CAPTURE contract");
    }

    root.deleteRecursively();
    return EXIT_SUCCESS;
}
