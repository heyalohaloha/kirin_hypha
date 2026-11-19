//! Import scopes and linked-crate scopes inspected by the shipping C ABI guard.
//!
//! The JUCE product links one Rust artifact, the `kirin_hypha_ffi` staticlib
//! (`juce_shell/CMakeLists.txt` builds `-p kirin_hypha_ffi`). The scanned crates are that crate and
//! every workspace crate it links as a normal dependency, on any target. Cargo itself reads the
//! manifests and the resolved graph (`cargo metadata --offline --locked`), so every dependency
//! syntax counts, and a shipping path
//! dependency outside the workspace fails the check instead of dropping out of it.
//!
//! The legacy nih-plug crates (PRE/POST/GUI) are kept only to verify the old VST3 identity and
//! state. They are not linked into the JUCE binaries, and their `nih_export_vst3!` exports come
//! from vendored nih-plug code that this rule cannot guard, so a shipping crate may not depend on
//! nih-plug. Third-party sources are out of scope, but their resolved normal dependencies must
//! not activate `capi` either. A shipping crate may not enable a dependency's `capi` feature
//! (the vendored ebur128 keeps its C API behind it), directly or through its own features.

use serde_json::Value;
use std::path::{Path, PathBuf};

/// Local top-level modules are scanned with their crate; a nested declaration cannot vouch for
/// an unrelated external root at file scope.
pub(super) fn module_roots<'a>(tokens: &[super::Token<'a>]) -> Vec<&'a str> {
    let mut depth = 0;
    let mut roots = Vec::new();
    for (at, token) in tokens.iter().enumerate() {
        if token.is("mod") && depth == 0 {
            if let Some(name) = tokens.get(at + 1).filter(|t| t.kind == super::Kind::Ident) {
                roots.push(name.text);
            }
        }
        if token.is("{") {
            depth += 1;
        }
        if token.is("}") {
            depth -= 1;
        }
    }
    roots
}

/// A `use` tree that could put another macro under an allowed name: a rename to it
/// (`use x::emit as thread_local;`), or an import of it whose path starts outside std and the crate.
pub(super) fn imports_under_allowed_name(
    after_use: &[super::Token],
    macros: &[&str],
    owned_roots: &[&str],
) -> bool {
    let end = after_use.iter().position(|t| t.is(";"));
    let tree = &after_use[..end.unwrap_or(after_use.len())];
    let trusted = ["std", "core", "crate", "$crate", "self", "super"];
    let trusted_root = tree
        .iter()
        .find(|t| !t.is("::"))
        .is_some_and(|t| trusted.contains(&t.text) || owned_roots.contains(&t.text));
    let precise_capture = tree.first().is_some_and(|t| t.is("<")); // `impl Trait + use<'a>`
    !precise_capture
        && ((!trusted_root && tree.iter().any(|t| t.is("*")))
            || tree.iter().enumerate().any(|(i, t)| {
                let allowed = ["thread_local", "assert", "include"].contains(&t.text)
                    || macros.contains(&t.text);
                let raw = i >= 2 && tree[i - 1].is("#") && tree[i - 2].is("r");
                let before = i.checked_sub(if raw { 3 } else { 1 });
                t.kind == super::Kind::Ident
                    && allowed
                    && (!trusted_root || before.is_some_and(|b| tree[b].is("as")))
            }))
}

#[derive(Debug)]
pub(super) struct ShippingCrate {
    pub(super) name: String,
    pub(super) dir: PathBuf,
}

/// Whether one feature value turns on a feature named exactly `capi`: a plain feature, or the
/// feature part of `name/feature` and `name?/feature`. `dep:name` only enables an optional
/// dependency, so names that merely contain the letters (`dep:capillary`) are fine.
fn enables_capi(value: &str) -> bool {
    if value.starts_with("dep:") {
        return false;
    }
    value.rsplit('/').next() == Some("capi")
}

/// The resolved workspace, offline and locked: never download or silently change dependencies.
pub(super) fn workspace_metadata(root: &Path) -> Value {
    let metadata = read_metadata(root, true);
    assert!(
        metadata["resolve"]["nodes"].is_array(),
        "missing resolved graph"
    );
    metadata
}

