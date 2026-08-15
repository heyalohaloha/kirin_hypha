#include "PreDisplayController.h"
#include "PreDisplayProjection.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::pre_display
{
    namespace
    {
        constexpr std::int64_t kPresenceLeaseMs = 1'500;
        constexpr std::int64_t kProjectionPollMs = 100;
        constexpr int kGuideScanEveryPolls = 5;
        constexpr std::int64_t kClockStaleMs = 2'000;
        constexpr std::int64_t kInspectHoldNs = 1'000'000'000;
        constexpr std::int64_t kMaxPointerBytes = 16 * 1024;
        constexpr std::int64_t kMaxGuideBytes = 1024 * 1024;
        constexpr int kMaxGuideItems = 2'048;
        constexpr std::int64_t kMaxSafeJsonInteger = 9'007'199'254'740'991;

        struct GuideItem
        {
            juce::String itemId;
            juce::String label;
            juce::String sourceLabel;
            juce::String channel;
            std::int64_t startNs = 0;
            std::int64_t endNs = 0;
            double lowHz = 0.0;
            double highHz = 0.0;
            bool hasBand = false;
        };

        struct GuideModel
        {
            juce::String cacheKey;
            juce::String groupId;
            juce::String guideId;
            juce::String contentHash;
            juce::String payloadKind;
            juce::String focusEventId;
            juce::String sourcePairLabel;
            std::int64_t sourceZeroProjectNs = 0;
            std::int64_t revision = 0;
            std::vector<GuideItem> items;

            bool valid() const noexcept { return guideId.isNotEmpty(); }
        };

        std::uint32_t currentProcessId() noexcept
        {
           #if JUCE_WINDOWS
            return static_cast<std::uint32_t> (::GetCurrentProcessId());
           #else
            return static_cast<std::uint32_t> (::getpid());
           #endif
        }

        bool safeId (const juce::String& value)
        {
            if (value.isEmpty() || value.length() > 128)
                return false;
            for (const auto character : value)
                if (! (character >= 'A' && character <= 'Z')
                    && ! (character >= 'a' && character <= 'z')
                    && ! (character >= '0' && character <= '9')
                    && character != '.' && character != '_' && character != ':' && character != '-')
                    return false;
            return true;
        }

        bool safeHash (const juce::String& value)
        {
            if (value.length() != 64)
                return false;
            for (const auto character : value)
                if (! (character >= '0' && character <= '9')
                    && ! (character >= 'a' && character <= 'f'))
                    return false;
            return true;
        }

        bool safeDisplayLabel (const juce::String& value)
        {
            if (value.isEmpty() || value.length() > 48 || value != value.trim())
                return false;
            for (const auto character : value)
                if (character < 0x20 || character == 0x7f)
                    return false;
            return true;
        }

        bool safeGuideFileName (const juce::String& value)
        {
            return value.isNotEmpty()
                && value == juce::File::createLegalFileName (value)
                && value.endsWith (".json")
                && ! value.contains ("..")
                && ! value.containsAnyOf ("/\\");
        }

        juce::var parseBoundedJson (const juce::File& file, std::int64_t maxBytes)
        {
            if (! file.existsAsFile() || file.isSymbolicLink())
                return {};
            const auto size = file.getSize();
            if (size <= 0 || size > maxBytes)
                return {};
            return juce::JSON::parse (file);
        }

        juce::String objectString (const juce::DynamicObject& object, const char* name)
        {
            return object.getProperty (name).toString();
        }

        bool objectInteger (const juce::DynamicObject& object, const char* name,
                            std::int64_t minimum, std::int64_t maximum, std::int64_t& result)
        {
            const auto value = object.getProperty (name);
            if (! value.isInt() && ! value.isInt64())
                return false;
            result = static_cast<std::int64_t> (static_cast<juce::int64> (value));
            return result >= minimum && result <= maximum;
        }

        bool replaceJsonAtomically (const juce::File& target, const juce::var& value)
        {
            if (! target.getParentDirectory().createDirectory())
                return false;
            juce::TemporaryFile temporary (target);
            {
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr || ! stream->openedOk())
                    return false;
                const auto json = juce::JSON::toString (value, true);
                if (! stream->writeText (json + "\n", false, false, "\n"))
                    return false;
                stream->flush();
                if (stream->getStatus().failed())
                    return false;
            }
            return temporary.overwriteTargetFileWithTemporary();
        }

        juce::String clockSourceName (ClockSource source)
        {
            switch (source)
            {
                case ClockSource::projectTimeline: return "project_timeline";
                case ClockSource::audioRenderTimeline: return "audio_render_timeline";
                case ClockSource::unknown: break;
            }
            return "unknown";
        }

        bool guideTargetsAllPre (const juce::DynamicObject& guide)
        {
            const auto* target = guide.getProperty ("target").getDynamicObject();
            if (target == nullptr)
                return false;
            return objectString (*target, "selection_mode") == "all_pre_instances";
        }

        juce::String sourceLabelFor (const std::map<std::string, juce::String>& labels,
                                     const juce::String& sourceRef)
        {
            const auto found = labels.find (sourceRef.toStdString());
            return found != labels.end() ? found->second : sourceRef.substring (0, 48);
        }

        juce::String compactText (const juce::String& text, int maximumCharacters)
        {
            if (text.length() <= maximumCharacters)
                return text;
            return text.substring (0, juce::jmax (1, maximumCharacters - 1)).trimEnd()
                 + juce::String::charToString (0x2026);
        }

        bool parseBand (const juce::var& value, GuideItem& item)
        {
            if (value.isVoid() || value.isUndefined())
                return true;
            const auto* band = value.getDynamicObject();
            if (band == nullptr)
                return value.isVoid();
            const auto lowValue = band->getProperty ("low_hz");
            const auto highValue = band->getProperty ("high_hz");
            const auto isNumber = [] (const juce::var& number)
            {
                return number.isDouble() || number.isInt() || number.isInt64();
            };
            if (! isNumber (lowValue) || ! isNumber (highValue))
                return false;
            const auto low = static_cast<double> (lowValue);
            const auto high = static_cast<double> (highValue);
            if (! std::isfinite (low) || ! std::isfinite (high) || low < 0.0 || high <= low
                || high > 1'000'000.0)
                return false;
            item.lowHz = low;
            item.highHz = high;
            item.hasBand = true;
            return true;
        }

        bool parseGuideItem (const juce::DynamicObject& object, const GuideModel& model,
                             const std::map<std::string, juce::String>& sourceLabels,
                             const std::map<std::string, std::int64_t>& sourceDurations,
                             GuideItem& item)
        {
            item.itemId = objectString (object, "item_id");
            if (! safeId (item.itemId)
                || ! objectInteger (object, "start_ns", 0, kMaxSafeJsonInteger, item.startNs)
                || ! objectInteger (object, "end_ns", 0, kMaxSafeJsonInteger, item.endNs)
                || item.endNs <= item.startNs
                || ! parseBand (object.getProperty ("band"), item))
                return false;

            if (model.payloadKind == "inspect")
            {
                const auto typeId = objectString (object, "type_id");
                const auto sourceRef = objectString (object, "source_ref");
                if (! safeId (typeId) || ! safeId (sourceRef))
                    return false;
                const auto displayLabel = objectString (object, "display_label");
                item.label = displayLabel.isEmpty() ? typeId.substring (0, 48) : displayLabel;
                item.sourceLabel = sourceLabelFor (sourceLabels, sourceRef);
                item.channel = objectString (object, "channel_key");
                const auto duration = sourceDurations.find (sourceRef.toStdString());
                if (! safeDisplayLabel (item.label)
                    || duration == sourceDurations.end() || item.endNs > duration->second
                    || (! item.channel.isEmpty() && ! safeId (item.channel)))
                    return false;
            }
            else
            {
                const auto state = objectString (object, "frequency_state");
                if (state != "measured" && state != "unlocated"
                    && state != "missing" && state != "invalid")
                    return false;
                if ((state == "measured") != item.hasBand)
                    return false;
            }
            return true;
        }

        bool parseGuideModel (const juce::DynamicObject& guide, const juce::String& cacheKey,
                              GuideModel& model)
        {
            if (objectString (guide, "format") != "kirin_pre_display_guide"
                || objectString (guide, "version") != "1.0")
                return false;
            model.cacheKey = cacheKey;
            model.guideId = objectString (guide, "guide_id");
            model.contentHash = objectString (guide, "content_hash");
            const auto* target = guide.getProperty ("target").getDynamicObject();
            const auto* timeBasis = guide.getProperty ("time_basis").getDynamicObject();
            const auto* payload = guide.getProperty ("payload").getDynamicObject();
            const auto* sourceSet = guide.getProperty ("source_set").getArray();
            if (! safeId (model.guideId) || ! safeHash (model.contentHash)
                || target == nullptr || timeBasis == nullptr || payload == nullptr
                || sourceSet == nullptr || sourceSet->isEmpty() || sourceSet->size() > 32)
                return false;
            model.groupId = objectString (*target, "group_id");
            model.payloadKind = objectString (*payload, "kind");
            if (! safeId (model.groupId)
                || objectString (*target, "selection_mode") != "all_pre_instances"
                || (model.payloadKind != "masking" && model.payloadKind != "inspect")
                || ! objectInteger (guide, "revision", 1, kMaxSafeJsonInteger, model.revision)
                || objectString (*timeBasis, "kind") != "source_to_project_offset"
                || objectString (*timeBasis, "unit") != "nanoseconds"
                || objectString (*timeBasis, "interval_convention") != "half_open"
                || (objectString (*timeBasis, "alignment_method") != "project_zero"
                    && objectString (*timeBasis, "alignment_method") != "captured_playhead"
                    && objectString (*timeBasis, "alignment_method") != "audio_landmark")
                || ! objectInteger (*timeBasis, "source_zero_project_ns", -kMaxSafeJsonInteger,
                                    kMaxSafeJsonInteger, model.sourceZeroProjectNs))
                return false;

            std::map<std::string, juce::String> sourceLabels;
            std::map<std::string, std::int64_t> sourceDurations;
            for (const auto& sourceValue : *sourceSet)
            {
                const auto* source = sourceValue.getDynamicObject();
                if (source == nullptr)
                    return false;
                const auto ref = objectString (*source, "source_ref");
                auto label = objectString (*source, "display_label");
                std::int64_t duration = 0;
                if (! safeId (ref) || (! label.isEmpty() && ! safeDisplayLabel (label))
                    || ! objectInteger (*source, "duration_ns", 0, kMaxSafeJsonInteger, duration))
                    return false;
                if (label.isEmpty())
                    label = ref.substring (0, 48);
                if (! sourceLabels.emplace (ref.toStdString(), label).second
                    || ! sourceDurations.emplace (ref.toStdString(), duration).second)
                    return false;
            }

            const juce::Array<juce::var>* items = nullptr;
            std::int64_t maskingDuration = kMaxSafeJsonInteger;
            if (model.payloadKind == "masking")
            {
                items = payload->getProperty ("intervals").getArray();
                const auto* order = payload->getProperty ("source_order").getArray();
                if (order == nullptr || order->size() != 2)
                    return false;
                const auto a = order->getUnchecked (0).toString();
                const auto b = order->getUnchecked (1).toString();
                if (! safeId (a) || ! safeId (b) || a == b
                    || sourceLabels.find (a.toStdString()) == sourceLabels.end()
                    || sourceLabels.find (b.toStdString()) == sourceLabels.end())
                    return false;
                const auto durationA = sourceDurations.find (a.toStdString());
                const auto durationB = sourceDurations.find (b.toStdString());
                if (durationA == sourceDurations.end() || durationB == sourceDurations.end())
                    return false;
                maskingDuration = juce::jmin (durationA->second, durationB->second);
                model.sourcePairLabel = compactText (sourceLabelFor (sourceLabels, a), 16) + " × "
                                      + compactText (sourceLabelFor (sourceLabels, b), 16);
            }
            else
            {
                items = payload->getProperty ("events").getArray();
                const auto sourceRef = objectString (*payload, "source_ref");
                if (! safeId (sourceRef)
                    || sourceLabels.find (sourceRef.toStdString()) == sourceLabels.end())
                    return false;
                model.focusEventId = objectString (*payload, "focus_event_id");
                if (! model.focusEventId.isEmpty() && ! safeId (model.focusEventId))
                    return false;
            }
            if (items == nullptr || items->size() > kMaxGuideItems)
                return false;
            std::map<std::string, bool> itemIds;
            model.items.reserve (static_cast<std::size_t> (items->size()));
            for (const auto& value : *items)
            {
                const auto* object = value.getDynamicObject();
                GuideItem item;
                if (object == nullptr
                    || ! parseGuideItem (*object, model, sourceLabels, sourceDurations, item)
                    || ! itemIds.emplace (item.itemId.toStdString(), true).second
                    || (model.payloadKind == "masking" && item.endNs > maskingDuration))
                    return false;
                model.items.push_back (std::move (item));
            }
            if (model.payloadKind == "inspect" && model.focusEventId.isNotEmpty()
                && itemIds.find (model.focusEventId.toStdString()) == itemIds.end())
                return false;
            std::stable_sort (model.items.begin(), model.items.end(), [] (const auto& a, const auto& b)
            {
                if (a.startNs != b.startNs) return a.startNs < b.startNs;
                return a.itemId < b.itemId;
            });
            return true;
        }

        juce::String conciseNumber (double value)
        {
            if (value >= 1'000.0)
            {
                const auto kHz = value / 1'000.0;
                return juce::String (kHz, std::abs (kHz - std::round (kHz)) < 0.05 ? 0 : 1);
            }
            return juce::String (value, std::abs (value - std::round (value)) < 0.05 ? 0 : 1);
        }

        juce::String bandText (const GuideItem& item)
        {
            if (! item.hasBand)
                return {};
            if (item.lowHz >= 1'000.0)
                return conciseNumber (item.lowHz) + "–" + conciseNumber (item.highHz) + " kHz";
            if (item.highHz >= 1'000.0)
                return conciseNumber (item.lowHz) + " Hz–" + conciseNumber (item.highHz) + " kHz";
            return conciseNumber (item.lowHz) + "–" + conciseNumber (item.highHz) + " Hz";
        }

        juce::String timeText (std::int64_t nanoseconds)
        {
            const auto tenths = juce::jmax<std::int64_t> (0, (nanoseconds + 50'000'000) / 100'000'000);
            const auto totalSeconds = tenths / 10;
            const auto hours = totalSeconds / 3'600;
            const auto minutes = (totalSeconds / 60) % 60;
            const auto seconds = totalSeconds % 60;
            const auto tenth = tenths % 10;
            if (hours > 0)
                return juce::String (hours) + ":" + juce::String (minutes).paddedLeft ('0', 2)
                     + ":" + juce::String (seconds).paddedLeft ('0', 2) + "." + juce::String (tenth);
            return juce::String (minutes) + ":" + juce::String (seconds).paddedLeft ('0', 2)
                 + "." + juce::String (tenth);
        }

        juce::String itemDetail (const GuideModel& guide, const GuideItem& item, int overlapCount)
        {
            juce::StringArray parts;
            if (guide.payloadKind == "masking")
                parts.add (guide.sourcePairLabel);
            else
            {
                if (item.sourceLabel.isNotEmpty()) parts.add (item.sourceLabel);
                if (item.channel.isNotEmpty() && item.channel != "mix") parts.add (item.channel.toUpperCase());
            }
            const auto band = bandText (item);
            if (band.isNotEmpty()) parts.add (band);
            if (overlapCount > 0) parts.add ("+" + juce::String (overlapCount));
            return compactText (parts.joinIntoString ("  ·  "), 48);
        }
    }

    struct Controller::WorkerState
    {
        GuideModel guide;
        std::uint64_t clockGeneration = 0;
        std::int64_t clockObservedAtMs = 0;
        int pollsUntilGuideScan = 0;
    };

    Controller::Controller (const ClockTap& clockTapIn)
        : juce::Thread ("Kirin PRE display"), clockTap (clockTapIn),
          workerState (std::make_unique<WorkerState>())
    {
    }

    Controller::~Controller()
    {
        signalThreadShouldExit();
        notify();
        stopThread (2'000);
        removeOwnPresence();
    }

    void Controller::configureAndStart (RuntimeIdentity identityIn)
    {
        if (! safeId (identityIn.instanceId) || ! safeId (identityIn.projectUuid))
            return;
        if (identityIn.hostProcessId == 0)
            identityIn.hostProcessId = currentProcessId();
        juce::File previousPresenceFile;
        juce::File currentPresenceFile;
        {
            const juce::ScopedLock lock (identityLock);
            previousPresenceFile = ownPresenceFile;
            identity = std::move (identityIn);
            configured = true;
            ownPresenceFile = transportRoot().getChildFile ("presence")
                                             .getChildFile (identity.instanceId + ".json");
            currentPresenceFile = ownPresenceFile;
        }
        if (previousPresenceFile != juce::File() && previousPresenceFile != currentPresenceFile
            && previousPresenceFile.existsAsFile())
            previousPresenceFile.deleteFile();
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    void Controller::setName (const juce::String& name)
    {
        const juce::ScopedLock lock (identityLock);
        identity.name = name.substring (0, 64);
    }

    DisplaySnapshot Controller::displaySnapshot() const
    {
        const juce::ScopedLock lock (displayLock);
        return display;
    }

    juce::File Controller::transportRoot()
    {
       #if JUCE_WINDOWS
        auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        if (local.isEmpty())
            local = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getFullPathName();
        return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data")
                                 .getChildFile ("pre_display").getChildFile ("v1");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library").getChildFile ("Application Support").getChildFile ("Kirin OS")
            .getChildFile ("plugin_data").getChildFile ("pre_display").getChildFile ("v1");
       #endif
    }

    void Controller::run()
    {
        while (! threadShouldExit())
        {
            RuntimeIdentity currentIdentity;
            {
                const juce::ScopedLock lock (identityLock);
                if (configured)
                    currentIdentity = identity;
            }
            if (safeId (currentIdentity.instanceId))
            {
                ClockSnapshot clock;
                clockTap.read (clock);
                const auto now = juce::Time::currentTimeMillis();
                if (clock.generation != workerState->clockGeneration)
                {
                    workerState->clockGeneration = clock.generation;
                    workerState->clockObservedAtMs = now;
                }
                if (workerState->pollsUntilGuideScan <= 0)
                {
                    writePresence (currentIdentity, clock);
                    refreshActiveGuide();
                    workerState->pollsUntilGuideScan = kGuideScanEveryPolls;
                }
                --workerState->pollsUntilGuideScan;
                publishDisplay (projectDisplay (clock, now));
            }
            wait (static_cast<int> (kProjectionPollMs));
        }
    }

    bool Controller::writePresence (const RuntimeIdentity& currentIdentity, const ClockSnapshot& clock)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_pre_display_presence");
        root->setProperty ("version", "1.0");
        root->setProperty ("instance_id", currentIdentity.instanceId);
        root->setProperty ("name", currentIdentity.name.substring (0, 64));
        root->setProperty ("plugin_version", currentIdentity.pluginVersion);
        root->setProperty ("plugin_format", currentIdentity.pluginFormat);
        root->setProperty ("platform", currentIdentity.platform);
        root->setProperty ("architecture", currentIdentity.architecture);
        root->setProperty ("host_process_id", static_cast<juce::int64> (currentIdentity.hostProcessId));
        root->setProperty ("project_uuid", currentIdentity.projectUuid);
        root->setProperty ("daw_session_uuid", currentIdentity.dawSessionUuid.isEmpty()
                                                   ? juce::var() : juce::var (currentIdentity.dawSessionUuid));

        auto capabilities = new juce::DynamicObject();
        capabilities->setProperty ("guide_protocol", "1.0");
        capabilities->setProperty ("project_clock", true);
        capabilities->setProperty ("audio_alignment", false);
        root->setProperty ("capabilities", juce::var (capabilities));

        const auto now = juce::Time::currentTimeMillis();
        auto clockObject = new juce::DynamicObject();
        clockObject->setProperty ("generation", static_cast<juce::int64> (clock.generation));
        clockObject->setProperty ("position_samples", juce::String (clock.positionSamples));
        clockObject->setProperty ("sample_rate", clock.sampleRate);
        clockObject->setProperty ("block_frames", static_cast<juce::int64> (clock.blockFrames));
        clockObject->setProperty ("playing", clock.playing);
        clockObject->setProperty ("source", clockSourceName (clock.source));
        clockObject->setProperty ("observed_at_ms", workerState->clockObservedAtMs > 0
                                                       ? workerState->clockObservedAtMs : now);
        root->setProperty ("clock", juce::var (clockObject));
        root->setProperty ("lease_expires_at_ms", now + kPresenceLeaseMs);
        const auto presenceFile = transportRoot().getChildFile ("presence")
                                                 .getChildFile (currentIdentity.instanceId + ".json");
        return replaceJsonAtomically (presenceFile, juce::var (root));
    }

    void Controller::refreshActiveGuide()
    {
        const auto root = transportRoot();
        // The group tombstone is the sole explicit clear authority and must win even when a
        // crashed Kirin OS process has not yet removed the old active pointer. A replacement
        // publish keeps this tombstone until its new pointer is completely committed.
        const auto clearValue = parseBoundedJson (
            root.getChildFile ("retired").getChildFile ("kirin_os.clear.json"),
            kMaxPointerBytes);
        const auto* clear = clearValue.getDynamicObject();
        if (clear != nullptr
            && objectString (*clear, "format") == "kirin_pre_display_retired"
            && objectString (*clear, "version") == "1.0"
            && objectString (*clear, "scope") == "group"
            && objectString (*clear, "group_id") == "kirin_os")
        {
            workerState->guide = {};
            return;
        }

        juce::Array<juce::File> pointers;
        pointers.add (root.getChildFile ("active").getChildFile ("kirin_os.json"));

        std::vector<GuideModel> matches;
        for (const auto& pointerFile : pointers)
        {
            const auto pointerValue = parseBoundedJson (pointerFile, kMaxPointerBytes);
            const auto* pointer = pointerValue.getDynamicObject();
            if (pointer == nullptr || objectString (*pointer, "format") != "kirin_pre_display_active"
                || objectString (*pointer, "version") != "1.0")
                continue;
            const auto guideFileName = objectString (*pointer, "guide_file");
            const auto artifactHash = objectString (*pointer, "artifact_sha256");
            const auto guideId = objectString (*pointer, "guide_id");
            const auto contentHash = objectString (*pointer, "content_hash");
            const auto cacheKey = guideFileName + ":" + artifactHash;
            if (! safeGuideFileName (guideFileName) || ! safeHash (artifactHash)
                || ! safeId (guideId) || ! safeHash (contentHash))
                continue;
            std::int64_t revision = 0;
            const auto pointerGroupId = objectString (*pointer, "group_id");
            const auto pointerPayloadKind = objectString (*pointer, "payload_kind");
            if (pointerGroupId != "kirin_os"
                || (pointerPayloadKind != "masking" && pointerPayloadKind != "inspect")
                || ! objectInteger (*pointer, "revision", 1, kMaxSafeJsonInteger, revision))
                continue;
            if (workerState->guide.valid() && workerState->guide.cacheKey == cacheKey
                && workerState->guide.guideId == guideId
                && workerState->guide.contentHash == contentHash
                && workerState->guide.groupId == pointerGroupId
                && workerState->guide.payloadKind == pointerPayloadKind
                && workerState->guide.revision == revision)
            {
                matches.push_back (workerState->guide);
                continue;
            }

            const auto guideFile = root.getChildFile ("guides").getChildFile (guideFileName);
            if (! guideFile.existsAsFile() || guideFile.isSymbolicLink()
                || guideFile.getSize() <= 0 || guideFile.getSize() > kMaxGuideBytes
                || juce::SHA256 (guideFile).toHexString() != artifactHash)
                continue;
            const auto guideValue = parseBoundedJson (guideFile, kMaxGuideBytes);
            const auto* guide = guideValue.getDynamicObject();
            GuideModel parsed;
            if (guide == nullptr || objectString (*guide, "guide_id") != guideId
                || objectString (*guide, "content_hash") != contentHash
                || ! guideTargetsAllPre (*guide)
                || ! parseGuideModel (*guide, cacheKey, parsed))
                continue;
            if (parsed.groupId != "kirin_os" || parsed.groupId != pointerGroupId
                || parsed.payloadKind != pointerPayloadKind
                || parsed.revision != revision)
                continue;
            matches.push_back (std::move (parsed));
        }

        if (matches.size() == 1)
        {
            workerState->guide = std::move (matches.front());
            return;
        }
        // A corrupt/momentarily missing pointer must not erase a guide while the user is
        // adjusting the DAW. The explicit tombstone check above is the only clear path.
    }

    DisplaySnapshot Controller::projectDisplay (const ClockSnapshot& clock, std::int64_t nowMs) const
    {
        const auto& guide = workerState->guide;
        if (! guide.valid())
            return {};
        DisplaySnapshot out;
        out.guideId = guide.guideId;
        out.contentHash = guide.contentHash;
        out.payloadKind = guide.payloadKind;
        const juce::String heading = guide.payloadKind == "masking" ? "MASKING" : "INSPECT";

        std::int64_t projectNs = 0;
        if (! canProjectGuideTime (clock.source)
            || ! projectSamplesToNanoseconds (clock.positionSamples, clock.sampleRate, projectNs))
        {
            out.status = DisplayStatus::waitingForProjectClock;
            out.primary = heading + "  RECEIVED";
            if (! guide.items.empty())
            {
                const auto& first = guide.items.front();
                const auto fact = guide.payloadKind == "inspect" ? first.label : bandText (first);
                out.detail = "NEXT " + timeText (first.startNs) + (fact.isNotEmpty() ? "  ·  " + fact : "");
            }
            else
                out.detail = "No timed items  ·  Guide retained";
            out.primary = compactText (out.primary, 48);
            out.detail = compactText (out.detail, 48);
            return out;
        }

        std::int64_t sourceNs = 0;
        if (! subtractNanoseconds (projectNs, guide.sourceZeroProjectNs, sourceNs))
        {
            out.status = DisplayStatus::waitingForProjectClock;
            out.primary = heading + "  RECEIVED";
            out.detail = "Timeline outside guide range  ·  Guide retained";
            out.primary = compactText (out.primary, 48);
            out.detail = compactText (out.detail, 48);
            return out;
        }
        const GuideItem* chosen = nullptr;
        bool held = false;
        int overlapCount = 0;
        for (const auto& item : guide.items)
        {
            const bool active = containsHalfOpen (item.startNs, item.endNs, sourceNs);
            const bool isHeld = guide.payloadKind == "inspect" && sourceNs >= item.endNs
                             && sourceNs < item.endNs + kInspectHoldNs;
            if (! active && ! isHeld)
                continue;
            const bool itemFocused = item.itemId == guide.focusEventId;
            const bool chosenFocused = chosen != nullptr && chosen->itemId == guide.focusEventId;
            if (chosen == nullptr
                || (itemFocused && ! chosenFocused)
                || (itemFocused == chosenFocused && item.startNs > chosen->startNs))
            {
                chosen = &item;
                held = isHeld && ! active;
            }
            ++overlapCount;
        }
        if (chosen != nullptr)
        {
            out.status = DisplayStatus::active;
            out.primary = heading + "  " + timeText (sourceNs);
            if (guide.payloadKind == "inspect")
                out.primary += "  ·  " + chosen->label;
            else
            {
                const auto band = bandText (*chosen);
                if (band.isNotEmpty()) out.primary += "  ·  " + band;
            }
            out.detail = itemDetail (guide, *chosen, juce::jmax (0, overlapCount - 1));
            if (held) out.detail += (out.detail.isEmpty() ? "HELD" : "  ·  HELD");
        }
        else
        {
            const auto next = std::find_if (guide.items.begin(), guide.items.end(), [sourceNs] (const auto& item)
            {
                return item.startNs > sourceNs;
            });
            if (next != guide.items.end())
            {
                out.status = DisplayStatus::next;
                out.primary = heading + "  NEXT " + timeText (next->startNs);
                out.detail = itemDetail (guide, *next, 0);
                if (guide.payloadKind == "inspect" && next->label.isNotEmpty())
                    out.detail = next->label + (out.detail.isNotEmpty() ? "  ·  " + out.detail : "");
            }
            else
            {
                out.status = DisplayStatus::end;
                out.primary = heading + "  END";
                out.detail = "Guide retained";
            }
        }

        const bool clockStale = workerState->clockObservedAtMs > 0
                             && nowMs - workerState->clockObservedAtMs > kClockStaleMs;
        if (clockStale || ! clock.playing)
            out.detail += (out.detail.isEmpty() ? "PAUSED  ·  Guide retained"
                                                : "  ·  PAUSED");
        out.primary = compactText (out.primary, 48);
        out.detail = compactText (out.detail, 48);
        return out;
    }

    void Controller::publishDisplay (DisplaySnapshot next)
    {
        const juce::ScopedLock lock (displayLock);
        display = std::move (next);
    }

    void Controller::removeOwnPresence()
    {
        juce::File file;
        {
            const juce::ScopedLock lock (identityLock);
            file = ownPresenceFile;
        }
        if (file != juce::File() && file.existsAsFile())
            file.deleteFile();
    }
}
