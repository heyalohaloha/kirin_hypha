use super::*;

fn root() -> PathBuf {
    normalize(&Path::new(env!("CARGO_MANIFEST_DIR")).join(".."))
}

fn summary(source: &str) -> (Vec<(String, bool)>, Vec<String>) {
    let scan = scan(source);
    let entries = scan
        .entries
        .into_iter()
        .map(|e| (e.name, e.guarded))
        .collect();
    (entries, scan.problems)
}

#[test]
fn every_shipping_c_abi_entry_runs_entirely_inside_catch_unwind() {
    let root = root();
    let cmake = std::fs::read_to_string(root.join("juce_shell/CMakeLists.txt")).expect("CMake");
    assert!(cmake.contains("build --release -p kirin_hypha_ffi"));
    let crates = shipping_crates(&root);
    assert!(crates
        .iter()
        .any(|dir| dir.ends_with("crates/kirin_hypha_ffi")));
    assert!(crates
        .iter()
        .any(|dir| dir.ends_with("crates/kirin_measure")));
    let mut names = Vec::new();
    let mut failures = Vec::new();
    for dir in &crates {
        let manifest = std::fs::read_to_string(dir.join("Cargo.toml")).expect("manifest");
        assert!(
            !manifest.contains("nih_plug"),
            "{} links nih-plug",
            dir.display()
        );
        assert!(
            !manifest.contains("capi"),
            "{} enables a C API",
            dir.display()
        );
        let mut files = Vec::new();
        rust_sources(&dir.join("src"), &mut files);
        for file in files {
            let source = std::fs::read_to_string(&file).expect("UTF-8 source");
            let scan = scan(&source);
            for entry in scan.entries {
                if !entry.guarded {
                    failures.push(format!("{}:{} {}", file.display(), entry.line, entry.name));
                }
                names.push(entry.name);
            }
            failures.extend(
                scan.problems
                    .into_iter()
                    .map(|p| format!("{} {p}", file.display())),
            );
        }
    }
    assert!(
        failures.is_empty(),
        "make the whole body `catch_unwind(..)` with a constant fallback:\n{}",
        failures.join("\n")
    );
    // The scan must see the audio-callback entries; an empty or blind scan proves nothing.
    for known in [
        "kirin_hypha_push_samples",
        "kirin_hypha_push_paired_analysis",
        "kirin_hypha_create",
    ] {
        assert!(names.iter().any(|n| n == known), "scan missed {known}");
    }
    let ebur128 = std::fs::read_to_string(root.join("vendor/ebur128/Cargo.toml")).expect("ebur128");
    assert!(!ebur128
        .lines()
        .any(|l| l.starts_with("default") && l.contains("capi")));
}

#[test]
fn the_legacy_nih_plug_crates_are_outside_the_shipping_graph() {
    let crates = shipping_crates(&root());
    for legacy in ["hypha_pre", "hypha_post", "hypha_gui"] {
        assert!(
            !crates.iter().any(|dir| dir.ends_with(legacy)),
            "{legacy} ships"
        );
    }
}

#[test]
fn guarded_shapes_pass() {
    let source = r##"
#[no_mangle]
pub unsafe extern "C" fn closure(h: *mut u8) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if h.is_null() { return false; }
        let brace = '}';
        brace == '}'
    }))
    .unwrap_or(false)
}
#[no_mangle]
pub extern "C" fn discarded() { let _ = std::panic::catch_unwind(|| ()); }
#[no_mangle]
pub extern "C" fn path() { let _ = catch_unwind(AssertUnwindSafe(service::shutdown)); }
#[no_mangle]
pub extern "C" fn pointer() -> *mut u8 {
    catch_unwind(|| std::ptr::null_mut()).unwrap_or(std::ptr::null_mut())
}
#[export_name = "renamed"]
pub extern "C" fn cast<'a>(_x: &'a [u8; 4]) -> u8 {
    catch_unwind(move || 1).unwrap_or(Status::Idle as u8)
}
macro_rules! entry { ($name:ident) => {
    #[no_mangle]
    pub extern "C" fn $name() { let _ = catch_unwind(|| ()); }
} }
entry!(generated);
type Callback = unsafe extern "C" fn(u32) -> bool;
const S: &str = "#[no_mangle] pub extern \"C\" fn in_string() {}";
// #[no_mangle] pub extern "C" fn in_comment() {}
"##;
    let (entries, problems) = summary(source);
    let names: Vec<_> = entries.iter().map(|(n, g)| (n.as_str(), *g)).collect();
    assert_eq!(
        names,
        [
            ("closure", true),
            ("discarded", true),
            ("path", true),
            ("pointer", true),
            ("cast", true),
            ("$name", true)
        ]
    );
    assert!(problems.is_empty(), "{problems:?}");
}

