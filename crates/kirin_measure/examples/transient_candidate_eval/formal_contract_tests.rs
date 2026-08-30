use std::collections::BTreeMap;

use tempfile::tempdir;

use super::*;

const ARCHIVE_SHA256: &str = "7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053";

#[test]
fn authenticated_synthetic_envelopes_still_fail_before_dataset_resolution() {
    let fixture = make_fixture();
    let inspected = inspect_authorization_envelope(&fixture.cli).unwrap();
    assert_eq!(inspected.manifest_sha256(), "11".repeat(32));
    assert_eq!(inspected.folds_sha256(), "22".repeat(32));
    assert_eq!(
        inspected.authorization_sha256(),
        fixture.authorization_sha256
    );
    assert_eq!(inspected.receipt_sha256().development_selection.len(), 64);

    let error = verify_formal_prerequisites(&fixture.cli).unwrap_err();
    assert!(
        error.contains("blocked before authorization or dataset input resolution"),
        "{error}"
    );
    for blocker in SEMANTIC_VERIFIER_BLOCKERS {
        assert!(error.contains(blocker), "missing {blocker}: {error}");
    }
    assert!(!fixture.cli.root.exists());
    assert!(!fixture.cli.manifest.exists());
    assert!(!fixture.cli.candidate_config.exists());
}

#[test]
fn missing_authorization_is_not_read_before_the_source_pin_blocker() {
    let mut fixture = make_fixture();
    let missing = fixture
        ._directory
        .path()
        .join("definitely-missing-authorization.json");
    fixture.cli.formal.as_mut().unwrap().authorization = missing;
    let error = verify_formal_prerequisites(&fixture.cli).unwrap_err();
    assert!(
        error.contains("formal_authorization_not_pinned_in_source_commit"),
        "{error}"
    );
    assert!(!error.contains("cannot resolve"), "{error}");
}

#[test]
fn linked_bytes_are_rehashed_and_schema_checked() {
    let fixture = make_fixture();
    let formal = fixture.cli.formal.as_ref().unwrap();
    let base = formal.authorization.parent().unwrap();
    fs::write(
        base.join("fold.json"),
        br#"{"schema":"kirin-hypha-attack-fold-balance-receipt-v1","changed":true}"#,
    )
    .unwrap();
    let error = inspect_authorization_envelope(&fixture.cli).unwrap_err();
    assert!(error.contains("linked receipt SHA-256 mismatch"), "{error}");

    let fixture = make_fixture();
    let formal = fixture.cli.formal.as_ref().unwrap();
    let base = formal.authorization.parent().unwrap();
    let bytes = br#"{"schema":"wrong"}"#;
    fs::write(base.join("fold.json"), bytes).unwrap();
    rewrite_link_hash(&formal.authorization, "fold_balance", sha256_bytes(bytes));
    refresh_authorization(&formal.authorization);
    let refreshed = fs::read(&formal.authorization).unwrap();
    let mut cli = fixture.cli;
    cli.formal.as_mut().unwrap().authorization_sha256 = sha256_bytes(&refreshed);
    let error = inspect_authorization_envelope(&cli).unwrap_err();
    assert!(error.contains("schema mismatch"), "{error}");
}

#[test]
fn authorization_hash_and_chain_are_independent_gates() {
    let mut fixture = make_fixture();
    fixture.cli.formal.as_mut().unwrap().authorization_sha256 = "00".repeat(32);
    assert!(inspect_authorization_envelope(&fixture.cli)
        .unwrap_err()
        .contains("authorization SHA-256 mismatch"));

    let fixture = make_fixture();
    let mut value: serde_json::Value =
        serde_json::from_slice(&fs::read(&fixture.authorization_path).unwrap()).unwrap();
    value["chain_sha256"] = serde_json::Value::String("00".repeat(32));
    let bytes = serde_json::to_vec_pretty(&value).unwrap();
    fs::write(&fixture.authorization_path, &bytes).unwrap();
    let mut cli = fixture.cli;
    cli.formal.as_mut().unwrap().authorization_sha256 = sha256_bytes(&bytes);
    assert!(inspect_authorization_envelope(&cli)
        .unwrap_err()
        .contains("chain SHA-256 mismatch"));
}

