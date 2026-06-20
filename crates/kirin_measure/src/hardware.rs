//! Hardware identification — 3 要素取得と hardware_id 計算。
//!
//!
//! # 3 要素
//! | 要素 | 取得コマンド |
//! |------|------------|
//! | `IOPlatformUUID`       | `ioreg -rd1 -c IOPlatformExpertDevice` |
//! | `system_serial_number` | `system_profiler SPHardwareDataType` |
//! | `boot_disk_uuid`       | `diskutil info /` |
//!
//! # 2-of-3 判定
//! 保存済み 3 要素と現在 3 要素を比較し、**2 つ以上一致** なら同一マシンとみなす。
//!
//! # 取得失敗時のフォールバック
//! いずれかの要素が取得できない場合、その要素を空文字列として扱い、
//!
//! # T-1 との分担
//! 本モジュールは `HardwareComponents` 構造体 + macOS コマンド呼び出し。
//! `hardware_id` ハッシュ計算は `identity::hash_hardware` に委譲。

use serde::{Deserialize, Serialize};
use std::process::Command;

/// 3 要素を個別保持する構造体（serde で identity.json に埋め込まれる）。
///
/// 取得失敗した要素は空文字列。2-of-3 判定時は空文字列同士の一致を「不一致」扱いにして、
/// 「何もないマシン × 何もないマシン」を同一とみなさないようにする。
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct HardwareComponents {
    /// `ioreg` 由来。例: `"AB12CD34-5678-90EF-1234-567890ABCDEF"`
    pub iop: String,
    /// `system_profiler` 由来。例: `"C02ABCDEFGHI"`
    pub sn: String,
    /// `diskutil` 由来。例: `"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"`
    pub bd: String,
}

impl HardwareComponents {
    /// SHA-256(iop | sn | bd) を hex で返す。
    pub fn hardware_id(&self) -> String {
        crate::identity::hash_hardware(&self.iop, &self.sn, &self.bd)
    }

    /// 2-of-3 判定: 保存値 vs 現在値。
    ///
    /// 空文字列の要素は比較対象から除外（unknown）。
    /// 空でない要素同士を比較して、一致数を数える。
    ///
    /// # 戻り値
    /// - `Match::Same` : 2 つ以上の非空要素が一致
    /// - `Match::Different` : 比較可能な要素がすべて不一致
    /// - `Match::Insufficient` : 比較可能な要素が 1 つ以下（判定不能）
    pub fn compare(&self, other: &HardwareComponents) -> Match {
        let mut comparable = 0;
        let mut matched = 0;

        for (a, b) in [
            (&self.iop, &other.iop),
            (&self.sn, &other.sn),
            (&self.bd, &other.bd),
        ] {
            if !a.is_empty() && !b.is_empty() {
                comparable += 1;
                if a == b {
                    matched += 1;
                }
            }
        }

        if comparable < 2 {
            Match::Insufficient
        } else if matched >= 2 {
            Match::Same
        } else if matched == 0 {
            Match::Different
        } else {
            // 1 つだけ一致（3 要素中 2 つ比較可能で 1 つ一致、または 3 つ比較可能で 1 つ一致）
            // → 2-of-3 を満たさないので Different 扱い
            Match::Different
        }
    }

    /// 現在マシンから 3 要素を取得する。取得失敗した要素は空文字列。
    ///
    /// すべて失敗しても panic せずに `default()` を返す。
    /// 呼び出し元は `iop`/`sn`/`bd` の空チェックで判定を細分化できる。
    pub fn current() -> Self {
        Self {
            iop: fetch_io_platform_uuid().unwrap_or_default(),
            sn: fetch_serial_number().unwrap_or_default(),
            bd: fetch_boot_disk_uuid().unwrap_or_default(),
        }
    }
}

/// 2-of-3 判定結果。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Match {
    /// 2 要素以上一致 → 同一マシン。
    Same,
    /// 比較可能な全要素が不一致、または一致 1 件のみ → 別マシン。
    Different,
    /// 比較可能な要素が 1 件以下（両方取得失敗等） → 判定不能。permissive に倒す。
    Insufficient,
}

// ── macOS コマンド呼び出し ───────────────────────────────────────────────

/// `ioreg -rd1 -c IOPlatformExpertDevice` から `IOPlatformUUID` を抽出。
///
/// 出力例:
/// ```text
///     "IOPlatformUUID" = "AB12CD34-5678-90EF-1234-567890ABCDEF"
/// ```
fn fetch_io_platform_uuid() -> Option<String> {
    let out = Command::new("ioreg")
        .args(["-rd1", "-c", "IOPlatformExpertDevice"])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let text = String::from_utf8_lossy(&out.stdout);
    extract_quoted_value(&text, "IOPlatformUUID")
}

/// `system_profiler SPHardwareDataType` から `Serial Number (system)` を抽出。
///
/// 出力例:
/// ```text
///       Serial Number (system): C02ABCDEFGHI
/// ```
fn fetch_serial_number() -> Option<String> {
    let out = Command::new("system_profiler")
        .arg("SPHardwareDataType")
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let text = String::from_utf8_lossy(&out.stdout);
    for line in text.lines() {
        let t = line.trim();
        // "Serial Number (system): XXXX" または "Serial Number: XXXX"
        if let Some(rest) = t.strip_prefix("Serial Number (system):") {
            let v = rest.trim();
            if !v.is_empty() && v != "Not Available" {
                return Some(v.to_string());
            }
        } else if let Some(rest) = t.strip_prefix("Serial Number:") {
            let v = rest.trim();
            if !v.is_empty() && v != "Not Available" {
                return Some(v.to_string());
            }
        }
    }
    None
}

