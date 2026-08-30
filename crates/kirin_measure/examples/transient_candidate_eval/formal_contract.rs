use std::fs;
use std::path::{Component, Path, PathBuf};

use serde::{Deserialize, Serialize};

use super::{sha256_bytes, Cli, Purpose};

const AUTHORIZATION_SCHEMA: &str = "kirin-hypha-attack-formal-evaluation-authorization-v1";
const AUTHORIZATION_CHAIN_DOMAIN: &str =
    "kirin-hypha-attack-formal-evaluation-authorization-chain-v1";
const DEVELOPMENT_SELECTION_SCHEMA: &str = "kirin-hypha-attack-drum-development-receipt-v2";
const MIDI_ARCHIVE_MEMBER_SCHEMA: &str = "kirin-hypha-attack-midi-archive-member-receipt-v1";
const AUDIO_INGEST_SCHEMA: &str = "kirin-hypha-attack-audio-ingest-receipt-v1";
const FOLD_BALANCE_SCHEMA: &str = "kirin-hypha-attack-fold-balance-receipt-v1";
const BLIND_PROXY_AUDIT_SCHEMA: &str = "kirin-hypha-attack-midi-proxy-audit-result-v1";
const CANDIDATE_PLAN_SCHEMA: &str = "kirin-hypha-attack-candidate-plan-receipt-v1";

const SEMANTIC_VERIFIER_BLOCKERS: [&str; 11] = [
    "formal_authorization_not_pinned_in_source_commit",
    "git_commit_argument_not_authenticated",
    "development_selection_semantic_verifier_not_implemented",
    "midi_archive_member_provenance_verifier_not_implemented",
    "audio_ingest_and_duplicate_verifier_not_implemented",
    "fold_balance_qualification_verifier_not_implemented",
    "blind_proxy_audit_verifier_not_implemented",
    "candidate_plan_ordered_configs_stages_controls_verifier_not_implemented",
    "candidate_set_completion_receipt_not_implemented",
    "not_ready_context_guard_unimplemented",
    "lodo_loso_diagnostic_results_not_ready",
];

#[derive(Debug)]
pub(crate) struct FormalArguments {
    pub(crate) folds: PathBuf,
    pub(crate) authorization: PathBuf,
    pub(crate) authorization_sha256: String,
}

/// Private-constructor evidence that only the authorization envelope and its
/// linked bytes were authenticated. The formal gate deliberately never emits
/// this value until all six semantic receipt verifiers exist.
#[derive(Debug)]
pub(crate) struct FormalAuthorization {
    manifest_sha256: String,
    folds_sha256: String,
    authorization_sha256: String,
    chain_sha256: String,
    receipt_sha256: ReceiptHashes,
}

impl FormalAuthorization {
    pub(crate) fn manifest_sha256(&self) -> &str {
        &self.manifest_sha256
    }

    pub(crate) fn folds_sha256(&self) -> &str {
        &self.folds_sha256
    }

    pub(crate) fn authorization_sha256(&self) -> &str {
        &self.authorization_sha256
    }

    pub(crate) fn chain_sha256(&self) -> &str {
        &self.chain_sha256
    }

