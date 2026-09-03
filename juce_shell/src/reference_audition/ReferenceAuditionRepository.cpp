#include "ReferenceAuditionRepository.h"

#include <utility>

#include <juce_cryptography/juce_cryptography.h>

#include "ReferenceAuditionProtocol.h"

namespace hypha::reference_audition
{
    namespace
    {
        bool readBounded (const juce::File& file, std::int64_t maximumBytes,
                          juce::MemoryBlock& output)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return false;
            const auto bytes = file.getSize();
            if (bytes < 1 || bytes > maximumBytes)
                return false;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk())
                return false;
            output.setSize (static_cast<size_t> (bytes), false);
            return stream->read (output.getData(), static_cast<int> (bytes)) == bytes;
        }

        juce::var parseJson (const juce::MemoryBlock& bytes)
        {
            const auto text = juce::String::fromUTF8 (
                static_cast<const char*> (bytes.getData()),
                static_cast<int> (bytes.getSize()));
            return juce::JSON::parse (text);
        }

        LoadResult rejected (juce::String code, Preparation preparation = {})
        {
            LoadResult result;
            result.state = LoadState::rejected;
            result.rejectionCode = std::move (code);
            result.preparation = std::move (preparation);
            return result;
        }
    }

    Repository::Repository (juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    juce::File Repository::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
        if (local == juce::File())
        {
            const auto profile = juce::SystemStats::getEnvironmentVariable ("USERPROFILE", {});
            if (profile.isNotEmpty())
                local = juce::File (profile).getChildFile ("AppData").getChildFile ("Local");
        }
        return local.getChildFile ("Kirin OS").getChildFile ("plugin_data")
                    .getChildFile ("hypha_ab").getChildFile ("v1");
       #else
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return home.getChildFile ("Library").getChildFile ("Application Support")
                   .getChildFile ("Kirin OS").getChildFile ("plugin_data")
                   .getChildFile ("hypha_ab").getChildFile ("v1");
       #endif
    }

    LoadResult Repository::load (const juce::String& workId) const
    {
        if (! safeId (workId) || root == juce::File())
            return rejected ("target_work_invalid");

        const auto preparationFile = root.getChildFile ("preparations")
                                         .getChildFile (workId + ".json");
        if (! preparationFile.exists())
            return {};

        juce::MemoryBlock preparationBytes;
        if (! readBounded (preparationFile, maximumPreparationBytes, preparationBytes))
            return rejected ("preparation_contract_rejected");
        Preparation preparation;
        if (! parsePreparation (parseJson (preparationBytes), preparation)
            || preparation.workId != workId)
            return rejected ("preparation_contract_rejected");

        const auto receiptFile = root.getChildFile ("sources")
                                     .getChildFile (preparation.receiptSha256 + ".json");
        juce::MemoryBlock receiptBytes;
        if (! readBounded (receiptFile, maximumSourceReceiptBytes, receiptBytes)
            || receiptBytes.getSize() != static_cast<size_t> (preparation.receiptBytes)
            || juce::SHA256 (receiptBytes).toHexString() != preparation.receiptSha256)
            return rejected ("source_receipt_rejected", preparation);

        SourceReceipt receipt;
        if (! parseSourceReceipt (parseJson (receiptBytes), receipt)
            || ! receipt.matches (preparation))
            return rejected ("source_receipt_rejected", preparation);

        LoadResult result;
        result.state = LoadState::accepted;
        result.preparation = std::move (preparation);
        result.receipt = std::move (receipt);
        return result;
    }

    juce::String Repository::verifySourceFile (const SourceReceipt& receipt) const
    {
        if (! receipt.valid())
            return "source_receipt_rejected";
        const juce::File source (receipt.filePath);
        if (! source.existsAsFile() || source.isSymbolicLink())
            return "source_open_failed";
        if (receipt.revisionSize.isNotEmpty()
            && juce::String (source.getSize()) != receipt.revisionSize.upToFirstOccurrenceOf (".", false, false))
            return "source_changed";
        if (juce::SHA256 (source).toHexString() != receipt.sourceSha256)
            return "source_changed";
        return {};
    }
}
