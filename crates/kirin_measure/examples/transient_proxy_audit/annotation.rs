use std::fs;
use std::path::Path;

use super::csv::{encode, format_micros, parse_line, parse_seconds_micros};
use super::io::sha256_bytes;
use super::plan::AuditPlan;

pub(crate) const ANNOTATION_SCHEMA: &str = "kirin-hypha-attack-acoustic-annotation-v1";
const COMPOUND_SPAN_MICROS: i64 = 30_000;
const HEADER: &str = "schema,plan_sha256,annotator_id,item_id,audio_relative_path,segment_start_seconds,segment_duration_seconds,row_type,onset_seconds";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum Completion {
    Pending,
    Complete,
}

#[derive(Debug)]
pub(crate) struct AnnotationArtifact {
    pub(crate) annotator_id: String,
    pub(crate) raw_sha256: String,
    pub(crate) completion: Completion,
    pub(crate) events_by_item: Vec<Vec<i64>>,
}

pub(crate) fn render_template(plan: &AuditPlan, plan_sha256: &str, annotator: &str) -> String {
    let mut output = String::from(HEADER);
    output.push('\n');
    for item in &plan.items {
        let fields = [
            ANNOTATION_SCHEMA.to_string(),
            plan_sha256.to_string(),
            annotator.to_string(),
            item.item_id.clone(),
            item.audio_relative_path.clone(),
            format_micros(item.segment_start_micros as i64),
            format_micros(item.segment_duration_micros as i64),
            "pending".to_string(),
            String::new(),
        ];
        output.push_str(
            &fields
                .iter()
                .map(|field| encode(field))
                .collect::<Vec<_>>()
                .join(","),
        );
        output.push('\n');
    }
    output
}

pub(crate) fn read_annotation(
    path: &Path,
    plan: &AuditPlan,
    plan_sha256: &str,
    expected_annotator: &str,
) -> Result<AnnotationArtifact, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read annotation {}: {error}", path.display()))?;
    validate_annotation_bytes(&bytes, plan, plan_sha256, expected_annotator)
}

