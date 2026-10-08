#include "../src/CaptureWorkAttachment.h"
#include "../src/HyphaCapturePngFile.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>

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

juce::File publishedRequest (const juce::File& requests)
{
    juce::File result;
    for (const auto& file : requests.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        const auto id = file.getFileNameWithoutExtension();
        // JUCE's atomic-write sibling also ends in .json, but is named
        // <UUID>_temp<random>.json. submit() uses the canonical dashed UUID that
        // Kirin OS requires. Only that committed path is a request.
        if (id.length() != 36 || juce::Uuid (id).toDashedString() != id) continue;
        if (result != juce::File()) return {}; // more than one final request is invalid
        result = file;
    }
    return result;
}

juce::File waitForRequest (const juce::File& requests)
{
    // The controller writes on its own thread. A loaded CI runner can take seconds, so wait up to
    // ten seconds by the clock; a request that is published returns at once.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const auto file = publishedRequest (requests);
        if (file != juce::File()) return file;
        juce::Thread::sleep (10);
    }
    return publishedRequest (requests);
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
    // Same clock deadline as waitForRequest: a terminal result returns at once.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
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
        const auto images = root.getChildFile ("local-png");
        require (images.createDirectory().wasOk(), "create isolated local PNG destination");
        const auto output = images.getChildFile ("capture.png");
        for (const auto size : { 8, 3, 12 })
        {
            juce::Image image (juce::Image::ARGB, size, size, true);
            const auto colour = size == 3 ? juce::Colours::blue : juce::Colours::red;
            image.clear (image.getBounds(), colour);
            require (hypha::capture::saveFrozenPng (image, output), "create or replace one complete local PNG");
            juce::MemoryOutputStream expected;
            require (juce::PNGImageFormat().writeImageToStream (image, expected), "encode the immutable fixture");
            juce::MemoryBlock actual;
            require (output.loadFileAsData (actual) && actual.getSize() == expected.getDataSize()
                     && std::memcmp (actual.getData(), expected.getData(), actual.getSize()) == 0,
                     "replacement contains exactly the new PNG, without the old image or trailing bytes");
            const auto decoded = juce::ImageFileFormat::loadFrom (output);
            require (decoded.getWidth() == size && decoded.getHeight() == size
                     && decoded.getPixelAt (0, 0) == colour,
                     "opening the replacement shows the newly frozen pixels");
        }
        juce::MemoryBlock retained;
        require (output.loadFileAsData (retained), "retain previous valid PNG");
        require (! hypha::capture::saveFrozenPng ({}, output), "invalid image reports failure");
        juce::MemoryBlock unchanged;
        require (output.loadFileAsData (unchanged) && unchanged == retained,
                 "failed image preparation preserves the previous PNG");
        const auto blocked = images.getChildFile ("blocked-parent");
        require (blocked.replaceWithText ("preserve"), "create file blocking the destination directory");
        juce::Image image (juce::Image::ARGB, 3, 3, true);
        require (! hypha::capture::saveFrozenPng (image, blocked.getChildFile ("capture.png"))
                 && blocked.loadFileAsString() == "preserve"
                 && ! hypha::capture::saveFrozenPng (image, images),
                 "unwritable or directory destination fails without changing existing content");
        require (images.findChildFiles (juce::File::findFiles, false).size() == 2,
                 "successful and failed saves leave no temporary siblings");
    }

    {
        const auto requests = root.getChildFile ("atomic-publication-control");
        require (requests.createDirectory().wasOk(), "create atomic publication control");
        const auto target = requests.getChildFile (juce::Uuid().toDashedString() + ".json");
        juce::TemporaryFile pending (target);
        require (pending.getFile().replaceWithText ("{}"), "create actual atomic-write sibling");
        require (requests.findChildFiles (juce::File::findFiles, false, "*.json").size() == 1
                 && publishedRequest (requests) == juce::File(),
                 "one temporary JSON is not a committed request");
        require (pending.overwriteTargetFileWithTemporary()
                 && publishedRequest (requests) == target,
                 "only atomic commit publishes the final request path");
    }

    {
        hypha::capture::WorkAttachmentController controller (root);
        const auto work = postWork();
        auto typed = descriptor();
        typed.domain = "time";
        typed.v1MeaningPreserved = false;
        const auto frozenPng = captureBytes();
        const auto retained = frozenPng;
        require (controller.submit (work, frozenPng, typed)
                    == hypha::capture::WorkAttachmentSubmit::unsupportedPresentation,
                 "v1 explicitly rejects independent targets/cutoffs instead of dropping meaning");
        require (! root.getChildFile ("requests").exists()
                 && ! root.getChildFile ("artifacts").exists()
                 && frozenPng == retained && ! controller.takeResult().terminal(),
                 "unsupported attachment writes nothing and preserves the immutable local PNG");
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
        // Kirin OS's receiver checks this exact form, and polls only files named by it.
        require (std::regex_match (requestId.toStdString(), std::regex (
                     "[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}", std::regex::icase))
                 && requestFile.getFileName() == requestId + ".json"
                 && request->getProperty ("artifact_file").toString() == requestId + ".png",
                 "name the request and its files with the canonical UUID that Kirin OS accepts");
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
