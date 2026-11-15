// Keep the RT guard sensitive to calls while allowing identifiers containing "wait".
pub(super) fn has_wait_call(code: &str) -> bool {
    // Inspect complete identifiers, so `awaits` is not mistaken for a wait operation.
    for (start, _) in code.match_indices(|c: char| c.is_ascii_alphabetic() || c == '_') {
        if start > 0 && code.as_bytes()[start - 1].is_ascii_alphanumeric()
            || start > 0 && code.as_bytes()[start - 1] == b'_'
        {
            continue;
        }
        let rest = &code[start..];
        let end = rest
            .find(|c: char| !c.is_ascii_alphanumeric() && c != '_')
            .unwrap_or(rest.len());
        let name = &rest[..end];
        let wait = name == "wait"
            || name.starts_with("wait_")
            || name.ends_with("_wait")
            || name.contains("_wait_");
        if wait && rest[end..].trim_start().starts_with(['(', '<']) {
            return true;
        }
    }
    false
}

#[test]
fn rt_wait_guard_distinguishes_identifiers_from_blocking_calls() {
    assert!(!has_wait_call("alignment.awaits (block);"));
    for call in [
        "condition.wait(lock);",
        "condition.wait_for (lock, timeout);",
        "std::atomic_wait (&flag, value);",
    ] {
        assert!(has_wait_call(call), "missed {call}");
    }
}