fn validate_annotation_bytes(
    bytes: &[u8],
    plan: &AuditPlan,
    plan_sha256: &str,
    expected_annotator: &str,
) -> Result<AnnotationArtifact, String> {
    if !matches!(expected_annotator, "A" | "B") {
        return Err("annotator_id must be A or B".to_string());
    }
    let text = std::str::from_utf8(bytes).map_err(|error| format!("annotation UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(HEADER) {
        return Err("unexpected annotation CSV header".to_string());
    }
    let mut rows = plan
        .items
        .iter()
        .map(|_| ItemRows::default())
        .collect::<Vec<_>>();
    let mut last_item_index = 0_usize;
    let mut saw_row = false;
    for (line_index, raw_line) in lines.enumerate() {
        let row_number = line_index + 2;
        let line = raw_line.trim_end_matches('\r');
        if line.is_empty() {
            return Err(format!("blank annotation row {row_number}"));
        }
        let fields = parse_line(line).map_err(|error| format!("row {row_number}: {error}"))?;
        if fields.len() != 9 {
            return Err(format!("annotation row {row_number} must have 9 fields"));
        }
        if fields[0] != ANNOTATION_SCHEMA
            || fields[1] != plan_sha256
            || fields[2] != expected_annotator
        {
            return Err(format!("annotation identity mismatch at row {row_number}"));
        }
        let item_index = plan
            .items
            .iter()
            .position(|item| item.item_id == fields[3])
            .ok_or_else(|| format!("unknown item_id at row {row_number}: {}", fields[3]))?;
        if saw_row && item_index < last_item_index {
            return Err(format!(
                "annotation items are out of plan order at row {row_number}"
            ));
        }
        saw_row = true;
        last_item_index = item_index;
        let item = &plan.items[item_index];
        if fields[4] != item.audio_relative_path
            || fields[5] != format_micros(item.segment_start_micros as i64)
            || fields[6] != format_micros(item.segment_duration_micros as i64)
        {
            return Err(format!(
                "annotation segment metadata mismatch at row {row_number}"
            ));
        }
        let state = &mut rows[item_index];
        match fields[7].as_str() {
            "event" => {
                if state.status.is_some() {
                    return Err(format!("event follows item status at row {row_number}"));
                }
                let onset = parse_seconds_micros(&fields[8])
                    .map_err(|error| format!("invalid onset at row {row_number}: {error}"))?;
                let maximum = i64::try_from(item.segment_duration_micros)
                    .map_err(|_| "segment duration exceeds i64".to_string())?;
                if onset < 0 || onset >= maximum {
                    return Err(format!(
                        "onset is outside half-open segment range at row {row_number}"
                    ));
                }
                if state
                    .events
                    .last()
                    .is_some_and(|previous| *previous >= onset)
                {
                    return Err(format!(
                        "raw acoustic onsets must be strictly increasing at row {row_number}"
                    ));
                }
                state.events.push(onset);
            }
            "pending" | "complete" => {
                if !fields[8].is_empty() || state.status.is_some() {
                    return Err(format!(
                        "invalid or duplicate item status at row {row_number}"
                    ));
                }
                let status = if fields[7] == "pending" {
                    Completion::Pending
                } else {
                    Completion::Complete
                };
                if status == Completion::Pending && !state.events.is_empty() {
                    return Err(format!("pending item contains events at row {row_number}"));
                }
                state.status = Some(status);
            }
            value => return Err(format!("unknown row_type at row {row_number}: {value}")),
        }
    }
    if !saw_row || rows.iter().any(|row| row.status.is_none()) {
        return Err("annotation must contain exactly one status for every plan item".to_string());
    }
    let completion = if rows
        .iter()
        .all(|row| row.status == Some(Completion::Complete))
    {
        Completion::Complete
    } else {
        Completion::Pending
    };
    let events_by_item = rows
        .into_iter()
        .map(|row| compound_events(&row.events))
        .collect::<Result<Vec<_>, _>>()?;
    Ok(AnnotationArtifact {
        annotator_id: expected_annotator.to_string(),
        raw_sha256: sha256_bytes(bytes),
        completion,
        events_by_item,
    })
}

fn compound_events(raw_onsets: &[i64]) -> Result<Vec<i64>, String> {
    let mut events = Vec::new();
    let mut start = 0_usize;
    while start < raw_onsets.len() {
        let mut end = start + 1;
        while end < raw_onsets.len()
            && raw_onsets[end]
                .checked_sub(raw_onsets[start])
                .is_some_and(|span| span <= COMPOUND_SPAN_MICROS)
        {
            end += 1;
        }
        let sum = raw_onsets[start..end]
            .iter()
            .try_fold(0_i128, |total, onset| total.checked_add(i128::from(*onset)))
            .ok_or("raw acoustic onset sum overflow")?;
        let count = i128::try_from(end - start).map_err(|_| "compound size overflow")?;
        let mean = sum
            .checked_add(count / 2)
            .ok_or("compound acoustic mean overflow")?
            / count;
        events.push(i64::try_from(mean).map_err(|_| "compound acoustic onset overflow")?);
        start = end;
    }
    Ok(events)
}

#[derive(Default)]
struct ItemRows {
    events: Vec<i64>,
    status: Option<Completion>,
}

#[cfg(test)]
mod tests {
    use super::super::manifest::read_synthetic_fixture;
    use super::super::plan::{build_plan, render_plan};
    use super::*;

    fn plan() -> (AuditPlan, String) {
        let path = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("examples/transient_proxy_audit/fixtures/synthetic_proxy_audit_v1.json");
        let source_bytes = fs::read(&path).unwrap();
        let source = read_synthetic_fixture(&path, &sha256_bytes(&source_bytes)).unwrap();
        let plan = build_plan(&source, None).unwrap();
        let hash = sha256_bytes(&render_plan(&plan).unwrap());
        (plan, hash)
    }

    #[test]
    fn untouched_templates_validate_as_not_ready() {
        let (plan, hash) = plan();
        for annotator in ["A", "B"] {
            let template = render_template(&plan, &hash, annotator);
            let artifact =
                validate_annotation_bytes(template.as_bytes(), &plan, &hash, annotator).unwrap();
            assert_eq!(artifact.completion, Completion::Pending);
            assert!(artifact.events_by_item.iter().all(Vec::is_empty));
        }
    }

    #[test]
    fn complete_zero_event_annotation_is_ready_but_identity_is_strict() {
        let (plan, hash) = plan();
        let complete = render_template(&plan, &hash, "A").replace(",pending,\n", ",complete,\n");
        assert_eq!(
            validate_annotation_bytes(complete.as_bytes(), &plan, &hash, "A")
                .unwrap()
                .completion,
            Completion::Complete
        );
        assert!(validate_annotation_bytes(complete.as_bytes(), &plan, &hash, "B").is_err());
        assert!(
            validate_annotation_bytes(complete.as_bytes(), &plan, &"0".repeat(64), "A").is_err()
        );
    }

    #[test]
    fn out_of_range_unsorted_and_event_after_status_fail() {
        let (plan, hash) = plan();
        let base = render_template(&plan, &hash, "A");
        let first = base.lines().nth(1).unwrap();
        let fields = parse_line(first).unwrap();
        let prefix = fields[..7]
            .iter()
            .map(|field| encode(field))
            .collect::<Vec<_>>()
            .join(",");
        for onset in ["-0.000001", "50.000000"] {
            let invalid = format!("{HEADER}\n{prefix},event,{onset}\n{first}\n");
            assert!(
                validate_annotation_bytes(invalid.as_bytes(), &plan, &hash, "A")
                    .unwrap_err()
                    .contains("half-open")
            );
        }

        let after = format!(
            "{HEADER}\n{}\n{prefix},event,1.000000\n",
            first.replace("pending", "complete")
        );
        assert!(
            validate_annotation_bytes(after.as_bytes(), &plan, &hash, "A")
                .unwrap_err()
                .contains("follows")
        );
    }

    #[test]
    fn human_raw_taps_use_the_same_inclusive_non_chaining_compound_rule() {
        let (plan, hash) = plan();
        let base = render_template(&plan, &hash, "A");
        let first = base.lines().nth(1).unwrap();
        let fields = parse_line(first).unwrap();
        let prefix = fields[..7]
            .iter()
            .map(|field| encode(field))
            .collect::<Vec<_>>()
            .join(",");
        let remaining = base.lines().skip(2).collect::<Vec<_>>().join("\n");
        let raw = format!(
            "{HEADER}\n{prefix},event,1.000000\n{prefix},event,1.020000\n{prefix},event,1.040000\n{}\n{remaining}\n",
            first.replace("pending", "complete"),
        );
        let artifact = validate_annotation_bytes(raw.as_bytes(), &plan, &hash, "A").unwrap();
        assert_eq!(artifact.events_by_item[0], [1_010_000, 1_040_000]);
    }

    #[test]
    fn human_compound_mean_rounds_half_up_in_integer_microseconds() {
        assert_eq!(
            compound_events(&[1_000_000, 1_000_001]).unwrap(),
            [1_000_001]
        );
    }
}
