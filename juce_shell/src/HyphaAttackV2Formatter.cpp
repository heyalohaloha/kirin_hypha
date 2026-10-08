#include "HyphaAttackV2Formatter.h"
#include "HyphaLanguage.h"
#include <cmath>
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>

namespace hypha::attack_v2
{
juce::String words (const char* english, const char* japanese)
{
    return juce::String::fromUTF8 (i18n::current() == i18n::Language::japanese ? japanese : english);
}

bool validInterval (const KirinSnapshotInterval& interval) noexcept
{
    const auto valid = [] (const KirinSnapshotEndpoint& end) {
        return end.kind <= KIRIN_ENDPOINT_POSITIVE_INFINITY && end.closed <= 1
            && (end.kind == KIRIN_ENDPOINT_FINITE ? std::isfinite (end.value)
                                                  : end.closed == 0 && (end.value <= 0 && end.value >= 0)); };
    if (! valid (interval.lower) || ! valid (interval.upper) || interval.unit > KIRIN_INTERVAL_DECIBELS
        || interval.lower.kind == KIRIN_ENDPOINT_POSITIVE_INFINITY
        || interval.upper.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY) return false;
    if (interval.lower.kind == KIRIN_ENDPOINT_FINITE && interval.upper.kind == KIRIN_ENDPOINT_FINITE)
        return interval.lower.value < interval.upper.value
            || ((interval.lower.value <= interval.upper.value && interval.lower.value >= interval.upper.value) && interval.lower.closed && interval.upper.closed);
    return true;
}

FormattedInterval formatInterval (const KirinSnapshotInterval& interval, int decimals,
                                  const juce::String& unit, bool signedValue, bool exact)
{
    FormattedInterval result;
    result.raw = interval;
    result.unit = unit;
    if (! validInterval (interval)) return result;
    const bool lowerFinite = interval.lower.kind == KIRIN_ENDPOINT_FINITE;
    const bool upperFinite = interval.upper.kind == KIRIN_ENDPOINT_FINITE;
    if (! lowerFinite && ! upperFinite) return result; // Whole real line has no main scalar.
    const auto magnitude = std::max (lowerFinite ? std::abs (interval.lower.value) : 0.0,
                                     upperFinite ? std::abs (interval.upper.value) : 0.0);
    if (magnitude >= 1000.0)
    {
        result.exponent = static_cast<int> (std::floor (std::log10 (magnitude)));
        decimals = 2;
        const std::array<const char*, 10> digits { u8"⁰", u8"¹", u8"²", u8"³", u8"⁴", u8"⁵", u8"⁶", u8"⁷", u8"⁸", u8"⁹" };
        juce::String exponent;
        for (const auto digit : juce::String (result.exponent)) exponent += juce::String::fromUTF8 (digits[static_cast<std::size_t> (digit - '0')]);
        result.unit = juce::String::fromUTF8 (u8"×10") + exponent + " " + unit;
    }
    const long double divisor = std::pow (10.0L, result.exponent);
    const long double scale = std::pow (10.0L, decimals);
    const auto number = [&] (double raw, bool lower) {
        const auto scaled = static_cast<long double> (raw) / divisor * scale;
        auto rounded = exact ? std::round (scaled) : lower ? std::floor (scaled) : std::ceil (scaled);
        if (rounded <= 0 && rounded >= 0) rounded = 0; // normalize -0 without changing an endpoint's type.
        std::ostringstream stream;
        stream.imbue (std::locale::classic());
        if (signedValue && rounded >= 0) stream << '+';
        stream << std::fixed << std::setprecision (decimals) << rounded / scale;
        return juce::String (stream.str()).replace ("-", juce::String::fromUTF8 (u8"−")); };
    if (exact && lowerFinite && upperFinite && (interval.lower.value <= interval.upper.value && interval.lower.value >= interval.upper.value)
        && interval.lower.closed && interval.upper.closed)
        result.value = number (interval.lower.value, true);
    else if (! upperFinite)
        result.value = juce::String::fromUTF8 (interval.lower.closed ? u8"≥" : ">")
                     + number (interval.lower.value, true);
    else if (! lowerFinite)
        result.value = juce::String::fromUTF8 (interval.upper.closed ? u8"≤" : "<")
                     + number (interval.upper.value, false);
    else
        result.value = juce::String (interval.lower.closed ? "[" : "(")
                     + number (interval.lower.value, true) + "," + number (interval.upper.value, false)
                     + (interval.upper.closed ? "]" : ")");
    result.valid = true;
    return result;
}

juce::String reasonText (std::uint8_t reason, bool detailed)
{
    switch (reason)
    {
        case KIRIN_REASON_NONE: return {};
        case KIRIN_REASON_NO_PAIR: return words ("No PRE", u8"PREなし");
        case KIRIN_REASON_SILENT: return words ("Silent", u8"帯域無音");
        case KIRIN_REASON_BOTH_SILENT: return words ("Both silent", u8"両側無音");
        case KIRIN_REASON_RINGING: return words ("Ringing", u8"前音残響");
        case KIRIN_REASON_NEXT_HIT: return words ("Next hit", u8"次打まで");
        case KIRIN_REASON_NOT_KEPT: return words ("Not measured", u8"未計測");
        case KIRIN_REASON_MAPPING: return words ("No mapping", u8"対応不明");
        case KIRIN_REASON_CLOCK: return words ("Clock changed", u8"時計変更");
        case KIRIN_REASON_SOURCE_CHANGED: return words ("Source changed", u8"音源変更");
        case KIRIN_REASON_WORKER_UNAVAILABLE: return detailed ? words ("Worker unavailable", u8"計測応答なし") : words ("No worker", u8"計測応答なし");
        case KIRIN_REASON_REQUEST_DEADLINE: return words ("Request expired", u8"取得期限切れ");
        case KIRIN_REASON_WAITING_AUDIO:
        case KIRIN_REASON_WAITING_SERVICE:
        case KIRIN_REASON_WAITING_PUBLICATION:
            return detailed ? words (reason == KIRIN_REASON_WAITING_AUDIO ? "Waiting for audio"
                                      : reason == KIRIN_REASON_WAITING_SERVICE ? "Waiting for worker"
                                      : "Waiting for publication",
                                      reason == KIRIN_REASON_WAITING_AUDIO ? u8"音声待ち"
                                      : reason == KIRIN_REASON_WAITING_SERVICE ? u8"計測待ち" : u8"公開待ち")
                            : words ("Measuring", u8"取得中");
        case KIRIN_REASON_SEMANTICS: return words ("Update PRE", u8"PRE更新");
        case KIRIN_REASON_LONG_TAIL: return words ("Long tail", u8"長い余韻");
        case KIRIN_REASON_AUDIO_END: return words ("Audio ended", u8"入力終了");
        default: return words ("Unknown", u8"不明");
    }
}
}
