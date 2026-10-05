#pragma once

// 300% の B・C・V の画面で項目を指したときに下の行に出す説明（HyphaReferenceHelp.h）。何を測っていて何に使うかを
// 一行で言う（良し悪しは書かない。R-22）。日本語は HyphaJapaneseReferenceAbcv.cpp。どれも 300% の足元の行に
// 両言語で収まる（ReferenceHoverHelpTest.h）。
namespace hypha::reference_ui::help_text
{
// 範囲の帯（HyphaReferenceRangeStrips.cpp）。
inline constexpr const char* strips = "Bars: p10 to p90 of each window, the mark is the median. A gold, the compared side cyan.";
inline constexpr const char* crest = "Crest: how far peaks rise above the average (true peak over RMS), moment by moment.";
inline constexpr const char* movement = "Loudness movement: how far the 3-second loudness swings around its median.";
inline constexpr const char* momentary = "Momentary loudness every 0.4 s, with the compared side at the level it plays at.";
inline constexpr const char* width = "Width: side against mid, up to 150%. Shows how wide the stereo image is.";
inline constexpr const char* correlation = "Correlation: how alike left and right move. +1 the same, 0 unrelated, -1 opposite.";
inline constexpr const char* peak = "Peak of each moment (dBFS), with the compared side at the level it plays at.";
inline constexpr const char* rms = "RMS of each moment (dBFS), the body of the sound, with the compared side at its playing level.";
inline constexpr const char* onset = "Onset: how sharply the level rises from the moment before. Shows the attacks.";
inline constexpr const char* attack = "Attack: how far each moment's peak stands above its loudness (LUFS-M); limiting lowers it.";
inline constexpr const char* timeLines = "The first fact over the last 30 seconds: A gold, V cyan, scaled to the lines.";

// スペクトルと 4 帯域（HyphaReferenceCueSummary.cpp・HyphaReferenceVersionPage.cpp・HyphaReferenceBalance.cpp）。
inline constexpr const char* cueSpectrum = "Spectrum of C's Cue and the same length of recent A, with C at the level it plays at.";
inline constexpr const char* versionSpectrum = "Spectrum of A and V over the same aligned section, with V at the level it plays at.";
inline constexpr const char* balance = "Balance: A's last 10 seconds, the chosen song and the B set's range, at B's level.";
inline constexpr const char* blauert = "Blauert's bands: A's 1 kHz against 300-400 Hz and 3-4 kHz, bands tied to sensed distance.";
inline constexpr const char* cueBands = "Four bands: how much more or less A has than C in each band, after matching level.";
inline constexpr const char* versionBands = "Four bands: how much more or less A has than V in each band, over the same section.";

// C の画面のほかの項目（HyphaReferenceComponent.cpp・HyphaReferenceCheckPage.cpp）。
inline constexpr const char* integrated = "Integrated loudness of A's recent window and of C, and how A compares with C.";
inline constexpr const char* truePeak = "Highest true peak of A and of C at the level C plays at, and how A compares.";
inline constexpr const char* cueBar = "Where C's Cue sits in the song and its loop. The line is C's playing position.";
inline constexpr const char* match = "The gain C plays at: matched to A's recent window, or the gain it plays at now.";
inline constexpr const char* checkTabs = "Checks of this CHECK SET from Kirin OS. A tab changes C's Check and keeps the song.";

// V と B の画面の部品（HyphaReferenceHoverHelp.cpp）。
inline constexpr const char* versionTabs = "WHOLE shows the whole song; the other tabs compare A and V over the same section.";
inline constexpr const char* whole = "The whole song: A above V, peak outside and RMS inside. Select a region to compare.";
inline constexpr const char* songs = "Songs of the B set: the LUFS-I of each Cue and the gain MATCH would play it at.";

// 説明の全部（足したらここにも足す。試験は全部を両言語で確かめる：足元の行に収まる・日本語に英語が残らない）。
inline constexpr const char* all[] { strips, crest, movement, momentary, width, correlation, peak, rms, onset, attack,
                                     timeLines, cueSpectrum, versionSpectrum, balance, blauert, cueBands, versionBands,
                                     integrated, truePeak, cueBar, match, checkTabs, versionTabs, whole, songs };
}
