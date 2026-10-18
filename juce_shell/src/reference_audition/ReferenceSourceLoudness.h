#pragma once

namespace hypha::reference_audition
{
    // Kirin OS measures a file with its own channels (ITU-R BS.1770). A mono file played on both
    // channels of a stereo track sums two equal channel powers, so the played copy measures
    // 10*log10(2) LU above the file's value. MATCH compares A with what is played.
    constexpr double playedLoudnessOffsetLu (int fileChannels, int trackChannels) noexcept
    {
        return fileChannels == 1 && trackChannels == 2 ? 3.010299956639812 : 0.0;
    }
}
