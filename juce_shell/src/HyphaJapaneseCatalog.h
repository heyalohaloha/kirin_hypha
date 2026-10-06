#pragma once

#include <cstddef>
#include <vector>

// The Japanese for Hypha's English screen text (HyphaLanguage.h, INV-S40). Each entry pairs the
// English exactly as the source writes it with its Japanese, written as a u8 literal. %1 to %3
// stand for values the English carries (a size, a count, a name); the Japanese puts them where
// Japanese word order needs them.
//
// Only prose is here: sentences and phrases that explain, instruct or report. Labels,
// abbreviations, units, values and names stay English and have no entry, so they pass through.
namespace hypha::i18n::catalog
{
struct Entry
{
    const char* english;
    const char* japanese;
};

struct Section
{
    const char* name;
    const Entry* entries;
    std::size_t count;
};

// One section per surface, each in its own source file so it stays small enough to review.
Section observatorySection() noexcept;
Section analysisSection() noexcept;
Section menuSection() noexcept;
Section referenceSection() noexcept;
Section referenceGuideSection() noexcept;
Section referenceAbcvSection() noexcept;
Section blindSection() noexcept;
Section informationSection() noexcept;
Section noticeSection() noexcept;
Section helpLineSection() noexcept;
Section updateSection() noexcept;

inline std::vector<Section> sections()
{
    return { observatorySection(), analysisSection(), menuSection(), referenceSection(),
             referenceGuideSection(), referenceAbcvSection(), blindSection(), informationSection(),
             noticeSection(), helpLineSection(), updateSection() };
}
}
