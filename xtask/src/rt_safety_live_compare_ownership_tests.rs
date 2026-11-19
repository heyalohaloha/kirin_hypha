use super::{function_body, RING_H, SESSION_H};

pub(super) fn verify_ownership() {
    let mapping = include_str!("../../juce_shell/src/live_compare/LiveCompareSharedRing.cpp");
    let header = include_str!("../../juce_shell/src/live_compare/LiveCompareSharedRing.h");
    let claim = include_str!("../../juce_shell/src/live_compare/LiveCompareOwnerClaim.h");
    let windows = include_str!("../../juce_shell/src/live_compare/LiveCompareOwnerClaim.cpp");
    assert!(header.contains("std::unique_ptr<OwnerClaim> claim;"));
    assert!(!header.contains("#include \"LiveCompareOwnerClaim.h\""));
    assert!(!RING_H.contains("OwnerClaim") && !SESSION_H.contains("OwnerClaim"));
    assert!(!mapping.contains("mapped = fresh ?"));
    assert!(mapping.contains("if (! ownership->acquire (pairKey)) return false;"));
    assert!(mapping.contains("if (existing == nullptr || ! fresh)"));
    assert!(mapping.contains("O_CREAT | O_EXCL | O_RDWR"));
    assert!(claim.contains("LOCK_EX | LOCK_NB") && claim.contains("O_NOFOLLOW"));
    assert!(claim.contains("auto candidate = windows_detail::acquire (name);"));
    let acquire = function_body(windows, "void* acquire (");
    assert!(acquire.contains("GetLastError() != ERROR_ALREADY_EXISTS"));
    assert!(acquire.contains("if (! fresh) { CloseHandle (candidate); return nullptr; }"));
    assert_eq!(mapping.matches("claim.reset();").count(), 2);
}