#[test]
fn a_guard_that_does_not_cover_the_whole_body_fails() {
    let source = r#"
#[no_mangle]
pub extern "C" fn nested_helper() -> bool {
    fn inner() -> bool { std::panic::catch_unwind(|| true).unwrap_or(false) }
    inner()
}
#[no_mangle]
pub extern "C" fn inactive_cfg() {
    #[cfg(any())]
    let _ = catch_unwind(|| ());
    panic!()
}
#[no_mangle]
pub extern "C" fn code_after_guard() { let _ = catch_unwind(|| ()); panic!() }
#[no_mangle]
pub extern "C" fn checks_before_guard(p: *mut u8) -> bool {
    if p.is_null() { return false; }
    catch_unwind(|| true).unwrap_or(false)
}
#[no_mangle]
pub extern "C" fn fallback_runs_code() -> u8 { catch_unwind(|| 1).unwrap_or(compute()) }
#[no_mangle]
pub extern "C" /* c */ fn commented /* c */ () { panic!() }
#[no_mangle]
pub extern "C" fn text_only() { let _ = "catch_unwind(|| ())"; }
"#;
    let (entries, problems) = summary(source);
    assert_eq!(entries.len(), 7);
    assert!(entries.iter().all(|(_, guarded)| !guarded), "{entries:?}");
    assert!(entries.iter().any(|(name, _)| name == "commented"));
    assert!(problems.is_empty(), "{problems:?}");
}

#[test]
fn exports_the_scanner_cannot_see_fail() {
    for source in [
        "#[no_mangle]\npub fn rust_abi() {}",
        "#[unsafe(no_mangle)]\npub static TABLE: u8 = 0;",
        "#[export_name = \"x\"]\npub fn renamed() {}",
        "nih_export_vst3!(Plugin);",
        "#[plugin::export]\nfn proc_macro() {}",
        "fn catch_unwind<F>(f: F) {}",
        "use crate::fake as AssertUnwindSafe;",
        "macro_rules! catch_unwind { () => {} }",
    ] {
        let (_, problems) = summary(source);
        assert!(!problems.is_empty(), "accepted: {source}");
    }
}

#[test]
fn the_scope_follows_workspace_path_dependencies() {
    let root = tempfile_root();
    let write = |path: &str, text: &str| {
        let path = root.join(path);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
    };
    write(
        "Cargo.toml",
        "[workspace.dependencies]\ncore = { path = \"crates/core\" }\nserde = \"1\"\n[patch]\n",
    );
    write(
        "crates/kirin_hypha_ffi/Cargo.toml",
        "[dependencies]\ncore = { workspace = true }\nserde = { workspace = true }\n\
         [target.'cfg(windows)'.dependencies]\nlocal = { path = \"../local\" }\n\
         [dev-dependencies]\ntest_only = { path = \"../test_only\" }\n",
    );
    write("crates/core/Cargo.toml", "[dependencies]\n");
    write("crates/local/Cargo.toml", "[dependencies]\n");
    let mut found: Vec<_> = shipping_crates(&root)
        .iter()
        .map(|dir| dir.file_name().unwrap().to_string_lossy().into_owned())
        .collect();
    found.sort();
    assert_eq!(found, ["core", "kirin_hypha_ffi", "local"]);
    std::fs::remove_dir_all(&root).unwrap();
}

fn tempfile_root() -> PathBuf {
    let root = std::env::temp_dir().join(format!("ffi-unwind-scope-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    root
}
