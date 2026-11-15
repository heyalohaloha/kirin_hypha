// Keep the RT guard sensitive to wait operations and references while allowing "awaits".
pub(super) fn has_wait_operation(code: &str) -> bool {
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
        let wait = name != "awaits" && (name.contains("wait") || name.starts_with("WaitFor"));
        if wait {
            return true;
        }
    }
    false
}

#[test]
fn rt_wait_guard_distinguishes_identifiers_from_blocking_calls() {
    assert!(!has_wait_operation("alignment.awaits (block);"));
    let missed: Vec<_> = [
        "pthread_cond_timedwait(&condition, &mutex, &deadline);",
        "sem_timedwait(&semaphore, &deadline);",
        "waitpid(pid, &status, 0);",
        "awaits_waiting();",
        "condition.wait(lock);",
        "condition.wait_for (lock, timeout);",
        "std::atomic_wait (&flag, value);",
        "auto fn = &std::condition_variable::wait;",
        "co_await event;",
        "WaitForSingleObject(handle, 1);",
    ]
    .into_iter()
    .filter(|call| !has_wait_operation(call))
    .collect();
    assert!(missed.is_empty(), "missed {missed:?}");
}
