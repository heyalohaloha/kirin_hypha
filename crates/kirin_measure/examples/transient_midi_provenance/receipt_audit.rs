use std::collections::{BTreeMap, BTreeSet};

use super::*;

pub(super) fn validate_parent(parent: &ParentEvidence) -> Result<(), String> {
    if parent.receipt_sha256 != PINNED_DEVELOPMENT_RECEIPT_SHA256
        || parent.manifest_sha256 != PINNED_MANIFEST_SHA256
        || parent.folds_sha256 != PINNED_FOLDS_SHA256
        || parent.archive_sha256 != PINNED_MIDI_ARCHIVE_SHA256
    {
        return Err("MIDI receipt parent chain differs from source-pinned B-550".to_string());
    }
    Ok(())
}

pub(super) fn validate_archive(
    archive: &ArchiveReceiptInput,
    parent: &ParentEvidence,
) -> Result<(), String> {
    if archive.archive_sha256 != parent.archive_sha256
        || archive.archive_bytes != PINNED_MIDI_ARCHIVE_BYTES
        || archive.central_directory_entries != PINNED_ARCHIVE_ENTRIES
        || archive.member_name_prefix != "e-gmd-v1.0.0/"
    {
        return Err("MIDI archive evidence differs from the pinned contract".to_string());
    }
    Ok(())
}

pub(super) fn validate_members(members: &[&MemberEvidence], prefix: &str) -> Result<(), String> {
    if members.len() != PINNED_MANIFEST_ROWS {
        return Err(format!(
            "MIDI receipt requires exactly {PINNED_MANIFEST_ROWS} members"
        ));
    }
    let (mut ids, mut keys, mut manifest_names, mut member_names) = (
        BTreeSet::new(),
        BTreeSet::new(),
        BTreeSet::new(),
        BTreeSet::new(),
    );
    let mut fold_counts = [0_usize; 5];
    let mut split_counts = BTreeMap::new();
    for (index, member) in members.iter().enumerate() {
        validate_member(member, index + 1, prefix)?;
        if !ids.insert(&member.performance_id)
            || !keys.insert(&member.selection_key)
            || !manifest_names.insert(&member.manifest_midi_filename)
            || !member_names.insert(&member.archive_member_name)
        {
            return Err("MIDI receipt member identity is not one-to-one".to_string());
        }
        fold_counts[usize::from(member.fold)] += 1;
        *split_counts.entry(member.split).or_insert(0_usize) += 1;
    }
    if fold_counts != [58; 5]
        || split_counts.get(&DevelopmentSplit::Train) != Some(&PINNED_TRAIN_ROWS)
        || split_counts.get(&DevelopmentSplit::Validation) != Some(&PINNED_VALIDATION_ROWS)
    {
        return Err("MIDI receipt fold or train/validation coverage mismatch".to_string());
    }
    Ok(())
}

fn validate_member(member: &MemberEvidence, rank: usize, prefix: &str) -> Result<(), String> {
    if member.selection_rank != rank
        || member.performance_id.is_empty()
        || member.fold >= 5
        || !safe_relative_name(&member.manifest_midi_filename)
        || member.archive_member_name != format!("{prefix}{}", member.manifest_midi_filename)
        || member.uncompressed_bytes == 0
        || member.compressed_bytes == 0
        || member.excerpt.start_sample_44100 >= member.excerpt.end_sample_44100
        || !member
            .excerpt
            .start_sample_44100
            .is_multiple_of(crate::drum_excerpt::EXCERPT_START_QUANTUM_SAMPLES)
        || member.excerpt.manifest_counts != member.excerpt.observed.counts
    {
        return Err(format!(
            "invalid MIDI member evidence at selection rank {rank}"
        ));
    }
    for (name, value) in [
        ("selection key", member.selection_key.as_str()),
        ("manifest MIDI", member.manifest_midi_sha256.as_str()),
        ("archive member", member.member_sha256.as_str()),
        ("source notes", member.source.notes_sha256.as_str()),
        ("source events", member.source.events_sha256.as_str()),
        (
            "excerpt notes",
            member.excerpt.observed.notes_sha256.as_str(),
        ),
        (
            "excerpt events",
            member.excerpt.observed.events_sha256.as_str(),
        ),
    ] {
        require_sha256(value, name)?;
    }
    if member.manifest_midi_sha256 != member.member_sha256 {
        return Err(format!(
            "manifest/member SHA-256 mismatch at selection rank {rank}"
        ));
    }
    validate_counts(member.source.counts, "source")?;
    validate_counts(member.excerpt.observed.counts, "excerpt")
}

