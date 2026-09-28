// Live PRE/POST compare: the Audio Thread transport and correspondence rules stay free of
// allocation, locks, blocking I/O and thread waits (AGENTS R-12, INV-LC1 to INV-LC3).

const RING_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareRing.h");
const CORRESPONDENCE_H: &str =
    include_str!("../../juce_shell/src/live_compare/LiveCompareCorrespondence.h");
const CLOCK_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareClock.h");

fn without_line_comments(source: &str) -> String {
    source
        .lines()
        .map(|line| line.split("//").next().unwrap_or(""))
        .collect::<Vec<_>>()
        .join("\n")
}

#[test]
fn live_compare_audio_thread_code_avoids_blocking_work() {
    for (name, source) in [
        ("LiveCompareRing.h", RING_H),
        ("LiveCompareCorrespondence.h", CORRESPONDENCE_H),
        ("LiveCompareClock.h", CLOCK_H),
    ] {
        let code = without_line_comments(source);
        for forbidden in [
            "mutex",
            ".lock(",
            ".lock (",
            "new ",
            "delete ",
            "malloc",
            "calloc",
            "realloc",
            "free(",
            "std::vector",
            "std::string",
            "std::function",
            "std::thread",
            "std::filesystem",
            "fstream",
            "fopen",
            "printf",
            "sleep",
            "wait",
            "juce::",
        ] {
            assert!(
                !code.contains(forbidden),
                "{name} must not contain {forbidden}"
            );
        }
    }
}

#[test]
fn live_compare_shared_state_is_atomic_and_proven_before_output() {
    for required in [
        "static_assert (std::atomic<float>::is_always_lock_free);",
        "std::atomic<float> samples[",
        "h.seq.store (seq + 1, std::memory_order_relaxed);",
        "h.seq.store (seq + 2, std::memory_order_release);",
    ] {
        assert!(
            RING_H.contains(required),
            "LiveCompareRing.h lost {required}"
        );
    }
    for required in [
        "if (! ring.matches (pairKey, sampleRate))",
        "if (block.afterGap)",
        "profile.invalidateOnDisagreement",
        "if (start < runStart)",
        "return Verdict::torn;",
    ] {
        assert!(
            CORRESPONDENCE_H.contains(required),
            "LiveCompareCorrespondence.h lost {required}"
        );
    }
    assert!(CLOCK_H.contains("ClockBasis::pluginFrames"));
}
