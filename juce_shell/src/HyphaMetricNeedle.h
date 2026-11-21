#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace hypha::metric_needle
{
enum class Face : std::uint8_t { vu, lu, psr, crest, sharp };
enum class Status : std::uint8_t { ready, measuring, unavailable };
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
constexpr bool metric (Face face) noexcept
{ return face == Face::psr || face == Face::crest || face == Face::sharp; }
constexpr Face normaliseFace (int saved) noexcept
{ return saved >= 0 && saved <= 4 ? static_cast<Face> (saved) : Face::vu; }
// Keep vu_lu_choice separate. Legacy 4/5 retain their face but lose the removed I window.
constexpr std::uint8_t normaliseLegacyChoice (int saved) noexcept
{
    const auto clamped = saved < 0 ? 0 : saved > 5 ? 5 : saved;
    return static_cast<std::uint8_t> ((clamped & 1) | ((clamped == 2 || clamped == 3) ? 2 : 0));
}
inline Face restoredFace (std::optional<int> savedFace, int legacyChoice) noexcept
{ return savedFace ? normaliseFace (*savedFace) : (normaliseLegacyChoice (legacyChoice) & 1 ? Face::lu : Face::vu); }
constexpr const char* label (Face face) noexcept
{
    switch (face) {
        case Face::vu: return "VU"; case Face::lu: return "LU";
        case Face::psr: return "PSR"; case Face::crest: return "CREST"; case Face::sharp: return "SHARP";
    }
    return "VU";
}
constexpr const char* unit (Face face) noexcept { return face == Face::sharp ? "acum" : metric (face) ? "dB" : ""; }
inline double extent (Face face) noexcept { return face == Face::sharp ? 1.5 : metric (face) ? 24.0 : missing; }
inline double relative (double measured, double median) noexcept
{
    const auto result = measured - median;
    return std::isfinite (measured) && std::isfinite (median) && std::isfinite (result) ? result : missing;
}
inline std::optional<float> normalized (Face face, double difference) noexcept
{
    const auto range = extent (face);
    if (!std::isfinite (range) || !std::isfinite (difference)) return std::nullopt;
    return static_cast<float> ((std::clamp (difference, -range, range) + range) / (2.0 * range));
}
inline int rangeEdge (Face face, double difference) noexcept
{
    const auto range = extent (face);
    if (!std::isfinite (range) || !std::isfinite (difference)) return 0;
    return difference < -range ? -1 : difference > range ? 1 : 0;
}
struct Band { double low = missing, high = missing; };
struct Frame
{
    Face face = Face::psr;
    Status status = Status::unavailable;
    bool ownIsPre = false;
    // All values must have this face's definition. PRE/own are an exact pair;
    // REF is admitted by the caller's FOLLOW/source/position authority checks.
    double own = missing, pre = missing, reference = missing;
    double median = missing;
    Band band;
    std::string song;
    std::string reason; // Fixed diagnostic code, never a path or display text.
};
struct Reading
{
    double own = missing, pre = missing, reference = missing;
    std::optional<float> ownPosition, prePosition, referencePosition;
    std::optional<Band> relativeBand;
};
// Unknown/private diagnostic content must never reach the screen.
inline const char* reasonText (const std::string& code) noexcept
{
    if (code == "select_reference_post") return "Choose Reference in POST";
    if (code == "preparing") return "Preparing measurement";
    if (code == "unsupported_format") return "Unsupported audio format";
    if (code == "read_failed") return "Audio could not be read";
    if (code == "source_changed") return "Audio source changed";
    if (code == "private_copy_required") return "Private copy required";
    if (code == "capacity_exceeded") return "Audio exceeds scan limit";
    if (code == "audio_too_short") return "Audio is too short";
    if (code == "cue_too_short") return "Cue is too short";
    if (code == "no_valid_value") return "No measurable value";
    if (code == "analysis_failed") return "Measurement failed";
    return "UNAVAILABLE";
}
inline Reading reading (const Frame& frame) noexcept
{
    Reading result;
    if (!metric (frame.face) || frame.status != Status::ready || !std::isfinite (frame.median)) return result;
    result.own = relative (frame.own, frame.median);
    result.pre = frame.ownIsPre ? missing : relative (frame.pre, frame.median);
    result.reference = relative (frame.reference, frame.median);
    result.ownPosition = normalized (frame.face, result.own);
    result.prePosition = normalized (frame.face, result.pre);
    result.referencePosition = normalized (frame.face, result.reference);
    const auto low = relative (frame.band.low, frame.median), high = relative (frame.band.high, frame.median);
    if (std::isfinite (low) && std::isfinite (high) && low <= high
        && high >= -extent (frame.face) && low <= extent (frame.face)) result.relativeBand = Band {low, high};
    return result;
}
}
