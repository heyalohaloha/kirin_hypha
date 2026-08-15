#include "PreDisplayProtocol.h"

#include <array>

namespace hypha::pre_display
{
    bool canonicalIsoInstant (const juce::String& value)
    {
        if (value.length() != 24 || value[4] != '-' || value[7] != '-'
            || value[10] != 'T' || value[13] != ':' || value[16] != ':'
            || value[19] != '.' || value[23] != 'Z')
            return false;
        for (int index = 0; index < value.length(); ++index)
        {
            if (index == 4 || index == 7 || index == 10 || index == 13
                || index == 16 || index == 19 || index == 23)
                continue;
            if (! juce::CharacterFunctions::isDigit (value[index]))
                return false;
        }
        const auto year = value.substring (0, 4).getIntValue();
        const auto month = value.substring (5, 7).getIntValue();
        const auto day = value.substring (8, 10).getIntValue();
        const auto hour = value.substring (11, 13).getIntValue();
        const auto minute = value.substring (14, 16).getIntValue();
        const auto second = value.substring (17, 19).getIntValue();
        const auto millisecond = value.substring (20, 23).getIntValue();
        if (year < 1 || month < 1 || month > 12 || hour > 23
            || minute > 59 || second > 59 || millisecond > 999)
            return false;
        static constexpr std::array<int, 12> daysPerMonth {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
        };
        auto maximumDay = daysPerMonth[static_cast<std::size_t> (month - 1)];
        const bool leapYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        if (month == 2 && leapYear)
            ++maximumDay;
        return day >= 1 && day <= maximumDay;
    }
}
