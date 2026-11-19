use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};

struct Workspace(PathBuf);

impl Workspace {
    fn new() -> Self {
        static COUNTER: AtomicUsize = AtomicUsize::new(0);
        loop {
            let path = std::env::temp_dir().join(format!(
                "ffi-owned-sources-{}-{}",
                std::process::id(),
                COUNTER.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&path) {
                Ok(()) => return Self(path.canonicalize().unwrap()),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {}
                Err(error) => panic!("create source fixture: {error}"),
            }
        }
    }

    fn write(&self, name: &str, text: &str) {
        let path = self.0.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn compile(&self, root: &str) {
        let compiler = std::env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
        let output = std::process::Command::new(compiler)
            .args([
                "--crate-type=lib",
                "--emit=metadata",
                "--edition=2021",
                "--crate-name=source_scope_fixture",
            ])
            .arg(self.0.join("pkg").join(root))
            .arg("-o")
            .arg(self.0.join("fixture.rmeta"))
            .output()
            .expect("run fixture compiler");
        assert!(
            output.status.success(),
            "compiler disagrees with source fixture: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }

    fn scan(&self, root: &str) -> Result<Vec<PathBuf>, String> {
        let package = self.0.join("pkg");
        shipping_sources(&self.0, &package, &[package.join(root)])
    }
}

impl Drop for Workspace {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[test]
fn unions_historical_src_with_actual_library_module_and_include_sources() {
    let ws = Workspace::new();
    ws.write("pkg/src/existing.rs", "pub fn existing() {}");
    ws.write(
        "pkg/other/lib.txt",
        r#"
        mod normal;
        #[path = "custom/unit.txt"] mod explicit;
        #[cfg_attr(windows, path = "conditional.txt")] mod platform;
        mod inline {
            #[path = "inside.txt"] mod renamed;
        }
        include!("sibling.txt");
        "#,
    );
    ws.write(
        "pkg/other/normal.rs",
        "mod child; mod inline { #[path = \"part.rs\"] mod piece; }",
    );
    ws.write("pkg/other/normal/child/mod.rs", "pub fn child() {}");
    ws.write("pkg/other/normal/inline/part.rs", "");
    ws.write("pkg/other/custom/unit.txt", "pub fn chosen() {}");
    ws.write("pkg/other/platform.rs", "pub fn standard() {}");
    ws.write("pkg/other/conditional.txt", "pub fn conditional() {}");
    ws.write("pkg/other/inline/inside.txt", "pub fn nested() {}");
    ws.write("pkg/other/sibling.txt", "pub fn sibling() {}");
    ws.write("pkg/other/unused.rs", "pub fn ignored() {}");

    ws.compile("other/lib.txt");
    let expected: BTreeSet<_> = [
        "src/existing.rs",
        "other/lib.txt",
        "other/normal.rs",
        "other/normal/child/mod.rs",
        "other/normal/inline/part.rs",
        "other/custom/unit.txt",
        "other/platform.rs",
        "other/conditional.txt",
        "other/inline/inside.txt",
        "other/sibling.txt",
    ]
    .into_iter()
    .map(|p| ws.0.join("pkg").join(p))
    .collect();
    assert_eq!(
        ws.scan("other/lib.txt")
            .unwrap()
            .into_iter()
            .collect::<BTreeSet<_>>(),
        expected
    );
}

#[test]
fn explicit_module_children_and_included_items_keep_their_contexts() {
    let ws = Workspace::new();
    ws.write(
        "pkg/other/lib.rs",
        r#"#[path = "custom/unit.rs"] mod explicit; mod outer { include!("items.rs"); }"#,
    );
    ws.write(
        "pkg/other/custom/unit.rs",
        "mod child; mod inner { #[path = \"nested.rs\"] mod renamed; }",
    );
    ws.write("pkg/other/custom/inner/nested.rs", "");
    ws.write("pkg/other/custom/child.rs", "");
    ws.write("pkg/other/items.rs", "mod included_child;");
    ws.write("pkg/other/included_child.rs", "");
    ws.compile("other/lib.rs");
    let files = ws.scan("other/lib.rs").unwrap();
    assert!(files.contains(&ws.0.join("pkg/other/custom/child.rs")));
    assert!(files.contains(&ws.0.join("pkg/other/included_child.rs")));
}

#[test]
fn historical_macro_inventory_does_not_trust_unresolved_source_tokens() {
    let ws = Workspace::new();
    ws.write("pkg/other/lib.rs", "pub fn entry() {}");
    ws.write(
        "pkg/src/disconnected.rs",
        r#"macro_rules! data { () => { "mod" }; }"#,
    );
    assert_eq!(ws.scan("other/lib.rs").unwrap().len(), 2);
    ws.write(
        "pkg/src/disconnected.rs",
        "macro_rules! modules { () => { mod child; }; }",
    );
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("source declaration in opaque macro tokens"));
}

#[test]
fn conditional_default_and_alternate_paths_fail_closed_when_missing() {
    let ws = Workspace::new();
    ws.write(
        "pkg/other/lib.rs",
        "#[cfg_attr(windows, cfg_attr(test, path = \"test.txt\"))] mod choice;",
    );
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("missing default module"));
    ws.write("pkg/other/choice.rs", "");
    assert!(ws.scan("other/lib.rs").unwrap_err().contains("test.txt"));
    ws.write("pkg/other/test.txt", "");
    assert_eq!(ws.scan("other/lib.rs").unwrap().len(), 3);
}

#[test]
fn ambiguous_or_unresolved_source_declarations_never_pass() {
    let ws = Workspace::new();
    ws.write("pkg/other/lib.rs", "mod child;");
    ws.write("pkg/other/child.rs", "");
    ws.write("pkg/other/child/mod.rs", "");
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("ambiguous default module"));

    ws.write("pkg/other/lib.rs", "include!(concat!(\"child\", \".rs\"));");
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("static string literal"));

    ws.write("pkg/other/lib.rs", "#[path = \"\"] mod child;");
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("nonempty relative source path"));

    ws.write("pkg/other/lib.rs", "include!(\"lib.rs\");");
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("recursive source reference"));

    ws.write(
        "pkg/other/lib.rs",
        "macro_rules! modules { () => { mod child; }; }",
    );
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("opaque macro tokens"));

    ws.write("pkg/other/lib.rs", "fn not_closed(");
    assert!(ws.scan("other/lib.rs").is_err());
}

#[cfg(unix)]
#[test]
fn rejects_links_outside_workspace_and_source_directory_cycles() {
    use std::os::unix::fs::symlink;

    let ws = Workspace::new();
    let outside = Workspace::new();
    outside.write("foreign.rs", "pub fn foreign() {}");
    ws.write("pkg/other/lib.rs", "#[path = \"foreign.rs\"] mod foreign;");
    symlink(
        outside.0.join("foreign.rs"),
        ws.0.join("pkg/other/foreign.rs"),
    )
    .unwrap();
    assert!(ws
        .scan("other/lib.rs")
        .unwrap_err()
        .contains("not an owned regular workspace source"));

    ws.write("pkg/src/lib.rs", "");
    symlink(ws.0.join("pkg/src"), ws.0.join("pkg/src/cycle")).unwrap();
    assert!(ws
        .scan("src/lib.rs")
        .unwrap_err()
        .contains("symlink in source inventory"));
}
