use super::*;

#[test]
fn all_invalid_sized_calls_preserve_every_output_byte() {
    let mut bytes = vec![0xa5_u8; std::mem::size_of::<KirinAttackBandSummaryV2>() + 8];
    let alignment = bytes
        .as_ptr()
        .align_offset(std::mem::align_of::<KirinAttackBandSummaryV2>());
    let out = unsafe {
        bytes
            .as_mut_ptr()
            .add(alignment)
            .cast::<KirinAttackBandSummaryV2>()
    };
    let request = KirinAttackBandSummaryV2Request {
        version: 2,
        struct_size: std::mem::size_of::<KirinAttackBandSummaryV2Request>() as u32,
        band: 1,
        ..Default::default()
    };
    let before = bytes.clone();
    for (request_size, out_size) in [(0, 0), (15, 13_528), (16, 1), (16, 13_528)] {
        let status = unsafe {
            kirin_hypha_poll_attack_band_summary_v2(
                std::ptr::null(),
                request_size,
                &request,
                out_size,
                out,
            )
        };
        assert_eq!(status, KIRIN_SNAPSHOT_INVALID_REQUEST);
        assert_eq!(bytes, before);
    }
}

#[test]
fn sized_commit_rejects_short_and_misaligned_without_touching_output() {
    let mut bytes = vec![0xa5_u8; std::mem::size_of::<KirinAttackBandSummaryV2>() + 16];
    let alignment = bytes
        .as_ptr()
        .align_offset(std::mem::align_of::<KirinAttackBandSummaryV2>());
    let out = unsafe {
        bytes
            .as_mut_ptr()
            .add(alignment)
            .cast::<KirinAttackBandSummaryV2>()
    };
    let before = bytes.clone();
    assert_eq!(
        unsafe { commit_sized(out, 1, KirinAttackBandSummaryV2::default()) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(bytes, before);
    let bad = unsafe {
        bytes
            .as_mut_ptr()
            .add(alignment + 1)
            .cast::<KirinAttackBandSummaryV2>()
    };
    assert_eq!(
        unsafe {
            commit_sized(
                bad,
                bytes.len() as u32 - 16,
                KirinAttackBandSummaryV2::default(),
            )
        },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(bytes, before);
}

#[test]
fn source_change_and_binding_change_are_not_normal_cutoff_progress() {
    let source = AttackSourceKey {
        incarnation: [1; 16],
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        odf_hash: [3; 32],
    };
    let evidence = AttackSourceEvidence {
        source,
        odf_support_start: 0,
        odf_support_end: 200_000,
        pcm_start: 0,
        pcm_end: 200_000,
        cutoff: 200_000,
        band_semantic_hash: [1; 32],
        clock_policy: 1,
    };
    let proof = AttackMappingProof {
        authority_revision: 1,
        request_id: [1; 16],
        target_hash: [2; 32],
        pre: evidence,
        post: evidence,
        mapping_epoch: 1,
        support_start: 0,
        support_end: 200_000,
        band_semantic_hash: [1; 32],
    };
    let a = AttackObservationView {
        authority_revision: 1,
        target_hash: [2; 32],
        proof: Some(proof),
        ..Default::default()
    };
    let mut b = a.clone();
    b.proof.as_mut().unwrap().post.cutoff += 256;
    assert!(same_binding(&a, &b));
    b.proof.as_mut().unwrap().post.source.generation += 1;
    assert!(!same_binding(&a, &b));
    let mut b = a.clone();
    b.authority_revision += 1;
    assert!(!same_binding(&a, &b));
    let mut b = a.clone();
    b.proof.as_mut().unwrap().mapping_epoch += 1;
    assert!(!same_binding(&a, &b));
}

#[test]
fn valid_engine_unknown_request_enum_version_and_short_buffers_preserve_output() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let mut storage = vec![0xa5_u64; std::mem::size_of::<KirinAttackBandSummaryV2>() / 8];
    let out = storage.as_mut_ptr().cast::<KirinAttackBandSummaryV2>();
    let before = storage.clone();
    let valid = KirinAttackBandSummaryV2Request {
        version: 2,
        struct_size: 16,
        band: 1,
        ..Default::default()
    };
    for (request, expected) in [
        (
            KirinAttackBandSummaryV2Request {
                version: 99,
                ..valid
            },
            KIRIN_SNAPSHOT_UNSUPPORTED,
        ),
        (
            KirinAttackBandSummaryV2Request {
                target: 99,
                ..valid
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
        (
            KirinAttackBandSummaryV2Request { band: 0, ..valid },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
        (
            KirinAttackBandSummaryV2Request {
                struct_size: 15,
                ..valid
            },
            KIRIN_SNAPSHOT_INVALID_REQUEST,
        ),
    ] {
        assert_eq!(
            unsafe { kirin_hypha_poll_attack_band_summary_v2(&engine, 16, &request, 13528, out) },
            expected
        );
        assert_eq!(storage, before);
    }
    assert_eq!(
        unsafe { kirin_hypha_poll_attack_band_summary_v2(&engine, 16, &valid, 13527, out) },
        KIRIN_SNAPSHOT_INVALID_REQUEST
    );
    assert_eq!(storage, before);
}

#[test]
fn revision_changes_when_only_pre_facts_or_cohort_scope_change() {
    let mut snapshot = KirinAttackBandSummaryV2::default();
    let original = content_revision(&snapshot, 5, 7);
    assert_eq!(original, content_revision(&snapshot, 5, 7));
    assert_ne!(original, content_revision(&snapshot, 5, 8));
    snapshot.header.cutoff_sample = 1;
    assert_ne!(original, content_revision(&snapshot, 5, 7));
}
