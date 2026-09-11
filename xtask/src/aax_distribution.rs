use anyhow::{bail, Context, Result};
use serde::Deserialize;
use std::collections::HashSet;
use std::fs;
use std::path::{Component, Path, PathBuf};
use std::process::Command;

use crate::release_package_metadata::BundleEntry;

const MANIFEST_SOURCE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../config/hypha_macos_aax_bundles.json"
));
const MANIFEST_SCHEMA: &str = "kirin-hypha-macos-aax-bundles-v1";
const INSTALL_PARENT: &str = "Library/Application Support/Avid/Audio/Plug-Ins";
const ARCHIVE_PARENT: &str = "AAX";
const VERIFY_SCRIPT: &str = "scripts/ls_release/aax_bundle_verify.mjs";

#[derive(Clone, Debug, Deserialize)]
pub struct AaxBundleSpec {
    role: String,
    source_relative: PathBuf,
    install_relative: PathBuf,
    archive_relative: PathBuf,
    executable_name: String,
    bundle_identifier: String,
}

#[derive(Debug, Deserialize)]
struct AaxManifest {
    schema: String,
    default_build_root: PathBuf,
    bundles: Vec<AaxBundleSpec>,
}

pub struct AaxBundle {
    pub spec: AaxBundleSpec,
    pub source: PathBuf,
}

impl AaxBundle {
    fn label(&self) -> String {
        format!("{} AAX", self.spec.role)
    }

    fn archive_path(&self, root: &Path) -> PathBuf {
        root.join(&self.spec.archive_relative)
    }
}

pub fn bundles() -> Result<Vec<AaxBundle>> {
    let manifest: AaxManifest =
        serde_json::from_str(MANIFEST_SOURCE).context("parse macOS AAX bundle manifest")?;
    validate_manifest(&manifest)?;
    Ok(manifest
        .bundles
        .into_iter()
        .map(|spec| AaxBundle {
            source: manifest.default_build_root.join(&spec.source_relative),
            spec,
        })
        .collect())
}

fn validate_manifest(manifest: &AaxManifest) -> Result<()> {
    if manifest.schema != MANIFEST_SCHEMA {
        bail!(
            "AAX bundle manifest schema = {}, expected {MANIFEST_SCHEMA}",
            manifest.schema
        );
    }
    validate_relative(&manifest.default_build_root, "default_build_root")?;
    if manifest.bundles.len() != 2 {
        bail!("AAX bundle manifest must contain exactly PRE and POST");
    }
    let mut roles = HashSet::new();
    for spec in &manifest.bundles {
        if !matches!(spec.role.as_str(), "PRE" | "POST") || !roles.insert(&spec.role) {
            bail!("AAX bundle role must be unique PRE or POST: {}", spec.role);
        }
        for (value, field) in [
            (&spec.source_relative, "source_relative"),
            (&spec.install_relative, "install_relative"),
            (&spec.archive_relative, "archive_relative"),
        ] {
            validate_relative(value, field)?;
            if value.extension().and_then(|part| part.to_str()) != Some("aaxplugin") {
                bail!("{} {field} must identify an .aaxplugin", spec.role);
            }
        }
        if spec.install_relative.parent() != Some(Path::new(INSTALL_PARENT)) {
            bail!("{} install path is outside the Avid directory", spec.role);
        }
        if spec.archive_relative.parent() != Some(Path::new(ARCHIVE_PARENT))
            || spec.archive_relative.file_name() != spec.install_relative.file_name()
        {
            bail!(
                "{} archive path must preserve its installed name",
                spec.role
            );
        }
        validate_component(&spec.executable_name, "executable_name")?;
        validate_component(&spec.bundle_identifier, "bundle_identifier")?;
        if spec
            .source_relative
            .file_stem()
            .and_then(|part| part.to_str())
            != Some(&spec.executable_name)
        {
            bail!("{} source name must match executable_name", spec.role);
        }
    }
    Ok(())
}

