use super::*;

fn arguments(purpose: &str, profile: &str) -> Vec<OsString> {
    [
        "--purpose",
        purpose,
        "--profile",
        profile,
        "--manifest",
        "manifest.csv",
        "--development-receipt",
        "development.json",
        "--midi-receipt",
        "midi.json",
        "--audio-archive",
        "audio.zip",
        "--result",
        "audio-receipt.json",
    ]
    .into_iter()
    .map(OsString::from)
    .collect()
}

#[test]
fn cli_accepts_only_the_drum_audio_provenance_boundary() {
    let cli = Cli::parse(arguments(PURPOSE, PROFILE)).unwrap();
    assert_eq!(cli.audio_archive, PathBuf::from("audio.zip"));
    for (purpose, profile) in [
        ("candidate-score", PROFILE),
        (PURPOSE, "2MIX"),
        (PURPOSE, "fresh"),
        (PURPOSE, "test"),
    ] {
        assert!(Cli::parse(arguments(purpose, profile)).is_err());
    }
}

#[test]
fn cli_rejects_unknown_duplicate_missing_and_output_aliases() {
    let mut unknown = arguments(PURPOSE, PROFILE);
    unknown.extend([OsString::from("--fresh"), OsString::from("yes")]);
    assert!(Cli::parse(unknown).unwrap_err().contains("unknown"));

    let mut duplicate = arguments(PURPOSE, PROFILE);
    duplicate.extend([OsString::from("--profile"), OsString::from(PROFILE)]);
    assert!(Cli::parse(duplicate).unwrap_err().contains("duplicate"));

    let mut missing = arguments(PURPOSE, PROFILE);
    missing.pop();
    assert!(Cli::parse(missing).is_err());

    let mut alias = arguments(PURPOSE, PROFILE);
    *alias.last_mut().unwrap() = OsString::from("audio.zip");
    assert!(Cli::parse(alias).unwrap_err().contains("differ"));
}
