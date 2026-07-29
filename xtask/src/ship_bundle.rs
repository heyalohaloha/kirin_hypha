use anyhow::{anyhow, bail, Context, Result};
use serde::Deserialize;
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::process::Command;

const MANIFEST_SOURCE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../config/hypha_macos_ship_bundles.json"
));
const MANIFEST_SCHEMA: &str = "kirin-hypha-macos-ship-bundles-v1";

#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum BundleKind {
    Au,
    Vst3,
}

impl BundleKind {
    pub fn extension(self) -> &'static str {
        match self {
            Self::Au => "component",
            Self::Vst3 => "vst3",
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Au => "au",
            Self::Vst3 => "vst3",
        }
    }
}

#[derive(Clone, Debug, Deserialize)]
pub struct MacBundleSpec {
    pub role: String,
    pub kind: BundleKind,
    pub source_relative: PathBuf,
    pub install_relative: PathBuf,
    pub archive_relative: PathBuf,
    pub executable_name: String,
    pub display_name: String,
    pub component_cid: Option<String>,
    #[serde(default)]
    pub legacy_install_relative: Vec<PathBuf>,
}

impl MacBundleSpec {
    pub fn label(&self) -> String {
        format!("{} {}", self.role, self.kind.as_str().to_uppercase())
    }

    pub fn source_path(&self, build_root: &Path) -> PathBuf {
        build_root.join(&self.source_relative)
    }

    pub fn installed_path(&self, install_root: &Path) -> PathBuf {
        install_root.join(&self.install_relative)
    }

    pub fn archive_path(&self, archive_root: &Path) -> PathBuf {
        archive_root.join(&self.archive_relative)
    }

    pub fn installed_file_name(&self) -> Result<&str> {
        file_name(&self.install_relative)
    }

    pub fn legacy_installed_paths(&self, install_root: &Path) -> Vec<PathBuf> {
        self.legacy_install_relative
            .iter()
            .map(|relative| install_root.join(relative))
            .collect()
    }
}

#[derive(Debug, Deserialize)]
struct ShipManifest {
    schema: String,
    default_build_root: PathBuf,
    bundles: Vec<MacBundleSpec>,
}

pub fn default_build_root() -> Result<PathBuf> {
    Ok(load_manifest()?.default_build_root)
}

pub fn macos_bundles() -> Result<Vec<MacBundleSpec>> {
    Ok(load_manifest()?.bundles)
}

fn load_manifest() -> Result<ShipManifest> {
    let manifest: ShipManifest =
        serde_json::from_str(MANIFEST_SOURCE).context("parse macOS ship bundle manifest")?;
    validate_manifest(&manifest)?;
    Ok(manifest)
}

