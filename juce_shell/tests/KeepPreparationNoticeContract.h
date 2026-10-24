#pragma once

#include "../src/HyphaJapaneseCatalog.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace hypha::tests
{
// These are the fixed summaries sent by the Rust Keep controls. Both languages must fit a
// 128-byte notice buffer including its terminator; diagnostic details never belong here.
inline void verifyKeepPreparationNotices()
{
    const auto section = i18n::catalog::noticeSection();
    for (const auto* english : {
             "PRE and POST timed out. Try Keep again.",
             "Cannot write Keep. Check folder access.",
             "Keep storage is full. Free disk space.",
             "Keep files unavailable. Check save folder.",
             "Keep setup failed. Select PRE again.",
             "Keep could not start. Try again.",
             "Choose a PRE before Keep.",
             "PRE unavailable. Choose PRE again.",
             "No pairs ready. Select PRE, then retry.",
             "Another Keep is active" })
    {
        const i18n::catalog::Entry* found = nullptr;
        for (std::size_t index = 0; index < section.count; ++index)
            if (std::string_view (section.entries[index].english) == english)
                found = &section.entries[index];
        if (found == nullptr || std::strlen (english) >= 128
            || std::strlen (found->japanese) == 0 || std::strlen (found->japanese) >= 128
            || std::string_view (found->japanese) == english)
        {
            std::cerr << "Keep preparation notice missing or too long: " << english << '\n';
            std::exit (EXIT_FAILURE);
        }
    }
}
}
