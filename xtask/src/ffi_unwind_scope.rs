//! Which crates the C ABI guard scans: the crates linked into the shipping JUCE product.
//!
//! The JUCE product links one Rust artifact, the `kirin_hypha_ffi` staticlib
//! (`juce_shell/CMakeLists.txt` builds `-p kirin_hypha_ffi`). The scanned crates are that crate and
//! every workspace crate it links as a normal dependency, on any target. Cargo itself reads the
//! manifests (`cargo metadata --no-deps`), so every dependency syntax counts, and a shipping path
//! dependency outside the workspace fails the check instead of dropping out of it.
//!
//! The legacy nih-plug crates (PRE/POST/GUI) are kept only to verify the old VST3 identity and
//! state. They are not linked into the JUCE binaries, and their `nih_export_vst3!` exports come
//! from vendored nih-plug code that this rule cannot guard, so a shipping crate may not depend on
//! nih-plug. Third-party crates are out of scope; a shipping crate may not enable a dependency's
//! `capi` feature (the vendored ebur128 keeps its C API behind it), neither where it declares the
//! dependency nor through one of its own features.

use serde_json::Value;
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub(super) struct ShippingCrate {
    pub(super) name: String,
    pub(super) dir: PathBuf,
}

/// The workspace as Cargo reads it, without resolving or downloading dependencies.
pub(super) fn workspace_metadata(root: &Path) -> Value {
    let output = std::process::Command::new(env!("CARGO"))
        .args([
            "metadata",
            "--no-deps",
            "--format-version",
            "1",
            "--offline",
        ])
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
            if let Some(capi) = enables
                .filter_map(Value::as_str)
                .find(|f| f.contains("capi"))
            {
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
            if features
                .filter_map(Value::as_str)
                .any(|f| f.contains("capi"))
            {
                return Err(format!("{name} enables the C API of {dependency_name}"));
            }
            let Some(path) = dependency["path"].as_str() else {
                continue; // registry and git crates are third-party code
            };
            let member = packages
                .iter()
                .find(|package| dir_of(package) == Path::new(path))
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
        let mut names: Vec<_> = shipping_crates(&workspace_metadata(&root))
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
                shipping_crates(&workspace_metadata(&root)).is_err(),
                "{name} accepted"
            );
            std::fs::remove_dir_all(&root).unwrap();
        }
    }
}
