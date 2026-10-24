//! The Keep entry shared by the direct control and the All Keep receiver: pair selection,
//! pairing reservation, the capture generation and the PRE signal.
use super::keep_preparation::KeepFailure;
use super::*;

/// B-102: keep の解決本体（`keep()` と broadcast 受信 closure が共有する単一実装）。
/// POST単独 Record を作らないため、ここでは RecordStateMachine を Record にしない。
/// double-keep guard → select_target_pre → reservation → write_pending の順で arming し、
/// PRE ACK 後に POST IO Thread が Record へ入る。Keep は dropped WAV metadata を読まず、
/// Drop は generation archive の immutable rosterだけを受理して今回WAVを結び付ける。
#[allow(clippy::too_many_arguments)]
pub(crate) fn resolve_and_enter_keep(
    license: License,
    record_sm: &RecordStateMachine,
    pair_target: &RwLock<String>,
    paired_pre_target: &Mutex<Option<String>>,
    project_hash: &str,
    post_iid: &str,
    daw: &str,
    // B-108: ラッチ済みならラッチ先を直接 Arm target に使う（同名2台目でも結合不変）。未ラッチ時のみ
    // select_target_pre_for_arm にフォールバックする（resolve_arm_target 内で分岐）。
    latched: &Mutex<Option<LatchedPre>>,
    // Persistent IO/producer faults only. Direct Keep conflicts and capacity/readiness feedback
    // use the one-shot action channel below (R-28 without stale UI state).
    record_error_message: &Arc<RwLock<Option<String>>>,
    keep_action_notice: &Arc<RwLock<Option<String>>>,
    keep_phase: &Arc<AtomicU8>,
    keep_phase_generation_started_at_ms: &Arc<AtomicI64>,
    keep_record_generation: &Arc<AtomicU64>,
    started_at_position_samples: Option<i64>,
    capture_generation: Option<&CaptureGeneration>,
) -> bool {
    // B-071 double-keep guard: 既に Record 中なら no-op（既存 linkage 温存）。
    if record_sm.is_recording() {
        return false;
    }
    if !matches!(license, License::Os) {
        return false;
    }
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let pair = pair_target.read().map(|g| g.clone()).unwrap_or_default();
    // B-108/B-231: ラッチ済み（pair 名一致）はラッチ先を直接使用、未ラッチは B-104 Arm
    // ゲート（非Bypassed + fresh + 一意 / Active 要求なし）。v1.0.0 の「アーム→再生」を維持する。
    let Some(sel) = resolve_arm_target_for_post_project_in_session(
        &kirin_root,
        &pair,
        project_hash,
        daw,
        latched,
    ) else {
        if pair.is_empty() {
            KeepFailure::ChoosePre.publish(keep_action_notice);
        } else {
            KeepFailure::PreUnavailable.publish(keep_action_notice);
        }
        return false;
    };
    let target = sel.instance_id.clone();
    let base = match StoragePaths::default_platform() {
        Ok(p) => p.plugin_data_dir(),
        Err(error) => {
            KeepFailure::from_storage(&error).report(
                keep_action_notice,
                "storage location",
                &error,
            );
            return false;
        }
    };
    // A single Keep owns the same two-phase generation contract as All Keep. External
    // generations are already staged by the All Keep originator and are committed only after all
    // members report exact PRE+POST writer ownership.
    let owned_generation = capture_generation.is_none().then(|| {
        CaptureGeneration::new_single_named(
            project_hash.to_string(),
            post_iid.to_string(),
            target.clone(),
            daw.to_string(),
            current_host_process_id(),
            Some(pair.clone()),
        )
    });
    // The single Keep holds the generation lock before it reserves the pair. An older Keep aborted
    // before its commit gives its reservation back while it still holds that lock, so it can never
    // remove a reservation this Keep took or reused (INV-R3). An unstaged transaction that drops
    // on a later rejection changes nothing.
    let mut owned_transaction = match owned_generation.as_ref() {
        Some(generation) => match CaptureGenerationTransaction::begin(&base, generation) {
            Ok(transaction) => Some(transaction),
            Err(error) => {
                KeepFailure::from_generation(&error).report(keep_action_notice, "begin", &error);
                return false;
            }
        },
        None => None,
    };
    // B-127 (G-115-365): per-pairing O_EXCL reservation で cross-process atomic に枠を確保する。
    // pairing key = (target=PRE iid, post_iid=POST iid)。cap の真実源は枠ファイルの**物理存在のみ**
    // （count_distinct_pairings = reservation::count_frames）。reservation を先に atomic-create
    // してから枠数を数えることで、active marker 出現前の TOCTOU 窓を cross-process で閉じる。cap は
    // 12 pairs。keep() / keep_all() / broadcast 受信 closure は全て本関数を通る（JUCE 殻 parity）。
    // reserve は 1 回だけ呼ぶ。Created = 本呼び出しが枠を作った（reject 時に解放する責務）。
    let reservation_created =
        match reservation::reserve_pairing(&base, project_hash, &target, post_iid) {
            Ok(reservation::ReserveOutcome::Created) => true,
            Ok(reservation::ReserveOutcome::AlreadyReserved) => false,
            Ok(
                reservation::ReserveOutcome::PreInUse | reservation::ReserveOutcome::AuditionInUse,
            ) => {
                if let Ok(mut g) = keep_action_notice.write() {
                    *g = Some("PRE or Blind Compare already in use".to_string());
                }
                return false;
            }
            // G-115-365 (3): 枠が取れない（write_all 失敗等の Err / 不完全枠は内部で unlink 済）= reject。
            // 枠なしで keep に入らない。
            Err(error) => {
                KeepFailure::from_storage_io(error.kind()).report(
                    keep_action_notice,
                    "pair reservation",
                    &error,
                );
                return false;
            }
        };
    // count は自 reservation を含む枠数。自 reservation で 13 枠目になれば（`> MAX`）reject。既存 pairing
    // （自 reservation が AlreadyReserved）なら枠数は不変なので 12 枠目まで通る（`> MAX` であって `>= MAX`
    // ではない）。
    if count_distinct_pairings(&base, project_hash) > MAX_ACTIVE_PER_PROJECT {
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        // R-28: 13 ペア目を hard reject し silent drop しない（既存文言流用）。
        if let Ok(mut g) = keep_action_notice.write() {
            *g = Some("Maximum 12 pairs reached".to_string());
        }
        return false;
    }
    if let Some(generation) = capture_generation {
        let Some(generation_member) = generation.member(project_hash, post_iid) else {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            KeepFailure::SetupChanged.publish(keep_action_notice);
            return false;
        };
        if !generation_member.pre_instance_id.is_empty()
            && generation_member.pre_instance_id != target
        {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            KeepFailure::SetupChanged.publish(keep_action_notice);
            return false;
        }
    }
    let generation = match capture_generation.or(owned_generation.as_ref()) {
        Some(generation) => generation,
        None => {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            KeepFailure::SetupChanged.publish(keep_action_notice);
            return false;
        }
    };
    if let Some(transaction) = owned_transaction.as_mut() {
        if let Err(error) = transaction.stage() {
            if reservation_created {
                reservation::release_pairing(&base, project_hash, &target, post_iid);
            }
            KeepFailure::from_generation(&error).report(keep_action_notice, "stage", &error);
            return false;
        }
    }
    keep_phase_generation_started_at_ms.store(generation.started_at_ms, Ordering::Release);
    keep_record_generation.store(record_sm.generation().saturating_add(1), Ordering::Release);
    keep_phase.store(KIRIN_KEEP_PHASE_PREPARING, Ordering::Release);
    // target_pre_instance_id = 選定 PRE。PRE が自宛て signal を発見し ack する。
    let write_result = write_pending_claiming_expected_and_clock_for_generation(
        &base,
        project_hash,
        post_iid,
        target.clone(),
        daw.to_string(),
        started_at_position_samples,
        generation,
    );
    if write_result.is_ok() {
        // B-127: 正常 enter で stale な cap/io-fail 通知を消す（新しい健全な Record が開始した）。
        if let Ok(mut g) = record_error_message.write() {
            *g = None;
        }
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = Some(target.clone());
        }
        // B-140: 無音/停止中に Keep した場合、Record 開始時点では Watch 表示側のラッチが未成立な
        // ことがある。Record 中の Δ 表示は「ラッチ凍結」なので再探索しないため、keep が実際に
        // 選んだ PRE をここで display/Arm 共有ラッチへ確定させる。これにより無音でアーム→再生後も
        // 同じ PRE から Δ が出る（同名 PRE の後発出現では再選定しない）。
        if let Ok(mut g) = latched.lock() {
            *g = Some(LatchedPre {
                name: pair.clone(),
                instance_id: target.clone(),
                project_dir: sel.project_dir,
                pre_json: sel.pre_json,
                daw_session_id: sel.daw_session_id,
                host_process_id: sel.host_process_id,
                readiness: kirin_measure::LatchedPreReadiness::Confirmed,
            });
        }
        if let Some(transaction) = owned_transaction.take() {
            if !super::keep_preparation::start_keep_preparation(
                transaction,
                keep_action_notice,
                keep_phase,
                keep_phase_generation_started_at_ms,
                generation.started_at_ms,
                false,
            ) {
                return false;
            }
        } else if let Err(error) = spawn_keep_phase_observer(
            base.clone(),
            generation.capture_generation_id.clone(),
            generation.started_at_ms,
            Arc::clone(keep_phase),
            Arc::clone(keep_phase_generation_started_at_ms),
        ) {
            keep_phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
            KeepFailure::WorkerUnavailable.report(keep_action_notice, "observer dispatch", &error);
            return false;
        }
        true
    } else {
        if let Err(error) = write_result {
            KeepFailure::from_signal(&error).report(keep_action_notice, "PRE request", &error);
        }
        // write_pending 失敗時も予約枠を戻す（自分が作った場合のみ）。
        if reservation_created {
            reservation::release_pairing(&base, project_hash, &target, post_iid);
        }
        if let Ok(mut g) = paired_pre_target.lock() {
            *g = None;
        }
        keep_phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
        keep_record_generation.store(0, Ordering::Release);
        false
    }
}