fn read_metadata(root: &Path, resolve: bool) -> Value {
    let mode = if resolve { "--locked" } else { "--no-deps" };
    let output = std::process::Command::new(env!("CARGO"))
        .args(["metadata", mode, "--format-version", "1", "--offline"])
        .current_dir(root)
        .output()
        .expect("run cargo metadata");
    assert!(
        output.status.success(),
        "cargo metadata failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    serde_json::from_slice(&output.stdout).expect("cargo metadata JSON")
}

/// Follow only linked edges. Dev/build-only nodes and unused feature declarations do not ship.
fn reject_transitive_capi(metadata: &Value, root: &Value) -> Result<(), String> {
    // Manifest-only unit fixtures test the declaration checks below. The real source scan uses
    // workspace_metadata(), which requires a complete, locked resolve graph.
    let Some(nodes) = metadata["resolve"]["nodes"].as_array() else {
        return Ok(());
    };
    let mut pending = vec![&root["id"]];
    let mut seen = Vec::new();
    while let Some(id) = pending.pop() {
        if seen.contains(&id) {
            continue;
        }
        seen.push(id);
        let node = nodes
            .iter()
            .find(|n| &n["id"] == id)
            .ok_or_else(|| format!("missing dependency node {id}"))?;
        if node["features"]
            .as_array()
            .into_iter()
            .flatten()
            .any(|f| f == "capi")
        {
            return Err(format!("shipping dependency {id} enables a C API (capi)"));
        }
        let dependencies = node["deps"]
            .as_array()
            .ok_or("missing resolved dependency edges")?;
        for dependency in dependencies {
            let kinds = dependency["dep_kinds"]
                .as_array()
                .ok_or("missing dependency kinds")?;
            if kinds.iter().any(|kind| kind["kind"].is_null()) {
                pending.push(&dependency["pkg"]);
            }
        }
    }
    Ok(())
}

/// `kirin_hypha_ffi` and every workspace crate it links (normal dependencies, not dev or build).
pub(super) fn shipping_crates(metadata: &Value) -> Result<Vec<ShippingCrate>, String> {
    let packages = metadata["packages"].as_array().ok_or("no packages")?;
    let dir_of = |package: &Value| {
        Path::new(package["manifest_path"].as_str().unwrap_or_default())
            .parent()
            .map(Path::to_path_buf)
            .unwrap_or_default()
    };
    let root = packages
        .iter()
        .find(|package| package["name"] == "kirin_hypha_ffi")
        .ok_or("kirin_hypha_ffi is not a workspace member")?;
    reject_transitive_capi(metadata, root)?;
    let members = metadata["workspace_members"]
        .as_array()
        .ok_or("no workspace members")?;
    let mut pending = vec![root];
    let mut found: Vec<&Value> = Vec::new();
    while let Some(package) = pending.pop() {
        if found.iter().any(|seen| seen["id"] == package["id"]) {
            continue;
        }
        let name = package["name"].as_str().unwrap_or_default();
        // A feature can enable a dependency's feature too (`dep/capi`, `dep?/capi`).
        for (feature, enables) in package["features"].as_object().into_iter().flatten() {
            let enables = enables.as_array().into_iter().flatten();
            if let Some(capi) = enables.filter_map(Value::as_str).find(|f| enables_capi(f)) {
                return Err(format!(
                    "{name} enables a C API in feature {feature} ({capi})"
                ));
            }
        }
        for dependency in package["dependencies"].as_array().into_iter().flatten() {
            if !dependency["kind"].is_null() {
                continue; // dev and build dependencies are not linked into the product
            }
            let dependency_name = dependency["name"].as_str().unwrap_or_default();
            if dependency_name.starts_with("nih_plug") {
                return Err(format!(
                    "{name} links the legacy nih-plug path ({dependency_name})"
                ));
            }
            let features = dependency["features"].as_array().into_iter().flatten();
            if features.filter_map(Value::as_str).any(enables_capi) {
                return Err(format!("{name} enables the C API of {dependency_name}"));
            }
            let Some(path) = dependency["path"].as_str() else {
                continue; // registry and git crates are third-party code
            };
            let member = packages
                .iter()
                .find(|package| {
                    members.contains(&package["id"]) && dir_of(package) == Path::new(path)
                })
                .ok_or_else(|| format!("{name} links {path}, which is not a workspace member"))?;
            pending.push(member);
        }
        found.push(package);
    }
    Ok(found
        .into_iter()
        .map(|package| ShippingCrate {
            name: package["name"].as_str().unwrap_or_default().to_string(),
            dir: dir_of(package),
        })
        .collect())
}

pub(super) fn rust_sources(directory: &Path, out: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).expect("source directory") {
        let path = entry.expect("source entry").path();
        if path.is_dir() {
            rust_sources(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // These fixtures deliberately name absent external crates; only declaration syntax is read.
    fn declaration_metadata(root: &Path) -> Value {
        read_metadata(root, false)
    }

    fn fake_workspace(name: &str, files: &[(&str, &str)]) -> PathBuf {
        let root = std::env::temp_dir().join(format!("ffi-scope-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        for (path, text) in files {
            let path = root.join(path);
            std::fs::create_dir_all(path.parent().unwrap()).unwrap();
            std::fs::write(path, text).unwrap();
        }
        root
    }

    fn package(name: &str, dependencies: &str) -> String {
        format!(
            "[package]\nname = \"{name}\"\nversion = \"0.1.0\"\nedition = \"2021\"\n{dependencies}"
        )
    }

    #[test]
    fn every_dependency_syntax_and_target_counts_but_dev_dependencies_do_not() {
        let ffi = package(
            "kirin_hypha_ffi",
            "[dependencies]\nshared = { workspace = true }\n\
             [dependencies.table_form]\npath = \"../table_form\"\n\
             [target.'cfg(windows)'.dependencies]\nwindows_only = { path = \"../windows_only\" }\n\
             [dev-dependencies]\ntest_only = { path = \"../test_only\" }\n",
        );
        let mut files = vec![
            (
                "Cargo.toml",
                "[workspace]\nmembers = [\"crates/*\"]\nresolver = \"2\"\n\
                 [workspace.dependencies]\nshared = { path = \"crates/shared\" }\n",
            ),
            ("crates/kirin_hypha_ffi/Cargo.toml", ffi.as_str()),
            ("crates/kirin_hypha_ffi/src/lib.rs", ""),
        ];
        let members: Vec<_> = ["shared", "table_form", "windows_only", "test_only"]
            .map(|name| (format!("crates/{name}/Cargo.toml"), package(name, "")))
            .into();
        let sources: Vec<_> = members
            .iter()
            .map(|(manifest, _)| manifest.replace("Cargo.toml", "src/lib.rs"))
            .collect();
        files.extend(
            members
                .iter()
                .map(|(path, text)| (path.as_str(), text.as_str())),
        );
        files.extend(sources.iter().map(|path| (path.as_str(), "")));
        let root = fake_workspace("syntax", &files);
        let mut names: Vec<_> = shipping_crates(&declaration_metadata(&root))
            .unwrap()
            .into_iter()
            .map(|c| c.name)
            .collect();
        names.sort();
        assert_eq!(
            names,
            ["kirin_hypha_ffi", "shared", "table_form", "windows_only"]
        );
        std::fs::remove_dir_all(&root).unwrap();
    }

    #[test]
    fn nih_plug_a_c_api_feature_or_an_outside_path_crate_fails() {
        let outside = package(
            "kirin_hypha_ffi",
            "[dependencies]\noutside = { path = \"../../x\" }\n",
        );
        let outside_crate = package("outside", "");
        for (name, ffi) in [
            (
                "nih",
                package(
                    "kirin_hypha_ffi",
                    "[dependencies]\nnih_plug = { path = \"../nih\" }\n",
                ),
            ),
            (
                "capi",
                package(
                    "kirin_hypha_ffi",
                    "[dependencies]\nlibz = { version = \"1\", features = [\"capi\"] }\n",
                ),
            ),
            ("outside", outside),
            (
                "feature",
                package(
                    "kirin_hypha_ffi",
                    "[dependencies]\nlibz = \"1\"\n[features]\ndefault = [\"libz/capi\"]\n",
                ),
            ),
            (
                "weak-feature",
                package(
                    "kirin_hypha_ffi",
                    "[dependencies]\nlibz = { version = \"1\", optional = true }\n\
                     [features]\nextra = [\"libz?/capi\"]\n",
                ),
            ),
            (
                "own-feature",
                package(
                    "kirin_hypha_ffi",
                    "[features]\ndefault = [\"capi\"]\ncapi = []\n",
                ),
            ),
        ] {
            let root = fake_workspace(
                name,
                &[
                    (
                        "Cargo.toml",
                        "[workspace]\nmembers = [\"crates/kirin_hypha_ffi\"]\nexclude = [\"x\"]\n\
                         resolver = \"2\"\n",
                    ),
                    ("crates/kirin_hypha_ffi/Cargo.toml", ffi.as_str()),
                    ("crates/kirin_hypha_ffi/src/lib.rs", ""),
                    ("crates/nih/Cargo.toml", &package("nih_plug", "")),
                    ("crates/nih/src/lib.rs", ""),
                    ("x/Cargo.toml", outside_crate.as_str()),
                    ("x/src/lib.rs", ""),
                ],
            );
            assert!(
                shipping_crates(&declaration_metadata(&root)).is_err(),
                "{name} accepted"
            );
            std::fs::remove_dir_all(&root).unwrap();
        }
    }

    #[test]
    fn names_that_only_contain_capi_are_accepted() {
        let ffi = package(
            "kirin_hypha_ffi",
            "[dependencies]\ncapillary = { version = \"1\", optional = true }\n\
             libz = { version = \"1\", features = [\"capitals\"] }\n\
             [features]\ndefault = [\"dep:capillary\", \"capillary?/capi_docs\", \"libz/mcapi\", \"capital\"]\n\
             capital = []\n",
        );
        let root = fake_workspace(
            "benign",
            &[
                (
                    "Cargo.toml",
                    "[workspace]\nmembers = [\"crates/kirin_hypha_ffi\"]\nresolver = \"2\"\n",
                ),
                ("crates/kirin_hypha_ffi/Cargo.toml", ffi.as_str()),
                ("crates/kirin_hypha_ffi/src/lib.rs", ""),
            ],
        );
        let shipping = shipping_crates(&declaration_metadata(&root));
        std::fs::remove_dir_all(&root).unwrap();
        assert!(shipping.is_ok(), "{shipping:?}");
    }
}