    pub(crate) fn receipt_sha256(&self) -> &ReceiptHashes {
        &self.receipt_sha256
    }
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ReceiptHashes {
    pub(crate) development_selection: String,
    pub(crate) midi_archive_members: String,
    pub(crate) audio_ingest: String,
    pub(crate) fold_balance: String,
    pub(crate) blind_proxy_audit: String,
    pub(crate) candidate_plan: String,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct AuthorizationWire {
    schema: String,
    purpose: String,
    profile: String,
    dataset: DatasetIdentity,
    manifest_sha256: String,
    folds_sha256: String,
    receipts: ReceiptLinks,
    chain_sha256: String,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct DatasetIdentity {
    id: String,
    version: String,
    archive_sha256: String,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct ReceiptLinks {
    development_selection: ArtifactLink,
    midi_archive_members: ArtifactLink,
    audio_ingest: ArtifactLink,
    fold_balance: ArtifactLink,
    blind_proxy_audit: ArtifactLink,
    candidate_plan: ArtifactLink,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct ArtifactLink {
    relative_path: String,
    sha256: String,
}

#[derive(Deserialize)]
struct ReceiptEnvelope {
    schema: String,
}

pub(crate) fn verify_formal_prerequisites(cli: &Cli) -> Result<FormalAuthorization, String> {
    if cli.purpose != Purpose::FormalDevelopment {
        return Err("formal prerequisite verifier called for non-formal purpose".to_string());
    }
    Err(format!(
        "formal candidate evaluation blocked before authorization or dataset input resolution: {}",
        SEMANTIC_VERIFIER_BLOCKERS.join(",")
    ))
}

#[cfg(test)]
pub(crate) fn synthetic_formal_authorization(
    manifest_sha256: &str,
    folds_sha256: &str,
) -> FormalAuthorization {
    FormalAuthorization {
        manifest_sha256: manifest_sha256.to_string(),
        folds_sha256: folds_sha256.to_string(),
        authorization_sha256: "aa".repeat(32),
        chain_sha256: "bb".repeat(32),
        receipt_sha256: ReceiptHashes {
            development_selection: "01".repeat(32),
            midi_archive_members: "02".repeat(32),
            audio_ingest: "03".repeat(32),
            fold_balance: "04".repeat(32),
            blind_proxy_audit: "05".repeat(32),
            candidate_plan: "06".repeat(32),
        },
    }
}

fn inspect_authorization_envelope(cli: &Cli) -> Result<FormalAuthorization, String> {
    let formal = cli
        .formal
        .as_ref()
        .ok_or("formal purpose is missing formal arguments")?;
    require_sha256(
        &formal.authorization_sha256,
        "formal authorization expected SHA-256",
    )?;
    let authorization_path = fs::canonicalize(&formal.authorization).map_err(|error| {
        format!(
            "cannot resolve formal authorization {}: {error}",
            formal.authorization.display()
        )
    })?;
    if !authorization_path.is_file() {
        return Err("formal authorization is not a file".to_string());
    }
    let bytes = fs::read(&authorization_path)
        .map_err(|error| format!("cannot read formal authorization: {error}"))?;
    let actual_sha256 = sha256_bytes(&bytes);
    if actual_sha256 != formal.authorization_sha256 {
        return Err(format!(
            "formal authorization SHA-256 mismatch: expected {}, got {actual_sha256}",
            formal.authorization_sha256
        ));
    }
    let wire: AuthorizationWire = serde_json::from_slice(&bytes)
        .map_err(|error| format!("invalid formal authorization JSON: {error}"))?;
    validate_wire(cli, &wire)?;
    let expected_chain = authorization_chain_sha256(&wire)?;
    if wire.chain_sha256 != expected_chain {
        return Err(format!(
            "formal authorization chain SHA-256 mismatch: expected {expected_chain}, got {}",
            wire.chain_sha256
        ));
    }
    let base = authorization_path
        .parent()
        .ok_or("formal authorization has no parent directory")?;
    let development_selection = verify_link(
        base,
        &wire.receipts.development_selection,
        DEVELOPMENT_SELECTION_SCHEMA,
    )?;
    let midi_archive_members = verify_link(
        base,
        &wire.receipts.midi_archive_members,
        MIDI_ARCHIVE_MEMBER_SCHEMA,
    )?;
    let audio_ingest = verify_link(base, &wire.receipts.audio_ingest, AUDIO_INGEST_SCHEMA)?;
    let fold_balance = verify_link(base, &wire.receipts.fold_balance, FOLD_BALANCE_SCHEMA)?;
    let blind_proxy_audit = verify_link(
        base,
        &wire.receipts.blind_proxy_audit,
        BLIND_PROXY_AUDIT_SCHEMA,
    )?;
    let candidate_plan = verify_link(base, &wire.receipts.candidate_plan, CANDIDATE_PLAN_SCHEMA)?;
    Ok(FormalAuthorization {
        manifest_sha256: wire.manifest_sha256,
        folds_sha256: wire.folds_sha256,
        authorization_sha256: actual_sha256,
        chain_sha256: expected_chain,
        receipt_sha256: ReceiptHashes {
            development_selection,
            midi_archive_members,
            audio_ingest,
            fold_balance,
            blind_proxy_audit,
            candidate_plan,
        },
    })
}

fn validate_wire(cli: &Cli, wire: &AuthorizationWire) -> Result<(), String> {
    if wire.schema != AUTHORIZATION_SCHEMA
        || wire.purpose != "formal-development"
        || wire.profile != "DRUM"
    {
        return Err("unexpected formal authorization schema/purpose/profile".to_string());
    }
    if wire.dataset.id != cli.dataset_id
        || wire.dataset.version != cli.dataset_version
        || wire.dataset.archive_sha256 != cli.dataset_archive_sha256
    {
        return Err("formal authorization dataset identity mismatch".to_string());
    }
    for (name, value) in [
        ("manifest", wire.manifest_sha256.as_str()),
        ("folds", wire.folds_sha256.as_str()),
        ("chain", wire.chain_sha256.as_str()),
    ] {
        require_sha256(value, &format!("formal {name} SHA-256"))?;
    }
    Ok(())
}

fn verify_link(base: &Path, link: &ArtifactLink, expected_schema: &str) -> Result<String, String> {
    require_sha256(&link.sha256, "linked receipt SHA-256")?;
    let relative = Path::new(&link.relative_path);
    if relative.as_os_str().is_empty()
        || relative.is_absolute()
        || relative
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(format!(
            "invalid linked receipt relative path: {}",
            link.relative_path
        ));
    }
    let path = fs::canonicalize(base.join(relative)).map_err(|error| {
        format!(
            "cannot resolve linked receipt {}: {error}",
            link.relative_path
        )
    })?;
    if !path.starts_with(base) || !path.is_file() {
        return Err(format!(
            "linked receipt escapes authorization directory: {}",
            link.relative_path
        ));
    }
    let bytes = fs::read(&path)
        .map_err(|error| format!("cannot read linked receipt {}: {error}", link.relative_path))?;
    let digest = sha256_bytes(&bytes);
    if digest != link.sha256 {
        return Err(format!(
            "linked receipt SHA-256 mismatch for {}: expected {}, got {digest}",
            link.relative_path, link.sha256
        ));
    }
    let envelope: ReceiptEnvelope = serde_json::from_slice(&bytes).map_err(|error| {
        format!(
            "invalid linked receipt JSON {}: {error}",
            link.relative_path
        )
    })?;
    if envelope.schema != expected_schema {
        return Err(format!(
            "linked receipt schema mismatch for {}: expected {expected_schema}, got {}",
            link.relative_path, envelope.schema
        ));
    }
    Ok(digest)
}

fn authorization_chain_sha256(wire: &AuthorizationWire) -> Result<String, String> {
    let payload = serde_json::json!({
        "domain": AUTHORIZATION_CHAIN_DOMAIN,
        "purpose": wire.purpose,
        "profile": wire.profile,
        "dataset": wire.dataset,
        "manifest_sha256": wire.manifest_sha256,
        "folds_sha256": wire.folds_sha256,
        "receipts": wire.receipts,
    });
    serde_json::to_vec(&payload)
        .map(|bytes| sha256_bytes(&bytes))
        .map_err(|error| format!("cannot hash formal authorization chain: {error}"))
}

fn require_sha256(value: &str, name: &str) -> Result<(), String> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(format!("{name} must be 64 lowercase hex digits"))
    }
}

#[cfg(test)]
#[path = "formal_contract_tests.rs"]
mod tests;