fn validate_counts(counts: EventCounts, label: &str) -> Result<(), String> {
    if counts.compound_events > counts.raw_notes
        || counts.kick_only_events > counts.compound_events
        || counts.hat_only_events > counts.compound_events
    {
        Err(format!("invalid {label} MIDI event counts"))
    } else {
        Ok(())
    }
}

pub(super) fn aggregate(
    members: &[&MemberEvidence],
    duplicate_groups: DuplicateCounts,
) -> Result<Aggregate, String> {
    let mut value = Aggregate {
        members: members.len(),
        train_members: 0,
        validation_members: 0,
        folds: [0; 5],
        member_uncompressed_bytes: 0,
        member_compressed_bytes: 0,
        source: EventCounts::default(),
        excerpt: EventCounts::default(),
        empty_excerpts: 0,
        duplicate_groups,
    };
    for member in members {
        match member.split {
            DevelopmentSplit::Train => value.train_members += 1,
            DevelopmentSplit::Validation => value.validation_members += 1,
        }
        value.folds[usize::from(member.fold)] += 1;
        value.member_uncompressed_bytes = value
            .member_uncompressed_bytes
            .checked_add(member.uncompressed_bytes)
            .ok_or("selected MIDI member byte total overflow")?;
        value.member_compressed_bytes = value
            .member_compressed_bytes
            .checked_add(member.compressed_bytes)
            .ok_or("selected MIDI compressed byte total overflow")?;
        add_counts(&mut value.source, member.source.counts)?;
        add_counts(&mut value.excerpt, member.excerpt.observed.counts)?;
        value.empty_excerpts += usize::from(member.excerpt.observed.counts.raw_notes == 0);
    }
    Ok(value)
}

fn add_counts(total: &mut EventCounts, next: EventCounts) -> Result<(), String> {
    total.raw_notes = total
        .raw_notes
        .checked_add(next.raw_notes)
        .ok_or("raw note total overflow")?;
    total.compound_events = total
        .compound_events
        .checked_add(next.compound_events)
        .ok_or("compound total overflow")?;
    total.kick_only_events = total
        .kick_only_events
        .checked_add(next.kick_only_events)
        .ok_or("kick total overflow")?;
    total.hat_only_events = total
        .hat_only_events
        .checked_add(next.hat_only_events)
        .ok_or("hat total overflow")?;
    Ok(())
}

pub(super) fn validate_aggregate(
    aggregate: &Aggregate,
    archive: &ArchiveReceiptInput,
    parent: &ParentEvidence,
) -> Result<(), String> {
    let expected_source = EventCounts {
        raw_notes: parent.source_raw_notes,
        compound_events: parent.source_compound_events,
        kick_only_events: parent.source_kick_only_events,
        hat_only_events: parent.source_hat_only_events,
    };
    if aggregate.source != expected_source
        || aggregate.member_uncompressed_bytes != archive.selected_uncompressed_bytes
        || aggregate.member_compressed_bytes != archive.selected_compressed_bytes
    {
        return Err("MIDI receipt aggregate does not bind to parent/archive evidence".to_string());
    }
    Ok(())
}

