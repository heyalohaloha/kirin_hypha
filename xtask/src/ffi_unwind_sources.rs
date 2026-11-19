//! Collect the owned source files before the C ABI guard inventories macros.
//!
//! Preserve the historical scan of the package's src tree, and also follow the
//! actual Cargo library entry and its source declarations. Conditional module
//! paths are traversed conservatively without evaluating the host's cfg flags.

use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};
use syn::parse::Parser;
use syn::punctuated::Punctuated;
use syn::visit::Visit;
use syn::{Attribute, Expr, ItemMod, Lit, Meta, Token};

#[cfg(test)]
#[path = "ffi_unwind_sources_tests.rs"]
mod tests;

pub(super) fn shipping_sources(
    workspace: &Path,
    package: &Path,
    library_roots: &[PathBuf],
) -> Result<Vec<PathBuf>, String> {
    let workspace = fs::canonicalize(workspace).map_err(|e| context(workspace, e))?;
    let package = fs::canonicalize(package).map_err(|e| context(package, e))?;
    let mut scan = Sources {
        workspace,
        files: BTreeSet::new(),
        visited: BTreeSet::new(),
        active: BTreeSet::new(),
        parsed: BTreeSet::new(),
    };
    if !package.starts_with(&scan.workspace) || library_roots.is_empty() {
        return Err(context(
            &package,
            "package outside workspace or no library source",
        ));
    }
    let src = package.join("src");
    match fs::symlink_metadata(&src) {
        Ok(_) => scan.collect_src(&src)?,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
        Err(e) => return Err(context(&src, e)),
    }
    for root in library_roots {
        scan.file(root, FileKind::Root)?;
    }
    // Historical files outside the known declaration graph still contribute to
    // the guard's macro inventory. Inspect their macro token trees too, but do
    // not guess a module context or add their unreachable declarations.
    for file in scan.files.difference(&scan.parsed) {
        let source = fs::read_to_string(file).map_err(|e| context(file, e))?;
        let source = syn::parse_file(&source).map_err(|e| context(file, e))?;
        let mut audit = OpaqueMacros { found: false };
        audit.visit_file(&source);
        if audit.found {
            return Err(context(file, "source declaration in opaque macro tokens"));
        }
    }
    Ok(scan.files.into_iter().collect())
}

fn context(path: &Path, error: impl std::fmt::Display) -> String {
    format!("{}: {error}", path.display())
}

#[derive(Clone, Copy, Debug)]
enum FileKind {
    Root,
    Implicit,
    Explicit,
    Included,
}

struct Sources {
    workspace: PathBuf,
    files: BTreeSet<PathBuf>,
    // One physical file may be included in different logical module directories.
    visited: BTreeSet<(PathBuf, PathBuf, PathBuf)>,
    active: BTreeSet<PathBuf>,
    parsed: BTreeSet<PathBuf>,
}

impl Sources {
    fn collect_src(&mut self, directory: &Path) -> Result<(), String> {
        let kind = fs::symlink_metadata(directory)
            .map_err(|e| context(directory, e))?
            .file_type();
        if !kind.is_dir() || kind.is_symlink() {
            return Err(context(directory, "expected an owned source directory"));
        }
        for entry in fs::read_dir(directory).map_err(|e| context(directory, e))? {
            let entry = entry.map_err(|e| context(directory, e))?;
            let path = entry.path();
            let kind = entry.file_type().map_err(|e| context(&path, e))?;
            if kind.is_symlink() {
                return Err(context(
                    &path,
                    "symlink in source inventory requires review",
                ));
            }
            if kind.is_dir() {
                self.collect_src(&path)?;
            } else if path.extension().is_some_and(|ext| ext == "rs") {
                if !kind.is_file() {
                    return Err(context(&path, "expected a regular source file"));
                }
                self.files.insert(path);
            }
        }
        Ok(())
    }