fn spawn_keep_phase_observer(
    base: std::path::PathBuf,
    capture_generation_id: String,
    generation_started_at_ms: i64,
    phase: Arc<AtomicU8>,
    phase_generation: Arc<AtomicI64>,
) -> std::io::Result<()> {
    std::thread::Builder::new()
        .name("hypha-arm-observer".to_string())
        .spawn(move || {
            let deadline =
                Instant::now() + kirin_measure::capture_contract::CAPTURE_PRODUCER_READY_LIMIT;
            loop {
                if phase_generation.load(Ordering::Acquire) != generation_started_at_ms {
                    return;
                }
                if kirin_measure::read_active_generation(&base)
                    .ok()
                    .flatten()
                    .is_some_and(|active| {
                        active.capture_generation_id == capture_generation_id
                            && active.started_at_ms == generation_started_at_ms
                    })
                {
                    phase.store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release);
                    return;
                }
                let terminal = kirin_measure::read_generation_terminal(
                    &base,
                    &capture_generation_id,
                    generation_started_at_ms,
                );
                if terminal.ok().flatten().is_some() || Instant::now() >= deadline {
                    phase.store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
                    return;
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        })
        .map(|_| ())
}

#[cfg(test)]
mod tests {
    /// A single Keep holds its generation lock before it reserves the pair. An older aborted Keep
    /// gives its reservation back while holding that lock, so it can never remove the reservation
    /// of the Keep that follows it (`a_reservation_taken_after_begin_survives_an_older_aborted_keep`).
    #[test]
    fn a_single_keep_reserves_its_pair_only_after_it_holds_the_generation_lock() {
        let source = include_str!("keep_entry.rs");
        let body = &source[source.find("fn resolve_and_enter_keep(").unwrap()..];
        let begin = body.find("CaptureGenerationTransaction::begin(").unwrap();
        let reserve = body.find("reservation::reserve_pairing(").unwrap();
        assert!(
            begin < reserve,
            "the pair is reserved before the generation lock"
        );
    }
}
