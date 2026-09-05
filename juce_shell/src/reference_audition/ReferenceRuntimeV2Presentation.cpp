#include "ReferenceRuntimeV2Presentation.h"

#include <regex>

namespace hypha::reference_audition
{
    namespace
    {
        constexpr std::int64_t maximumPresentationBytes = 16 * 1024;

        bool uuidV4 (const juce::String& value)
        {
            static const std::regex expression (
                R"(^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$)");
            return std::regex_match (value.toStdString(), expression);
        }

        bool timestamp (const juce::String& value)
        {
            static const std::regex expression (
                R"(^[0-9]{4}-(0[1-9]|1[0-2])-([0-2][0-9]|3[01])T([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\.[0-9]{3}Z$)");
            return std::regex_match (value.toStdString(), expression);
        }

        bool exactProperties (const juce::DynamicObject& object,
                              std::initializer_list<const char*> names)
        {
            if (object.getProperties().size() != static_cast<int> (names.size()))
                return false;
            for (const auto* name : names)
                if (! object.hasProperty (name)) return false;
            return true;
        }

        bool readJson (const juce::File& file, juce::var& value)
        {
            if (! file.existsAsFile() || file.isSymbolicLink()
                || file.getSize() < 1 || file.getSize() > maximumPresentationBytes)
                return false;
            juce::MemoryBlock bytes;
            auto stream = file.createInputStream();
            if (stream == nullptr || ! stream->openedOk()) return false;
            bytes.setSize (static_cast<size_t> (file.getSize()), false);
            if (stream->read (bytes.getData(), static_cast<int> (bytes.getSize()))
                != static_cast<int> (bytes.getSize()))
                return false;
            const auto* raw = static_cast<const char*> (bytes.getData());
            if (bytes.getSize() >= 3 && static_cast<unsigned char> (raw[0]) == 0xef
                && static_cast<unsigned char> (raw[1]) == 0xbb
                && static_cast<unsigned char> (raw[2]) == 0xbf)
                return false;
            if (! juce::CharPointer_UTF8::isValidString (raw, static_cast<int> (bytes.getSize())))
                return false;
            value = juce::JSON::parse (
                juce::String::fromUTF8 (raw, static_cast<int> (bytes.getSize())));
            return ! value.isVoid();
        }
    }

    RuntimeV2PresentationRepository::RuntimeV2PresentationRepository (
        juce::File transportRootIn)
        : root (std::move (transportRootIn))
    {
    }

    RuntimePresentationLayout RuntimeV2PresentationRepository::load (
        const juce::String& workId) const noexcept
    {
        try
        {
            if (! uuidV4 (workId)) return RuntimePresentationLayout::automatic;
            juce::var value;
            if (! readJson (root.getChildFile ("presentations").getChildFile (workId + ".json"), value))
                return RuntimePresentationLayout::automatic;
            const auto* object = value.getDynamicObject();
            if (object == nullptr || ! exactProperties (*object, {
                    "format", "version", "work_id", "layout_mode", "updated_at" })
                || object->getProperty ("format") != "kirin_reference_presentation"
                || object->getProperty ("version") != "1.0"
                || object->getProperty ("work_id") != workId
                || ! timestamp (object->getProperty ("updated_at").toString()))
                return RuntimePresentationLayout::automatic;
            const auto layout = object->getProperty ("layout_mode").toString();
            if (layout == "main") return RuntimePresentationLayout::main;
            if (layout == "equal") return RuntimePresentationLayout::equal;
            return RuntimePresentationLayout::automatic;
        }
        catch (...)
        {
            return RuntimePresentationLayout::automatic;
        }
    }

    juce::String RuntimeV2PresentationRepository::text (RuntimePresentationLayout layout)
    {
        if (layout == RuntimePresentationLayout::main) return "main";
        if (layout == RuntimePresentationLayout::equal) return "equal";
        return "auto";
    }
}