/// `diskutil info /` から `Volume UUID` を抽出。
///
/// 出力例:
/// ```text
///    Volume UUID:               AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE
/// ```
///
/// どちらか先に見つかった方を採用する。
fn fetch_boot_disk_uuid() -> Option<String> {
    let out = Command::new("diskutil").args(["info", "/"]).output().ok()?;
    if !out.status.success() {
        return None;
    }
    let text = String::from_utf8_lossy(&out.stdout);
    for line in text.lines() {
        let t = line.trim();
        if let Some(rest) = t.strip_prefix("Volume UUID:") {
            let v = rest.trim();
            if !v.is_empty() {
                return Some(v.to_string());
            }
        }
    }
    // Volume UUID が取れない場合のフォールバック
    for line in text.lines() {
        let t = line.trim();
        if let Some(rest) = t.strip_prefix("Disk / Partition UUID:") {
            let v = rest.trim();
            if !v.is_empty() {
                return Some(v.to_string());
            }
        }
    }
    None
}

/// `"KEY" = "VALUE"` 形式の行から VALUE を抽出。
///
/// ioreg 出力から特定キーの値を取り出すためのヘルパー。
fn extract_quoted_value(text: &str, key: &str) -> Option<String> {
    let needle = format!("\"{}\"", key);
    for line in text.lines() {
        if let Some(pos) = line.find(&needle) {
            let after = &line[pos + needle.len()..];
            let after = after.trim_start();
            let after = after.strip_prefix('=')?.trim_start();
            let after = after.strip_prefix('"')?;
            let end = after.find('"')?;
            return Some(after[..end].to_string());
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extract_quoted_value_basic() {
        let text = r#"
    "IOPlatformUUID" = "ABC-123"
    "Other" = "zzz"
"#;
        assert_eq!(
            extract_quoted_value(text, "IOPlatformUUID"),
            Some("ABC-123".to_string())
        );
    }

    #[test]
    fn extract_quoted_value_missing_returns_none() {
        let text = r#""Foo" = "bar""#;
        assert!(extract_quoted_value(text, "IOPlatformUUID").is_none());
    }

    #[test]
    fn compare_identical_is_same() {
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "C".to_string(),
        };
        assert_eq!(a.compare(&a.clone()), Match::Same);
    }

    #[test]
    fn compare_two_matches_is_same() {
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "C".to_string(),
        };
        let b = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "DIFFERENT".to_string(),
        };
        assert_eq!(a.compare(&b), Match::Same);
    }

    #[test]
    fn compare_one_match_is_different() {
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "C".to_string(),
        };
        let b = HardwareComponents {
            iop: "A".to_string(),
            sn: "X".to_string(),
            bd: "Y".to_string(),
        };
        assert_eq!(a.compare(&b), Match::Different);
    }

    #[test]
    fn compare_no_match_is_different() {
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "C".to_string(),
        };
        let b = HardwareComponents {
            iop: "X".to_string(),
            sn: "Y".to_string(),
            bd: "Z".to_string(),
        };
        assert_eq!(a.compare(&b), Match::Different);
    }

    #[test]
    fn compare_empty_elements_excluded() {
        // 3 要素中 2 つが空 → 比較可能 1 のみ → Insufficient
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "".to_string(),
            bd: "".to_string(),
        };
        let b = HardwareComponents {
            iop: "A".to_string(),
            sn: "".to_string(),
            bd: "".to_string(),
        };
        assert_eq!(a.compare(&b), Match::Insufficient);
    }

    #[test]
    fn compare_all_empty_is_insufficient() {
        let a = HardwareComponents::default();
        let b = HardwareComponents::default();
        assert_eq!(a.compare(&b), Match::Insufficient);
    }

    #[test]
    fn compare_two_nonempty_both_match_is_same() {
        // 3 要素中 1 つ空、2 つ一致 → Same
        let a = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "".to_string(),
        };
        let b = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "".to_string(),
        };
        assert_eq!(a.compare(&b), Match::Same);
    }

    #[test]
    fn hardware_id_is_deterministic_and_hex() {
        let hc = HardwareComponents {
            iop: "A".to_string(),
            sn: "B".to_string(),
            bd: "C".to_string(),
        };
        let h1 = hc.hardware_id();
        let h2 = hc.hardware_id();
        assert_eq!(h1, h2);
        assert_eq!(h1.len(), 64);
        assert!(h1.chars().all(|c| c.is_ascii_hexdigit()));
    }

    // ── 実機コマンド呼び出しテスト（macOS 依存） ────────────────────
    // 本テストは実機で動作確認用。CI 等 Linux で走らせると失敗するので
    // `#[cfg(target_os = "macos")]` でゲート。

    #[cfg(target_os = "macos")]
    #[test]
    fn current_returns_at_least_one_component_on_macos() {
        let hc = HardwareComponents::current();
        // 少なくとも 1 つは取得できるはず（全滅なら macOS コマンド環境に問題あり）
        let non_empty = [&hc.iop, &hc.sn, &hc.bd]
            .iter()
            .filter(|s| !s.is_empty())
            .count();
        assert!(
            non_empty >= 1,
            "expected at least 1 component fetched, got iop={:?} sn={:?} bd={:?}",
            hc.iop,
            hc.sn,
            hc.bd
        );
    }
}
