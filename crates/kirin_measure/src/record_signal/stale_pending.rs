//! 起動時 dead Pending 掃除。

use chrono::{DateTime, Utc};
use std::fs;
use std::path::Path;

use super::{delete_signal, scan_signals_dir, SignalStatus, ACK_TIMEOUT_SECONDS, SIGNALS_SUBDIR};

/// B-103: 起動時に掃除する dead Pending record_signal のしきい値（秒）。
///
/// 生きている POST は `ACK_TIMEOUT_SECONDS`(30s) で自身の Pending を `mark_released` する
/// （io_thread_post の poll_ack_timeout）。よってそれを大きく超えて Pending のまま残るファイルは
/// 書込 POST の消失（クラッシュ / DAW 終了 / target PRE 不在のまま放置）とみなせる。生記録の
/// 誤掃除を避けるため、自 release(30s) の 4 倍を保守的しきい値とする。
pub const STALE_PENDING_SECS: i64 = ACK_TIMEOUT_SECONDS * 4;

/// B-103: `plugin_data_root` 配下の全 project の `record_signal/` を走査し、status==Pending
/// かつ age(`now` - `t`) > `stale_secs` のファイルだけを削除する（起動時 dead-pending 掃除）。
///
/// 保守条件: **Pending のみ**（Acknowledged / Released は対象外）かつ **`stale_secs` 超過のみ**
/// （fresh Pending = 進行中の Keep は保持）。`t` が parse 不能なものは安全側で保持。戻り = 削除件数。
/// テスト容易性のため `plugin_data_root` / `now` を注入する純粋ロジック版。
pub fn sweep_stale_pending_in(
    plugin_data_root: &Path,
    now: DateTime<Utc>,
    stale_secs: i64,
) -> usize {
    let project_entries = match fs::read_dir(plugin_data_root) {
        Ok(e) => e,
        Err(_) => return 0,
    };
    let mut cleared: usize = 0;
    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let project_hash = match project_dir.file_name().and_then(|n| n.to_str()) {
            Some(s) => s.to_string(),
            None => continue,
        };
        if project_hash.as_str() == SIGNALS_SUBDIR {
            continue;
        }
        for (post_iid, sig) in scan_signals_dir(plugin_data_root, &project_hash) {
            if sig.status != SignalStatus::Pending {
                continue;
            }
            let stale = match DateTime::parse_from_rfc3339(&sig.t) {
                Ok(t) => {
                    now.signed_duration_since(t.with_timezone(&Utc))
                        .num_seconds()
                        > stale_secs
                }
                Err(_) => false, // 安全側: parse 不能は保持
            };
            if !stale {
                continue;
            }
            if delete_signal(plugin_data_root, &project_hash, &post_iid).is_ok() {
                cleared += 1;
                log::info!(
                    "[record_signal] startup: swept stale Pending (project_hash={}, post_iid={})",
                    project_hash,
                    post_iid
                );
            }
        }
    }
    cleared
}

/// B-103: 起動時 dead-pending 掃除の production ラッパー（StoragePaths / now を解決）。
/// PRE / POST 両 io_thread の起動時に呼ぶ（age ベースなので role 非依存・冪等）。
pub fn sweep_stale_pending_at_startup() {
    let Ok(paths) = crate::storage::StoragePaths::default_platform() else {
        return;
    };
    let n = sweep_stale_pending_in(&paths.plugin_data_dir(), Utc::now(), STALE_PENDING_SECS);
    if n > 0 {
        log::info!(
            "[record_signal] startup: swept {} stale Pending signal(s)",
            n
        );
    }
}