fn validate_relative(value: &Path, field: &str) -> Result<()> {
    if value.as_os_str().is_empty()
        || value.is_absolute()
        || value.components().any(|part| {
            matches!(
                part,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        bail!(
            "AAX {field} must be a safe relative path: {}",
            value.display()
        );
    }
    Ok(())
}

fn validate_component(value: &str, field: &str) -> Result<()> {
    let path = Path::new(value);
    if value.is_empty()
        || path.components().count() != 1
        || !matches!(path.components().next(), Some(Component::Normal(_)))
    {
        bail!("AAX {field} must be one safe value: {value}");
    }
    Ok(())
}

pub fn current_source_id() -> Result<String> {
    let output = Command::new("git")
        .args(["rev-parse", "HEAD"])
        .output()
        .context("resolve current source id for AAX distribution")?;
    if !output.status.success() {
        bail!(
            "git rev-parse failed while resolving AAX source id: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }
    let value = String::from_utf8(output.stdout)
        .context("AAX source id is not UTF-8")?
        .trim()
        .to_string();
    if value.len() != 40 || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        bail!("invalid AAX source id: {value}");
    }
    Ok(value)
}

pub fn verify_sources(bundles: &[AaxBundle], version: &str, source_id: &str) -> Result<()> {
    for bundle in bundles {
        verify_bundle(bundle, &bundle.source, None, version, source_id)
            .with_context(|| format!("{} source verification failed", bundle.label()))?;
    }
    Ok(())
}

pub fn stage_archives(
    bundles: &[AaxBundle],
    archive_root: &Path,
    version: &str,
    source_id: &str,
) -> Result<()> {
    for bundle in bundles {
        let destination = bundle.archive_path(archive_root);
        fs::create_dir_all(destination.parent().context("AAX archive parent missing")?)?;
        run_status(
            Command::new("ditto").arg(&bundle.source).arg(&destination),
            "ditto AAX bundle into archive",
        )?;
        verify_bundle(
            bundle,
            &destination,
            Some(&bundle.source),
            version,
            source_id,
        )
        .with_context(|| format!("{} staged archive verification failed", bundle.label()))?;
    }
    Ok(())
}

pub fn verify_zip(
    bundles: &[AaxBundle],
    zip_path: &Path,
    package_root_name: &str,
    version: &str,
    source_id: &str,
) -> Result<()> {
    let temporary = TemporaryDirectory::create("kirin_hypha_aax_zip")?;
    run_status(
        Command::new("ditto")
            .args(["-x", "-k"])
            .arg(zip_path)
            .arg(&temporary.0),
        "extract AAX release zip",
    )?;
    let archive_root = temporary.0.join(package_root_name);
    for bundle in bundles {
        let extracted = bundle.archive_path(&archive_root);
        verify_bundle(bundle, &extracted, Some(&bundle.source), version, source_id)
            .with_context(|| format!("{} extracted zip verification failed", bundle.label()))?;
    }
    Ok(())
}

pub fn metadata_entries(bundles: &[AaxBundle]) -> Result<Vec<BundleEntry>> {
    bundles
        .iter()
        .map(|bundle| {
            Ok(BundleEntry {
                label: bundle.label(),
                format: ARCHIVE_PARENT.to_string(),
                file: bundle
                    .spec
                    .archive_relative
                    .file_name()
                    .and_then(|part| part.to_str())
                    .context("AAX archive file name must be UTF-8")?
                    .to_string(),
            })
        })
        .collect()
}

pub fn print_dry_run(bundles: &[AaxBundle]) {
    for bundle in bundles {
        eprintln!(
            "  include:      {} -> {}",
            bundle.source.display(),
            bundle.spec.archive_relative.display()
        );
    }
}

fn verify_bundle(
    bundle: &AaxBundle,
    destination: &Path,
    source: Option<&Path>,
    version: &str,
    source_id: &str,
) -> Result<()> {
    let mut command = Command::new("node");
    command
        .arg(VERIFY_SCRIPT)
        .args(["--bundle", path_text(destination)?])
        .args(["--executable", &bundle.spec.executable_name])
        .args(["--identifier", &bundle.spec.bundle_identifier])
        .args(["--version", version])
        .args(["--source-id", source_id])
        .args(["--source-state", "clean source"])
        .arg("--require-kimera")
        .arg("--require-native-only");
    if let Some(source) = source {
        command.args(["--source", path_text(source)?]);
    }
    run_status(&mut command, "verify AAX bundle")
}

fn path_text(path: &Path) -> Result<&str> {
    path.to_str()
        .with_context(|| format!("path is not UTF-8: {}", path.display()))
}

fn run_status(command: &mut Command, label: &str) -> Result<()> {
    let status = command.status().with_context(|| format!("spawn {label}"))?;
    if !status.success() {
        bail!("{label} failed with status {status}");
    }
    Ok(())
}

struct TemporaryDirectory(PathBuf);

impl TemporaryDirectory {
    fn create(label: &str) -> Result<Self> {
        let path = std::env::temp_dir().join(format!(
            "{label}_{}_{}",
            std::process::id(),
            rand::random::<u64>()
        ));
        fs::create_dir_all(&path)?;
        Ok(Self(path))
    }
}

impl Drop for TemporaryDirectory {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn manifest_has_exact_pre_and_post_aax_contract() {
        let bundles = bundles().unwrap();
        assert_eq!(bundles.len(), 2);
        assert_eq!(bundles[0].spec.role, "PRE");
        assert_eq!(bundles[1].spec.role, "POST");
        assert!(bundles.iter().all(|bundle| {
            bundle.source.starts_with("build-aax-universal")
                && bundle.spec.install_relative.starts_with(INSTALL_PARENT)
                && bundle.spec.archive_relative.starts_with(ARCHIVE_PARENT)
        }));
    }
}