    fn file(&mut self, spelling: &Path, kind: FileKind) -> Result<(), String> {
        let path = fs::canonicalize(spelling).map_err(|e| context(spelling, e))?;
        if !path.starts_with(&self.workspace) || !path.is_file() {
            return Err(context(spelling, "not an owned regular workspace source"));
        }
        let parent = spelling
            .parent()
            .ok_or_else(|| context(spelling, "source has no parent directory"))?;
        let stem = spelling
            .file_stem()
            .ok_or_else(|| context(spelling, "source has no file stem"))?;
        // A mod.rs, crate root, or explicitly assigned module owns its directory.
        // An implicit foo.rs has a separate foo directory for default modules.
        let conventional = if matches!(kind, FileKind::Implicit)
            && spelling.file_name().is_some_and(|name| name != "mod.rs")
        {
            parent.join(stem)
        } else {
            parent.to_path_buf()
        };
        // Explicit #[path] and include! files reset ownership to their parent directory.
        // Only implicit foo.rs contributes foo/ for nested inline/default modules.
        // Compiler-backed fixtures below keep these distinct from the logical module path.
        let module_dir = conventional;
        let inline_dir = module_dir.clone();
        let key = (
            spelling.to_path_buf(),
            module_dir.clone(),
            inline_dir.clone(),
        );
        if self.active.contains(&path) {
            return Err(context(spelling, "recursive source reference"));
        }
        if !self.visited.insert(key) {
            return Ok(());
        }
        self.active.insert(path.clone());
        self.files.insert(path.clone());
        let text = fs::read_to_string(&path).map_err(|e| context(spelling, e))?;
        let parsed = syn::parse_file(&text).map_err(|e| context(spelling, e))?;
        self.parsed.insert(path.clone());
        let mut visitor = DeclarationWalk {
            sources: self,
            source: spelling,
            module_dir,
            attr_dir: parent.to_path_buf(),
            inline_dir,
            error: None,
        };
        visitor.visit_file(&parsed);
        let error = visitor.error;
        self.active.remove(&path);
        error.map_or(Ok(()), Err)
    }
}

struct Paths {
    direct: Option<String>,
    conditional: Vec<String>,
}

fn path_value(meta: &Meta) -> Result<String, String> {
    if let Meta::NameValue(value) = meta {
        if let Expr::Lit(literal) = &value.value {
            if let Lit::Str(text) = &literal.lit {
                return Ok(text.value());
            }
        }
    }
    Err("module path must be a string literal".into())
}

fn conditional_paths(meta: &Meta, out: &mut Vec<String>) -> Result<(), String> {
    if meta.path().is_ident("path") {
        out.push(path_value(meta)?);
    } else if meta.path().is_ident("cfg_attr") {
        let Meta::List(list) = meta else {
            return Err("invalid cfg_attr".into());
        };
        let nested = Punctuated::<Meta, Token![,]>::parse_terminated
            .parse2(list.tokens.clone())
            .map_err(|e| format!("invalid cfg_attr: {e}"))?;
        if nested.is_empty() {
            return Err("missing cfg_attr predicate".into());
        }
        for item in nested.iter().skip(1) {
            conditional_paths(item, out)?;
        }
    }
    Ok(())
}

fn paths(attrs: &[Attribute]) -> Result<Paths, String> {
    let mut paths = Paths {
        direct: None,
        conditional: Vec::new(),
    };
    for attr in attrs {
        if attr.path().is_ident("path") {
            if paths.direct.replace(path_value(&attr.meta)?).is_some() {
                return Err("multiple unconditional module paths".into());
            }
        } else {
            conditional_paths(&attr.meta, &mut paths.conditional)?;
        }
    }
    Ok(paths)
}

fn relative(base: &Path, value: &str) -> Result<PathBuf, String> {
    let value = Path::new(value);
    if value.as_os_str().is_empty()
        || value.components().any(|c| {
            matches!(
                c,
                std::path::Component::RootDir | std::path::Component::Prefix(_)
            )
        })
    {
        return Err(context(value, "expected a nonempty relative source path"));
    }
    Ok(base.join(value))
}