pub(super) fn duplicate_audit(members: &[&MemberEvidence]) -> DuplicateAudit {
    let raw_members = duplicate_class(groups(members, |_| true, |row| row.member_sha256.clone()));
    let source_canonical_composite = duplicate_class(groups(
        members,
        |_| true,
        |row| {
            composite_sha256(
                "attack-drum-source-canonical-pair-v1",
                &row.source.notes_sha256,
                &row.source.events_sha256,
                None,
            )
        },
    ));
    let nonempty_excerpt_canonical_composite = duplicate_class(groups(
        members,
        |row| row.excerpt.observed.counts.raw_notes > 0,
        |row| {
            composite_sha256(
                "attack-drum-excerpt-canonical-pair-v1",
                &row.excerpt.observed.notes_sha256,
                &row.excerpt.observed.events_sha256,
                Some(row.excerpt.end_sample_44100 - row.excerpt.start_sample_44100),
            )
        },
    ));
    let cross_split_duplicate_groups = [
        &raw_members,
        &source_canonical_composite,
        &nonempty_excerpt_canonical_composite,
    ]
    .into_iter()
    .flat_map(|class| &class.groups)
    .filter(|group| group.splits.len() > 1)
    .count();
    let empty_excerpt_performance_ids = members
        .iter()
        .filter(|row| row.excerpt.observed.counts.raw_notes == 0)
        .map(|row| row.performance_id.clone())
        .collect();
    DuplicateAudit {
        policy: "raw-member, source note+event composite, and nonempty cropped note+event+length composite duplicates fail; event or note digest alone is not a rejection key; empty cropped sequences are reported and exempt",
        raw_members,
        source_canonical_composite,
        nonempty_excerpt_canonical_composite,
        cross_split_duplicate_groups,
        empty_excerpt_performance_ids,
    }
}

fn groups(
    members: &[&MemberEvidence],
    include: impl Fn(&MemberEvidence) -> bool,
    digest: impl Fn(&MemberEvidence) -> String,
) -> Vec<DuplicateGroup> {
    let mut by_hash = BTreeMap::<String, Vec<String>>::new();
    for member in members.iter().filter(|member| include(member)) {
        by_hash
            .entry(digest(member))
            .or_default()
            .push(member.performance_id.clone());
    }
    by_hash
        .into_iter()
        .filter_map(|(evidence_sha256, mut performance_ids)| {
            if performance_ids.len() < 2 {
                return None;
            }
            performance_ids.sort();
            let id_set = performance_ids.iter().collect::<BTreeSet<_>>();
            let splits = members
                .iter()
                .filter(|member| id_set.contains(&member.performance_id))
                .map(|member| member.split)
                .collect::<BTreeSet<_>>()
                .into_iter()
                .collect();
            Some(DuplicateGroup {
                evidence_sha256,
                performance_ids,
                splits,
            })
        })
        .collect()
}

fn duplicate_class(groups: Vec<DuplicateGroup>) -> DuplicateClass {
    DuplicateClass {
        status: if groups.is_empty() { "pass" } else { "fail" },
        duplicate_group_count: groups.len(),
        groups,
    }
}

pub(super) fn duplicate_counts(audit: &DuplicateAudit) -> DuplicateCounts {
    DuplicateCounts {
        raw_members: audit.raw_members.duplicate_group_count,
        source_canonical_composite: audit.source_canonical_composite.duplicate_group_count,
        nonempty_excerpt_canonical_composite: audit
            .nonempty_excerpt_canonical_composite
            .duplicate_group_count,
        cross_split: audit.cross_split_duplicate_groups,
    }
}

fn composite_sha256(domain: &str, left: &str, right: &str, length: Option<u64>) -> String {
    let mut bytes = Vec::with_capacity(domain.len() + left.len() + right.len() + 33);
    for value in [domain, left, right] {
        bytes.extend_from_slice(&(value.len() as u64).to_be_bytes());
        bytes.extend_from_slice(value.as_bytes());
    }
    bytes.push(u8::from(length.is_some()));
    if let Some(length) = length {
        bytes.extend_from_slice(&length.to_be_bytes());
    }
    sha256_bytes(&bytes)
}

fn require_sha256(value: &str, label: &str) -> Result<(), String> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(format!("{label} must be 64 lowercase hexadecimal digits"))
    }
}

fn safe_relative_name(value: &str) -> bool {
    !value.is_empty()
        && !value.starts_with('/')
        && !value.contains('\\')
        && !value.contains('\0')
        && !value.contains(':')
        && value
            .split('/')
            .all(|part| !part.is_empty() && part != "." && part != "..")
}
