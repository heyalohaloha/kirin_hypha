use super::*;

#[test]
fn common_status_unknown_enum_alignment_and_sizes_preserve_every_output_word() {
    assert_eq!(
        (KIRIN_TARGET_POST, KIRIN_TARGET_DELTA, KIRIN_TARGET_PRE),
        (0, 1, 2)
    );
    assert_eq!(
        (
            KIRIN_SNAPSHOT_SUCCESS,
            KIRIN_SNAPSHOT_BUSY,
            KIRIN_SNAPSHOT_INVALID_REQUEST,
            KIRIN_SNAPSHOT_UNSUPPORTED,
            KIRIN_SNAPSHOT_RETIRED
        ),
        (0, 1, 2, 3, 4)
    );
    let fixture = engine();
    const PACKET_WORDS: usize = std::mem::size_of::<KirinTimeSnapshotV2>() / 8;
    const ROW_WORDS: usize = std::mem::size_of::<KirinTimeHistoryEntryV2>() / 8;
    let mut packet = [0xA5A5_A5A5_A5A5_A5A5u64; PACKET_WORDS];
    let mut main = [0xA5A5_A5A5_A5A5_A5A5u64; ROW_WORDS * 2];
    let mut psr = main;
    let packet_before = packet;
    let rows_before = main;
    for variant in 0..10 {
        let mut req = request(u32::from(KIRIN_TARGET_PRE));
        let expected = match variant {
            0 => {
                req.version = 3;
                KIRIN_SNAPSHOT_UNSUPPORTED
            }
            1 => {
                req.struct_size = 8;
                KIRIN_SNAPSHOT_INVALID_REQUEST
            }
            2 => {
                req.main_target = 3;
                KIRIN_SNAPSHOT_INVALID_REQUEST
            }
            3 => {
                req.resolution = 3;
                KIRIN_SNAPSHOT_INVALID_REQUEST
            }
            4 => {
                req.main_target = u32::from(KIRIN_TARGET_POST);
                KIRIN_SNAPSHOT_UNSUPPORTED
            }
            5 => {
                req.packet_size -= 1;
                KIRIN_SNAPSHOT_INVALID_REQUEST
            }
            6 => {
                req.entry_size += 1;
                KIRIN_SNAPSHOT_INVALID_REQUEST
            }
            7 | 8 => KIRIN_SNAPSHOT_INVALID_REQUEST,
            _ => KIRIN_SNAPSHOT_BUSY,
        };
        let request_ptr = if variant == 7 {
            (&req as *const KirinTimeSnapshotRequestV2)
                .cast::<u8>()
                .wrapping_add(1)
                .cast()
        } else {
            &req
        };
        let out_ptr = if variant == 8 {
            packet.as_mut_ptr().cast::<u8>().wrapping_add(1).cast()
        } else {
            packet.as_mut_ptr().cast()
        };
        let _held = (variant == 9).then(|| fixture.meter_session.as_ref().unwrap().lock().unwrap());
        let status = unsafe {
            kirin_hypha_poll_time_snapshot_v2(
                &fixture,
                request_ptr,
                main.as_mut_ptr().cast(),
                2,
                psr.as_mut_ptr().cast(),
                2,
                out_ptr,
            )
        };
        assert_eq!(status, expected, "variant {variant}");
        assert_eq!(packet, packet_before);
        assert_eq!(main, rows_before);
        assert_eq!(psr, rows_before);
    }
}

#[test]
fn same_cutoff_revision_changes_with_proof_reason_current_and_history_but_not_poll_age() {
    let start = Instant::now();
    let (point, authority, view) = joined(start);
    let current = |v: &TimeComparisonView, now| {
        compared(
            Some(v),
            &authority,
            point.wire.span,
            Some(&point),
            true,
            now,
            0,
        )
    };
    let context = (point.wire.span, 48000, 0, 7, 1, true);
    let main = absolute(
        Some(&point),
        KIRIN_TARGET_POST,
        true,
        start,
        point.wire.span,
        0,
    );
    let revision =
        |m: &KirinTimeComponentV2, p: &KirinTimeComponentV2, rows: &[MeterHistoryEntry]| {
            revision::content_revision(m, p, rows, &[], context)
        };
    let first = current(&view, start);
    let original = revision(&main, &first, &[]);
    assert_eq!(
        original,
        revision(
            &main,
            &current(&view, start + Duration::from_millis(299)),
            &[]
        )
    );
    for variant in 0..5 {
        let mut changed = first;
        match variant {
            0 => changed.reason = TimeComparisonReason::Missing as u8,
            1 => changed.owner_identity += 1,
            2 => changed.claim_identity += 1,
            3 => changed.pre_span.token += 1,
            _ => changed.current.values[3] = 9.0,
        }
        assert_ne!(original, revision(&main, &changed, &[]));
    }
    let mut history = kirin_measure::meter_history::MeterHistory::new();
    history.push(
        1,
        3,
        8,
        48000,
        (Some(48000), CaptureClockSource::ProjectTimeline),
        &kirin_measure::MeasureResult {
            psr: Some(3.0),
            ..Default::default()
        },
        kirin_measure::MeterHistoryAux::default(),
    );
    let rows = history.recent(MeterHistoryResolution::Hz10, 1);
    assert_ne!(original, revision(&main, &first, &rows));
    let mut changed_rows = rows.clone();
    changed_rows[0].psr.mean = Some(9.0);
    assert_ne!(
        revision(&main, &first, &rows),
        revision(&main, &first, &changed_rows)
    );
}
