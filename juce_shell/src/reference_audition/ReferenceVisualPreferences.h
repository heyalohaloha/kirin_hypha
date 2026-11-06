#pragma once
#include <juce_core/juce_core.h>
#include "ReferenceTextEdges.h"
#include <array>
#include <cmath>
#include <cstdint>
namespace hypha::reference_audition
{
struct VisualViewChoice
{
    juce::String sourceHash;
    double start = 0, end = 12;
    bool follow = true, crest = false;
    bool valid() const noexcept
    { return sourceHash.length() == 64 && sourceHash.containsOnly ("0123456789abcdef")
        && std::isfinite (start) && std::isfinite (end) && start >= 0 && end > start && end <= 86400; }
    void write (juce::XmlElement& parent) const
    {
        if (!valid()) return;
        auto* view = parent.createNewChildElement ("View");
        view->setAttribute ("source", sourceHash); view->setAttribute ("start", start); view->setAttribute ("end", end);
        view->setAttribute ("follow", follow); view->setAttribute ("crest", crest);
    }
    static VisualViewChoice read (const juce::XmlElement& parent)
    {
        VisualViewChoice result;
        if (const auto* view = parent.getChildByName ("View"))
            result = { view->getStringAttribute ("source"), view->getDoubleAttribute ("start"),
                view->getDoubleAttribute ("end"), view->getBoolAttribute ("follow", true), view->getBoolAttribute ("crest") };
        return result.valid() ? result : VisualViewChoice {};
    }
};
// B・C の図の A の線の窓。変えるのは A の見せ方だけで、MATCH・追従の窓（matchWindowBlocks）とは別に持つ。
enum class AWindowChoice { seconds3, seconds10, seconds30, cue, all, custom };
inline constexpr std::array<AWindowChoice, 5> aWindowOptions { AWindowChoice::seconds3, AWindowChoice::seconds10,
    AWindowChoice::seconds30, AWindowChoice::cue, AWindowChoice::all };
inline constexpr int aWindowMaximumBlocks = 6'000;  // A を覚えておく上限（100 ms × 6,000 = 10 分）
inline bool validAWindowSeconds (int seconds) noexcept { return seconds >= 3 && seconds <= 600; }
inline int parseAWindowSeconds (const juce::String& text) noexcept
{
    const auto trimmed = reference_text::trimEdges (text);
    if (trimmed.isEmpty() || trimmed.length() > 3 || ! trimmed.containsOnly ("0123456789")) return 0;
    const auto seconds = trimmed.getIntValue();
    return validAWindowSeconds (seconds) ? seconds : 0;
}

inline const char* aWindowLabel (AWindowChoice choice) noexcept
{
    switch (choice)
    {
        case AWindowChoice::seconds3: return "3 S";
        case AWindowChoice::seconds10: return "10 S";
        case AWindowChoice::seconds30: return "30 S";
        case AWindowChoice::cue: return "CUE";
        case AWindowChoice::custom: return "SECONDS";
        case AWindowChoice::all: break;
    }
    return "ALL";
}

// リストを開いたときの役と操作の世代。回答はこれに結び、役の切替・復元・形式の変更・Blind の開始の前に開いたリストの
// 回答を捨てる（ReferenceComparisonController::aWindowTargetCurrent）。
struct AWindowTarget
{
    int slot = 0;
    std::uint64_t epoch = 0;
};

// `cueBlocks` はその役の Cue の長さ（100 ms のブロック数）。MATCH の下限 10 秒は掛けない。
inline int displayWindowBlocks (AWindowChoice choice, int cueBlocks, int seconds = 10) noexcept
{
    switch (choice)
    {
        case AWindowChoice::seconds3: return 30;
        case AWindowChoice::seconds10: return 100;
        case AWindowChoice::seconds30: return 300;
        case AWindowChoice::cue: return cueBlocks;
        case AWindowChoice::custom: return validAWindowSeconds (seconds) ? seconds * 10 : 100;
        case AWindowChoice::all: break;
    }
    return aWindowMaximumBlocks;
}

// 役ごとの窓。既定は B が 10 秒、C が Cue の長さで、既定のままなら state に書かない。
struct AWindowChoices
{
    AWindowChoice reference = AWindowChoice::seconds10, check = AWindowChoice::cue;
    int referenceSeconds = 10, checkSeconds = 30;
    AWindowChoice of (int slot) const noexcept { return slot == 3 ? reference : check; }
    int secondsOf (int slot) const noexcept { return slot == 3 ? referenceSeconds : checkSeconds; }
    int blocksOf (int slot, int cueBlocks) const noexcept { return displayWindowBlocks (of (slot), cueBlocks, secondsOf (slot)); }
    void write (juce::XmlElement& parent) const
    {
        const AWindowChoices standard;
        if (reference == standard.reference && check == standard.check) return;
        auto* windows = parent.createNewChildElement ("AWindow");
        if (reference != standard.reference) windows->setAttribute ("ref", stateName (reference));
        if (check != standard.check) windows->setAttribute ("check", stateName (check));
        if (reference == AWindowChoice::custom) windows->setAttribute ("ref_seconds", referenceSeconds);
        if (check == AWindowChoice::custom) windows->setAttribute ("check_seconds", checkSeconds);
    }
    static AWindowChoices read (const juce::XmlElement& parent)
    {
        AWindowChoices result;
        if (const auto* windows = parent.getChildByName ("AWindow"))
        {
            result.reference = fromState (windows->getStringAttribute ("ref"), result.reference);
            result.check = fromState (windows->getStringAttribute ("check"), result.check);
            const auto readSeconds = [&] (const char* key, AWindowChoice& choice, AWindowChoice fallback, int& seconds)
            {
                if (choice != AWindowChoice::custom) return;
                const auto parsed = parseAWindowSeconds (windows->getStringAttribute (key));
                if (parsed != 0) seconds = parsed;
                else choice = fallback;
            };
            readSeconds ("ref_seconds", result.reference, AWindowChoice::seconds10, result.referenceSeconds);
            readSeconds ("check_seconds", result.check, AWindowChoice::cue, result.checkSeconds);
        }
        return result;
    }
private:
    static const char* stateName (AWindowChoice choice) noexcept
    {
        switch (choice)
        {
            case AWindowChoice::seconds3: return "3s";
            case AWindowChoice::seconds10: return "10s";
            case AWindowChoice::seconds30: return "30s";
            case AWindowChoice::cue: return "cue";
            case AWindowChoice::custom: return "seconds";
            case AWindowChoice::all: break;
        }
        return "all";
    }
    static AWindowChoice fromState (const juce::String& text, AWindowChoice fallback) noexcept
    {
        for (const auto option : aWindowOptions)
            if (text == stateName (option)) return option;
        if (text == "seconds") return AWindowChoice::custom;
        return fallback;
    }
};

// Non-RT preferences survive editor close; source observations never do.
class VisualPreferences final
{
public:
    VisualViewChoice get() const { const juce::ScopedLock lock (mutex); return choice; }
    void set (VisualViewChoice value) { const juce::ScopedLock lock (mutex); choice = value.valid() ? std::move (value) : VisualViewChoice {}; }
    AWindowChoices aWindows() const { const juce::ScopedLock lock (mutex); return windows; }
    void setAWindows (AWindowChoices value) { const juce::ScopedLock lock (mutex); windows = value; }
    // 変わったら true。
    bool setAWindow (int slot, AWindowChoice value, int seconds = 10)
    {
        const juce::ScopedLock lock (mutex);
        auto& held = slot == 3 ? windows.reference : windows.check;
        auto& heldSeconds = slot == 3 ? windows.referenceSeconds : windows.checkSeconds;
        if (held == value && (value != AWindowChoice::custom || heldSeconds == seconds)) return false;
        held = value;
        if (value == AWindowChoice::custom) heldSeconds = seconds;
        return true;
    }
private:
    mutable juce::CriticalSection mutex;
    VisualViewChoice choice;
    AWindowChoices windows;
};
}