fn implicit_module(directory: &Path, name: &str) -> Result<PathBuf, String> {
    let candidates = [
        directory.join(format!("{name}.rs")),
        directory.join(name).join("mod.rs"),
    ];
    let mut available = Vec::new();
    for path in candidates {
        match fs::symlink_metadata(&path) {
            Ok(_) => available.push(path),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
            Err(e) => return Err(context(&path, e)),
        }
    }
    if available.len() == 1 {
        Ok(available.remove(0))
    } else {
        let error = if available.is_empty() {
            "missing default module source"
        } else {
            "ambiguous default module source"
        };
        Err(context(&directory.join(name), error))
    }
}

struct DeclarationWalk<'scan, 'source> {
    sources: &'scan mut Sources,
    source: &'source Path,
    module_dir: PathBuf,
    attr_dir: PathBuf,
    inline_dir: PathBuf,
    error: Option<String>,
}

impl DeclarationWalk<'_, '_> {
    fn module(&mut self, item: &ItemMod) -> Result<(), String> {
        let Paths {
            direct,
            conditional,
        } = paths(&item.attrs)?;
        let ident = item.ident.to_string();
        let name = ident.strip_prefix("r#").unwrap_or(&ident);
        if let Some((_, items)) = &item.content {
            let mut alternatives = BTreeSet::new();
            if direct.is_none() {
                alternatives.insert(self.inline_dir.join(name));
            }
            for path in direct.iter().chain(&conditional) {
                alternatives.insert(relative(&self.attr_dir, path)?);
            }
            for directory in alternatives {
                let mut nested = DeclarationWalk {
                    sources: self.sources,
                    source: self.source,
                    module_dir: directory.clone(),
                    attr_dir: directory.clone(),
                    inline_dir: directory,
                    error: None,
                };
                for item in items {
                    nested.visit_item(item);
                }
                if let Some(error) = nested.error {
                    return Err(error);
                }
            }
        } else {
            if direct.is_none() {
                let path = implicit_module(&self.module_dir, name)?;
                self.sources.file(&path, FileKind::Implicit)?;
            }
            for path in direct.iter().chain(&conditional) {
                let path = relative(&self.attr_dir, path)?;
                self.sources.file(&path, FileKind::Explicit)?;
            }
        }
        Ok(())
    }

    fn include(&mut self, mac: &syn::Macro) -> Result<(), String> {
        let path = mac
            .parse_body::<syn::LitStr>()
            .map_err(|_| "include! source must be a static string literal".to_string())?;
        let parent = self
            .source
            .parent()
            .ok_or("include! source has no parent directory")?;
        let path = relative(parent, &path.value())?;
        self.sources.file(&path, FileKind::Included)
    }
}

impl<'ast> Visit<'ast> for DeclarationWalk<'_, '_> {
    fn visit_item_mod(&mut self, item: &'ast ItemMod) {
        if self.error.is_none() {
            self.error = self.module(item).err().map(|e| context(self.source, e));
        }
    }

    fn visit_macro(&mut self, mac: &'ast syn::Macro) {
        if self.error.is_some() {
            return;
        }
        let name = mac.path.segments.last().map(|segment| &segment.ident);
        if name.is_some_and(|name| name == "include") {
            self.error = self.include(mac).err().map(|e| context(self.source, e));
        } else if has_opaque_source(mac) {
            self.error = Some(context(
                self.source,
                "source declaration in opaque macro tokens",
            ));
        }
    }
}

fn has_opaque_source(mac: &syn::Macro) -> bool {
    // The AST cannot resolve source declarations embedded in opaque macro
    // tokens. Do not silently treat them as absent.
    super::tokens(&mac.tokens.to_string())
        .iter()
        .any(|token| token.is("mod") || token.is("include"))
}

struct OpaqueMacros {
    found: bool,
}

impl<'ast> Visit<'ast> for OpaqueMacros {
    fn visit_macro(&mut self, mac: &'ast syn::Macro) {
        let declares_source = mac
            .path
            .segments
            .last()
            .is_some_and(|s| s.ident == "include");
        self.found |= declares_source || has_opaque_source(mac);
    }
}
