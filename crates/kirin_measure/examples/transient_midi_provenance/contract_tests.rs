use super::*;

fn arguments(purpose: &str, profile: &str) -> Vec<OsString> {
    [
        "--purpose",
        purpose,
        "--profile",
        profile,
        "--manifest",
        "/does/not/exist/manifest",
        "--development-receipt",
        "/does/not/exist/receipt",
        "--midi-archive",
        "/does/not/exist/archive",
        "--result",
        "/does/not/exist/result",
    ]
    .into_iter()
    .map(OsString::from)
    .collect()
}

#[test]
fn purpose_and_profile_are_rejected_during_argument_parsing() {
    assert!(Cli::parse(arguments("wrong-purpose", PROFILE)).is_err());
    assert!(Cli::parse(arguments(PURPOSE, "2MIX")).is_err());
    assert!(Cli::parse(arguments(PURPOSE, PROFILE)).is_ok());
}

#[test]
fn duplicate_unknown_and_aliased_result_arguments_fail_closed() {
    let mut duplicate = arguments(PURPOSE, PROFILE);
    duplicate.extend([OsString::from("--result"), OsString::from("again")]);
    assert!(Cli::parse(duplicate).is_err());
    assert!(Cli::parse([OsString::from("--unknown"), OsString::from("x")]).is_err());

    let mut aliased = arguments(PURPOSE, PROFILE);
    let result_index = aliased
        .iter()
        .position(|value| value == "--result")
        .unwrap()
        + 1;
    aliased[result_index] = OsString::from("/does/not/exist/manifest");
    assert!(Cli::parse(aliased).is_err());
}