fn validate_manifest(manifest: &ShipManifest) -> Result<()> {
    if manifest.schema != MANIFEST_SCHEMA {
        bail!(
            "ship bundle manifest schema = {}, expected {MANIFEST_SCHEMA}",
            manifest.schema
        );
    }
    validate_relative_path(&manifest.default_build_root, "default_build_root")?;
    if manifest.bundles.len() != 4 {
        bail!(
            "macOS ship bundle manifest must contain exactly 4 bundles, found {}",
            manifest.bundles.len()
        );
    }

    let mut identities = HashSet::new();
    for spec in &manifest.bundles {
        if !matches!(spec.role.as_str(), "PRE" | "POST") {
            bail!("invalid ship bundle role: {}", spec.role);
        }
        if !identities.insert((spec.role.clone(), spec.kind)) {
            bail!(
                "duplicate ship bundle role/format: {} {}",
                spec.role,
                spec.kind.as_str()
            );
        }
        validate_relative_path(&spec.source_relative, "source_relative")?;
        validate_relative_path(&spec.install_relative, "install_relative")?;
        validate_relative_path(&spec.archive_relative, "archive_relative")?;
        for path in &spec.legacy_install_relative {
            validate_relative_path(path, "legacy_install_relative")?;
        }
        validate_file_component(&spec.executable_name, "executable_name")?;

        let source_ext = spec
            .source_relative
            .extension()
            .and_then(|value| value.to_str());
        let install_ext = spec
            .install_relative
            .extension()
            .and_then(|value| value.to_str());
        let archive_ext = spec
            .archive_relative
            .extension()
            .and_then(|value| value.to_str());
        if source_ext != Some(spec.kind.extension())
            || install_ext != Some(spec.kind.extension())
            || archive_ext != Some(spec.kind.extension())
        {
            bail!(
                "{} bundle extension does not match its format",
                spec.label()
            );
        }
        let expected_install_parent = match spec.kind {
            BundleKind::Au => Path::new("Library/Audio/Plug-Ins/Components"),
            BundleKind::Vst3 => Path::new("Library/Audio/Plug-Ins/VST3"),
        };
        if spec.install_relative.parent() != Some(expected_install_parent) {
            bail!(
                "{} install_relative must be directly under {}",
                spec.label(),
                expected_install_parent.display()
            );
        }
        let expected_archive_parent = match spec.kind {
            BundleKind::Au => Path::new("Audio Unit"),
            BundleKind::Vst3 => Path::new("VST3"),
        };
        if spec.archive_relative.parent() != Some(expected_archive_parent)
            || spec.archive_relative.file_name() != spec.install_relative.file_name()
        {
            bail!(
                "{} archive_relative must preserve the installed file name under {}",
                spec.label(),
                expected_archive_parent.display()
            );
        }
        for legacy in &spec.legacy_install_relative {
            if legacy.parent() != Some(expected_install_parent)
                || legacy.extension().and_then(|value| value.to_str())
                    != Some(spec.kind.extension())
                || legacy == &spec.install_relative
            {
                bail!(
                    "{} legacy install path must be a distinct bundle under {}",
                    spec.label(),
                    expected_install_parent.display()
                );
            }
        }
        if spec
            .source_relative
            .file_stem()
            .and_then(|value| value.to_str())
            != Some(spec.executable_name.as_str())
        {
            bail!(
                "{} source bundle stem must equal executable_name",
                spec.label()
            );
        }
        if !spec.display_name.starts_with(&spec.role) {
            bail!("{} display_name must be role-first", spec.label());
        }
        match spec.kind {
            BundleKind::Au if spec.component_cid.is_some() => {
                bail!("{} AU must not declare a VST3 CID", spec.label())
            }
            BundleKind::Vst3 => {
                let cid = spec
                    .component_cid
                    .as_deref()
                    .context("VST3 component CID missing")?;
                if cid.len() != 32 || !cid.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                    bail!(
                        "{} VST3 CID must be 32 hexadecimal characters",
                        spec.label()
                    );
                }
                if spec
                    .install_relative
                    .file_stem()
                    .and_then(|value| value.to_str())
                    != Some(spec.display_name.as_str())
                {
                    bail!("{} installed VST3 bundle must be role-first", spec.label());
                }
            }
            BundleKind::Au => {}
        }
    }
    Ok(())
}

fn validate_relative_path(path: &Path, field: &str) -> Result<()> {
    if path.as_os_str().is_empty()
        || path.is_absolute()
        || path.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        bail!("{field} must be a safe relative path: {}", path.display());
    }
    Ok(())
}

fn validate_file_component(value: &str, field: &str) -> Result<()> {
    let path = Path::new(value);
    if value.is_empty()
        || path.components().count() != 1
        || !matches!(path.components().next(), Some(Component::Normal(_)))
    {
        bail!("{field} must be one safe file name component: {value}");
    }
    Ok(())
}

fn file_name(path: &Path) -> Result<&str> {
    path.file_name()
        .and_then(|value| value.to_str())
        .ok_or_else(|| anyhow!("non-UTF8 file name: {}", path.display()))
}

pub fn resolve_executable(bundle_path: &Path, spec: &MacBundleSpec) -> Result<PathBuf> {
    let plist = bundle_path.join("Contents/Info.plist");
    let declared = plist_value(&plist, "CFBundleExecutable")?;
    if declared != spec.executable_name {
        bail!(
            "{} CFBundleExecutable = {}, expected {}",
            bundle_path.display(),
            declared,
            spec.executable_name
        );
    }
    validate_file_component(&declared, "CFBundleExecutable")?;
    let binary = bundle_path.join("Contents/MacOS").join(&declared);
    if !binary.is_file() {
        bail!(
            "{} declares CFBundleExecutable {}, but {} is not a file",
            bundle_path.display(),
            declared,
            binary.display()
        );
    }
    Ok(binary)
}

pub fn verify_bundle_contract(bundle_path: &Path, spec: &MacBundleSpec) -> Result<PathBuf> {
    let binary = resolve_executable(bundle_path, spec)?;
    let plist = bundle_path.join("Contents/Info.plist");
    if spec.kind == BundleKind::Au {
        let expected = format!("Kirin: {}", spec.display_name);
        let actual = plist_value(&plist, "AudioComponents:0:name")?;
        if actual != expected {
            bail!(
                "{} AudioComponents:0:name = {}, expected {}",
                bundle_path.display(),
                actual,
                expected
            );
        }
        return Ok(binary);
    }

    for key in ["CFBundleDisplayName", "CFBundleName"] {
        let actual = plist_value(&plist, key)?;
        if actual != spec.executable_name {
            bail!(
                "{} {} = {}, expected {}",
                bundle_path.display(),
                key,
                actual,
                spec.executable_name
            );
        }
    }
    let moduleinfo = fs::read_to_string(bundle_path.join("Contents/Resources/moduleinfo.json"))
        .with_context(|| format!("read VST3 moduleinfo for {}", bundle_path.display()))?;
    let cid = spec
        .component_cid
        .as_deref()
        .context("VST3 component CID missing")?;
    if !moduleinfo.contains(&format!("\"CID\": \"{cid}\""))
        || !moduleinfo.contains(&format!("\"Name\": \"{}\"", spec.display_name))
    {
        bail!(
            "{} VST3 CID/display contract mismatch",
            bundle_path.display()
        );
    }
    verify_binary_contains_display_name(&binary, &spec.display_name)?;
    Ok(binary)
}