struct Fixture {
    _directory: tempfile::TempDir,
    cli: Cli,
    authorization_path: PathBuf,
    authorization_sha256: String,
}

fn make_fixture() -> Fixture {
    let directory = tempdir().unwrap();
    let schemas = BTreeMap::from([
        ("development.json", DEVELOPMENT_SELECTION_SCHEMA),
        ("midi.json", MIDI_ARCHIVE_MEMBER_SCHEMA),
        ("audio.json", AUDIO_INGEST_SCHEMA),
        ("fold.json", FOLD_BALANCE_SCHEMA),
        ("audit.json", BLIND_PROXY_AUDIT_SCHEMA),
        ("plan.json", CANDIDATE_PLAN_SCHEMA),
    ]);
    let mut links = BTreeMap::new();
    for (name, schema) in schemas {
        let bytes = serde_json::to_vec(&serde_json::json!({
            "schema": schema,
            "fixture": "synthetic_only"
        }))
        .unwrap();
        fs::write(directory.path().join(name), &bytes).unwrap();
        links.insert(
            name,
            ArtifactLink {
                relative_path: name.to_string(),
                sha256: sha256_bytes(&bytes),
            },
        );
    }
    let mut wire = AuthorizationWire {
        schema: AUTHORIZATION_SCHEMA.to_string(),
        purpose: "formal-development".to_string(),
        profile: "DRUM".to_string(),
        dataset: DatasetIdentity {
            id: "E-GMD".to_string(),
            version: "1.0.0".to_string(),
            archive_sha256: ARCHIVE_SHA256.to_string(),
        },
        manifest_sha256: "11".repeat(32),
        folds_sha256: "22".repeat(32),
        receipts: ReceiptLinks {
            development_selection: links.remove("development.json").unwrap(),
            midi_archive_members: links.remove("midi.json").unwrap(),
            audio_ingest: links.remove("audio.json").unwrap(),
            fold_balance: links.remove("fold.json").unwrap(),
            blind_proxy_audit: links.remove("audit.json").unwrap(),
            candidate_plan: links.remove("plan.json").unwrap(),
        },
        chain_sha256: String::new(),
    };
    wire.chain_sha256 = authorization_chain_sha256(&wire).unwrap();
    let bytes = serde_json::to_vec_pretty(&wire).unwrap();
    let authorization_path = directory.path().join("authorization.json");
    fs::write(&authorization_path, &bytes).unwrap();
    let authorization_sha256 = sha256_bytes(&bytes);
    let cli = Cli {
        root: directory.path().join("must-not-resolve-dataset"),
        manifest: directory.path().join("must-not-read-manifest.csv"),
        candidate_config: directory.path().join("must-not-read-candidate.json"),
        result: directory.path().join("result.json"),
        purpose: Purpose::FormalDevelopment,
        dataset_id: "E-GMD".to_string(),
        dataset_version: "1.0.0".to_string(),
        dataset_archive_sha256: ARCHIVE_SHA256.to_string(),
        git_commit: "aa".repeat(20),
        formal: Some(FormalArguments {
            folds: directory.path().join("must-not-read-folds.csv"),
            authorization: authorization_path.clone(),
            authorization_sha256: authorization_sha256.clone(),
        }),
    };
    Fixture {
        _directory: directory,
        cli,
        authorization_path,
        authorization_sha256,
    }
}

fn rewrite_link_hash(path: &Path, field: &str, digest: String) {
    let mut value: serde_json::Value = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
    value["receipts"][field]["sha256"] = serde_json::Value::String(digest);
    fs::write(path, serde_json::to_vec_pretty(&value).unwrap()).unwrap();
}

fn refresh_authorization(path: &Path) {
    let bytes = fs::read(path).unwrap();
    let mut wire: AuthorizationWire = serde_json::from_slice(&bytes).unwrap();
    wire.chain_sha256 = authorization_chain_sha256(&wire).unwrap();
    fs::write(path, serde_json::to_vec_pretty(&wire).unwrap()).unwrap();
}
