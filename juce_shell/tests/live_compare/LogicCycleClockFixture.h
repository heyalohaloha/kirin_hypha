#pragma once
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// Callback clocks measured in Logic Pro 12.2 (AU, 96 kHz, 240 BPM, a 4-bar cycle, PRE -> a
// 4096-sample PDC delay or nothing -> POST, playback from the project start, then stop). Only
// transport numbers are kept: the render clock, project sample, playing/gap and the reported
// PPQ in 1/15360-quarter steps, in host callback order (PRE, POST, PRE, ...).
//
// "A n" repeats n callbacks that follow Logic's measured rule exactly: render and project advance
// by the block, the cycle moves project back by exactly its length, and PPQ is the project
// position rounded away from zero to the 1/15360 grid. "R clock,project,playing,gap,ppqSteps" is
// one callback as measured. The encoding was checked to reproduce every measured row.
namespace logic_cycle_clock
{
struct Trial
{
    const char* name;
    int rate, frames;
    double bpm, start, end;
    bool cycle;
    std::uint32_t preLatency, postLatency;
    int callbacks;
    const char* rows;
};

inline const Trial trials[] {
    { "cycle, PDC 4096, 2048-frame callbacks", 96000, 2048, 240, 0, 16, true, 4781, 685, 1294,
      "R4096,-703,1,1,-450 R0,-4799,1,1,-3072 R6144,1344,1,0,861 A4 R6144,1344,1,0,861 A1278 "
      "R1320960,154040,0,0,98585 A1 R1323008,154040,0,0,98585 A1 R1325056,154040,0,0,98585 "
      "R1320960,154040,0,0,98585 R1327104,154040,0,0,98585 R1323008,154040,0,0,98585 " },
    { "cycle, PDC 4096, 512-frame callbacks", 96000, 512, 240, 0, 16, true, 4781, 685, 5154,
      "R4096,-703,1,1,-450 R0,-4799,1,1,-3072 R4608,-191,1,0,-122 A1 R5120,320,1,0,205 A2 "
      "R1536,-3263,1,0,-2088 A5 R3072,-1727,1,0,-1105 A5 R4608,-191,1,0,-122 A1 R5120,320,1,0,205 A5112 "
      "R1318400,154340,0,0,98777 A1 R1318912,154340,0,0,98777 A1 R1319424,154340,0,0,98777 A1 "
      "R1319936,154340,0,0,98777 A1 R1320448,154340,0,0,98777 A1 R1320960,154340,0,0,98777 A1 "
      "R1321472,154340,0,0,98777 A1 R1321984,154340,0,0,98777 A1 R1322496,154340,0,0,98777 "
      "R1318400,154340,0,0,98777 R1323008,154340,0,0,98777 R1318912,154340,0,0,98777 " },
    { "cycle, no PDC, 512-frame callbacks", 96000, 512, 240, 0, 16, true, 685, 685, 5140,
      "R0,-767,1,1,-491 R0,-767,1,1,-491 R512,-255,1,0,-163 R512,-255,1,0,-163 R1024,256,1,0,164 "
      "R1024,256,1,0,164 A5130 R1314816,159015,0,0,101769 R1314816,159015,0,0,101769 "
      "R1315328,159015,0,0,101769 R1315328,159015,0,0,101769 " },
    { "Cycle off, PDC 4096, 512-frame callbacks", 96000, 512, 240, 0, 16, false, 4781, 685, 5154,
      "R4096,-703,1,1,-450 R0,-4799,1,1,-3072 R4608,-191,1,0,-122 A1 R5120,320,1,0,205 A2 "
      "R1536,-3263,1,0,-2088 A5 R3072,-1727,1,0,-1105 A5 R4608,-191,1,0,-122 A1 R5120,320,1,0,205 A5112 "
      "R1318400,1306015,0,0,835849 A1 R1318912,1306015,0,0,835849 A1 R1319424,1306015,0,0,835849 A1 "
      "R1319936,1306015,0,0,835849 A1 R1320448,1306015,0,0,835849 A1 R1320960,1306015,0,0,835849 A1 "
      "R1321472,1306015,0,0,835849 A1 R1321984,1306015,0,0,835849 A1 R1322496,1306015,0,0,835849 "
      "R1318400,1306015,0,0,835849 R1323008,1306015,0,0,835849 R1318912,1306015,0,0,835849 " },
};

struct Row
{
    int role = 0;
    std::int64_t clock = 0, project = 0, ppqSteps = 0;
    bool playing = false, gap = false;
};

// Logic's measured PPQ: the exact project position rounded away from zero to 1/15360 quarter.
inline std::int64_t ppqSteps (const Trial& t, std::int64_t project)
{
    const auto numerator = project * 15360 * static_cast<std::int64_t> (t.bpm);
    const auto denominator = static_cast<std::int64_t> (60) * t.rate;
    const auto whole = numerator / denominator, rest = numerator % denominator;
    return rest == 0 ? whole : numerator > 0 ? whole + 1 : whole - 1;
}

inline std::int64_t cycleLength (const Trial& t) { return static_cast<std::int64_t> ((t.end - t.start) * 60.0 * t.rate / t.bpm); }
inline std::int64_t cycleEnd (const Trial& t) { return static_cast<std::int64_t> (t.end * 60.0 * t.rate / t.bpm); }

// The next callback of the same role under Logic's measured rule.
inline Row next (const Trial& t, const Row& previous)
{
    Row r = previous;
    r.clock += t.frames;
    r.project += t.frames;
    if (t.cycle && r.project >= cycleEnd (t)) r.project -= cycleLength (t);
    r.playing = true; r.gap = false;
    r.ppqSteps = ppqSteps (t, r.project);
    return r;
}

inline std::vector<Row> decode (const Trial& t)
{
    std::vector<Row> rows;
    Row last[2];
    std::istringstream in (t.rows);
    std::string token;
    while (in >> token)
    {
        if (token[0] == 'A')
            for (long n = std::strtol (token.c_str() + 1, nullptr, 10); n > 0; --n)
            {
                const int role = static_cast<int> (rows.size() % 2);
                rows.push_back (last[role] = next (t, last[role]));
            }
        else
        {
            Row r; r.role = static_cast<int> (rows.size() % 2);
            long long fields[5] {};
            std::istringstream values (token.substr (1));
            for (auto& field : fields) { values >> field; values.ignore (1); }
            r.clock = fields[0]; r.project = fields[1]; r.playing = fields[2] != 0; r.gap = fields[3] != 0;
            r.ppqSteps = fields[4];
            rows.push_back (last[r.role] = r);
        }
    }
    return rows;
}
}