pub fn verify_binary_copy(
    source_bundle: &Path,
    installed_bundle: &Path,
    spec: &MacBundleSpec,
) -> Result<(PathBuf, PathBuf)> {
    let source_binary = resolve_executable(source_bundle, spec)?;
    let installed_binary = resolve_executable(installed_bundle, spec)?;
    if !files_equal(&source_binary, &installed_binary)? {
        bail!(
            "binary mismatch after install: {} source={} installed={}",
            spec.label(),
            source_binary.display(),
            installed_binary.display()
        );
    }
    Ok((source_binary, installed_binary))
}

fn files_equal(left: &Path, right: &Path) -> Result<bool> {
    if fs::metadata(left)?.len() != fs::metadata(right)?.len() {
        return Ok(false);
    }
    let mut left = File::open(left)?;
    let mut right = File::open(right)?;
    let mut left_buf = [0u8; 64 * 1024];
    let mut right_buf = [0u8; 64 * 1024];
    loop {
        let left_len = left.read(&mut left_buf)?;
        let right_len = right.read(&mut right_buf)?;
        if left_len != right_len || left_buf[..left_len] != right_buf[..right_len] {
            return Ok(false);
        }
        if left_len == 0 {
            return Ok(true);
        }
    }
}

fn verify_binary_contains_display_name(binary: &Path, expected: &str) -> Result<()> {
    let out = Command::new("strings")
        .arg(binary)
        .output()
        .with_context(|| format!("spawn strings for {}", binary.display()))?;
    if !out.status.success() {
        bail!(
            "strings failed for {}: {}",
            binary.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    if !String::from_utf8_lossy(&out.stdout).contains(expected) {
        bail!(
            "{} does not contain display name {}",
            binary.display(),
            expected
        );
    }
    Ok(())
}

pub(crate) fn plist_value(plist: &Path, key: &str) -> Result<String> {
    let out = Command::new("/usr/libexec/PlistBuddy")
        .args(["-c", &format!("Print :{key}")])
        .arg(plist)
        .output()
        .with_context(|| format!("spawn PlistBuddy for {}", plist.display()))?;
    if !out.status.success() {
        bail!(
            "PlistBuddy failed for {}: {}",
            plist.display(),
            String::from_utf8_lossy(&out.stderr)
        );
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}

pub fn run_verify(args: Vec<String>) -> Result<()> {
    let mut build_root = default_build_root()?;
    let mut installed_root: Option<PathBuf> = None;
    let mut format: Option<BundleKind> = None;
    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--build-root" => {
                build_root = PathBuf::from(iter.next().context("--build-root requires a value")?)
            }
            "--installed-root" => {
                installed_root = Some(PathBuf::from(
                    iter.next().context("--installed-root requires a value")?,
                ))
            }
            "--format" => {
                format = Some(
                    match iter
                        .next()
                        .context("--format requires au or vst3")?
                        .as_str()
                    {
                        "au" => BundleKind::Au,
                        "vst3" => BundleKind::Vst3,
                        other => bail!("unsupported bundle format: {other}"),
                    },
                )
            }
            other => bail!("unknown ship-bundle-verify argument: {other}"),
        }
    }

    let bundles = macos_bundles()?;
    for spec in bundles
        .iter()
        .filter(|spec| format.is_none_or(|kind| spec.kind == kind))
    {
        let source = spec.source_path(&build_root);
        verify_bundle_contract(&source, spec)
            .with_context(|| format!("{} source metadata verification failed", spec.label()))?;
        let verified_path = if let Some(root) = &installed_root {
            let installed = spec.installed_path(root);
            verify_binary_copy(&source, &installed, spec)
                .with_context(|| format!("{} staged binary verification failed", spec.label()))?;
            verify_bundle_contract(&installed, spec)
                .with_context(|| format!("{} staged metadata verification failed", spec.label()))?;
            installed
        } else {
            source
        };
        println!(
            "[ship-bundle-verify] {} OK: {}",
            spec.label(),
            verified_path.display()
        );
    }
    Ok(())
}

#[cfg(test)]
#[path = "ship_bundle/tests.rs"]
mod tests;
