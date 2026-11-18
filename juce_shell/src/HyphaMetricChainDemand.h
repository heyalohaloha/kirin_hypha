#pragma once
#include "HyphaUiPreferences.h"
#include "HyphaMetricChainOwner.h"
#include "HyphaVuCalibrationFile.h"

namespace hypha::metric_needle
{
// PRE requests the existing POST-owned Reference worker, never a second reader.
// Files are keyed by the established chain locator, so this also works across
// separate PRE/POST plugin images/processes. Non-RT control/message thread only.
class ChainDemand final
{
public:
    explicit ChainDemand (juce::File directory = ui_preferences::defaultFile().getSiblingFile ("metric-demand"))
        : root (std::move (directory)), token (juce::Uuid().toString()) {}
    ~ChainDemand() { withdraw(); }
    std::shared_ptr<void> ownership (const juce::String& scope)
    {
        if (scope != ownedScope) { owner.reset(); ownedScope = scope; }
        if (!validScope (scope)) return {};
        if (!owner) {
            auto next = std::make_shared<ChainOwner> (root.getChildFile (scope).getChildFile ("owner.lock"));
            if (next->acquired()) owner = std::move (next);
        }
        return owner;
    }
    // Callers revoke the controller lease before releasing an ineligible owner.
    // Detached work keeps its own lease until cancellation has really completed.
    std::shared_ptr<void> selectionOwnership (const juce::String& scope, bool selected, bool wanted)
    { return ownership (selected && wanted ? scope : juce::String {}); }
    bool serviceDue (juce::int64 now = juce::Time::currentTimeMillis())
    {
        if (lastService > 0 && now >= lastService && now - lastService < 250) return false;
        lastService = now; return true;
    }
    void local (bool value) noexcept { localRequested = value; }
    bool local() const noexcept { return localRequested; }
    void request (const juce::String& scope, std::uint32_t rate, std::uint32_t channels,
                  bool wanted, juce::int64 now = juce::Time::currentTimeMillis())
    {
        if (scope != requestedScope || rate != requestedRate || channels != requestedChannels || !wanted)
        { withdraw(); requestedScope = scope; requestedRate = rate; requestedChannels = channels; }
        if (!wanted || !validScope (scope) || !validFormat (rate, channels)) return;
        if (lastWrite > 0 && now >= lastWrite && now - lastWrite < 250) return;
        const auto directory = root.getChildFile (scope);
        if (root.isSymbolicLink() || directory.isSymbolicLink() || directory.createDirectory().failed()
            || root.isSymbolicLink() || directory.isSymbolicLink()) return;
        const auto text = juce::String ("KHMD1\n") + scope + "\n" + juce::String (rate) + " "
            + juce::String (channels) + " " + juce::String (now) + "\n";
        const auto target = directory.getChildFile (token + ".req");
        if (vu_calibration::writeAtomicText (target, text)) { ownFile = target; lastWrite = now; }
    }
    bool requested (const juce::String& scope, std::uint32_t rate, std::uint32_t channels,
                    juce::int64 now = juce::Time::currentTimeMillis()) const
    {
        if (!validScope (scope) || !validFormat (rate, channels)) return false;
        if (root.isSymbolicLink() || root.getChildFile (scope).isSymbolicLink()) return false;
        const auto directory = root.getChildFile (scope);
        const auto live = [&] (const juce::File& file)
        {
            if (file == juce::File {} || file.getParentDirectory() != directory
                || file.isSymbolicLink() || file.getSize() <= 0 || file.getSize() > 128) return false;
            const auto lines = juce::StringArray::fromLines (file.loadFileAsString());
            if (lines.size() < 3 || lines.size() > 4 || lines[0] != "KHMD1" || lines[1] != scope
                || (lines.size() == 4 && lines[3].isNotEmpty())) return false;
            const auto fields = juce::StringArray::fromTokens (lines[2], " ", {});
            if (fields.size() != 3 || fields[0] != juce::String (rate) || fields[1] != juce::String (channels)
                || fields[2].isEmpty() || fields[2].length() > 18 || !fields[2].containsOnly ("0123456789")) return false;
            const auto written = fields[2].getLargeIntValue();
            return written > 0 && now >= written && now - written < 1500;
        };
        // Cache one live peer, never its validity: revalidate its lease each tick.
        // Scan at most 63 additional contents, rotating past crash leftovers.
        if (live (lastPeer)) return true;
        lastPeer = juce::File {};
        const auto files = directory.findChildFiles (juce::File::findFiles, false, "*.req");
        if (files.isEmpty()) { peerCursor = 0; return false; }
        const auto count = static_cast<std::size_t> (files.size());
        for (std::size_t n = 0; n < std::min<std::size_t> (63, count); ++n)
        {
            const auto& file = files[static_cast<int> (peerCursor++ % count)];
            if (live (file)) { lastPeer = file; return true; }
        }
        return false;
    }
    void withdraw()
    {
        if (ownFile != juce::File {}) ownFile.deleteFile();
        ownFile = juce::File {}; lastWrite = 0;
    }
private:
    static bool validScope (const juce::String& scope)
    { return scope.length() == 64 && scope.containsOnly ("0123456789abcdef"); }
    static bool validFormat (std::uint32_t rate, std::uint32_t channels)
    { return rate >= 8000 && rate <= 384000 && rate % 10 == 0 && (channels == 1 || channels == 2); }
    juce::File root, ownFile;
    mutable juce::File lastPeer;
    mutable std::size_t peerCursor = 0;
    juce::String token, requestedScope;
    std::uint32_t requestedRate = 0, requestedChannels = 0;
    juce::int64 lastWrite = 0, lastService = 0;
    bool localRequested = false;
    juce::String ownedScope;
    std::shared_ptr<ChainOwner> owner;
};
}
