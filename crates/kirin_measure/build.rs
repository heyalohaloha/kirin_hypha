//! Build script — HMAC key embedding surface (T-7 (a) / G-58-06).
//!
//! The actual key loader lives in `src/identity.rs::hmac_key` via
//! `option_env!("KIRIN_HYPHA_HMAC_KEY")`. This script only:
//!   1. Re-runs the compile when the env var changes (incremental
//!      builds must not cache a stale key),
//!   2. Prints which source the baked-in key came from so the build
//!      log is self-documenting for packaging/CI.

fn main() {
    println!("cargo:rerun-if-env-changed=KIRIN_HYPHA_HMAC_KEY");
    match std::env::var("KIRIN_HYPHA_HMAC_KEY") {
        Ok(v) if !v.is_empty() => println!(
            "cargo:warning=Kirin Hypha HMAC key source = env override ({} bytes)",
            v.len()
        ),
        _ => println!(
            "cargo:warning=Kirin Hypha HMAC key source = DEFAULT_HMAC_KEY (Phase 1.0 embedded)"
        ),
    }
}
