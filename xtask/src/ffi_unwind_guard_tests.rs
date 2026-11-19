use super::scope::{rust_sources, shipping_crates, workspace_metadata};
use super::*;
use std::path::Path;

fn root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap()
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
    let crates = shipping_crates(&workspace_metadata(root)).unwrap();
    for name in ["kirin_hypha_ffi", "kirin_measure"] {
        assert!(
            crates.iter().any(|c| c.name == name),
            "{name} is not scanned"
        );
    }
    let crate_names: Vec<_> = crates.iter().map(|c| c.name.as_str()).collect();
    let mut names = Vec::new();
    let mut failures = Vec::new();
    for dir in crates.iter().map(|c| &c.dir) {
        let mut files = Vec::new();
        rust_sources(&dir.join("src"), &mut files);
        let sources: Vec<_> = files
            .iter()
            .map(|file| std::fs::read_to_string(file).expect("UTF-8 source"))
            .collect();
        let macros: Vec<String> = sources.iter().flat_map(|s| defined_macros(s)).collect();
        for (file, source) in files.iter().zip(&sources) {
            let scan = scan_with(source, &macros, &crate_names);
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
    let crates = shipping_crates(&workspace_metadata(root())).unwrap();
    for legacy in ["hypha_pre", "hypha_post", "hypha_gui"] {
        assert!(!crates.iter().any(|c| c.name == legacy), "{legacy} ships");
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
crate::entry!(qualified);
use crate::entry;
use std::{thread_local, fmt::Debug as _};
fn captures<'a>(x: &'a u8) -> impl Sized + use<'a> { thread_local! { static C: u8 = 0; } x }
thread_local! { static CELL: u8 = const { 0 }; }
const _: () = std::assert!(1 == 1);
mod tests { include!("sibling_tests.rs"); }
fn body_macros() { log::warn!("x"); let _ = format!("{}", 1); }
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
#[no_mangle]
pub extern "C" fn default_runs_code() -> Snapshot { catch_unwind(|| x()).unwrap_or_default() }
#[no_mangle]
pub extern "C" fn method_fallback() -> u8 { catch_unwind(|| 1).unwrap_or(LIMIT.min(2)) }
"#;
    let (entries, problems) = summary(source);
    assert_eq!(entries.len(), 9);
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
        "use plugin::nih_export_vst3 as register;\nregister!(Plugin);",
        "impl Plugin { register!(); }",
        "use plugin::emit as thread_local;\nthread_local!(Plugin);",
        "pub use plugin::{emit as r#assert};",
        "use plugin::thread_local;",
        "use ::plugin::{other, include};",
        "use crate::emit as include;",
        "macro_rules! entry { () => {} }\nuse plugin::emit as entry;",
        "include!(\"../generated/exports.rs\");",
        "include!(concat!(env!(\"OUT_DIR\"), \"/exports.rs\"));",
    ] {
        let (_, problems) = summary(source);
        assert!(!problems.is_empty(), "accepted: {source}");
    }
}

// These are inert scanner inputs. No imported crate or macro is compiled or executed.
#[test]
fn untrusted_glob_imports_cannot_supply_allowlisted_item_macros() {
    for source in [
        "use external::*; thread_local! {}",
        "pub use ::external::{macros::*}; assert!(true);",
        "use {external::macros::*, std::fmt::Debug}; include!(\"sibling.rs\");",
        "mod nested { mod external {} } use external::*; thread_local! {}",
    ] {
        let (_, problems) = summary(source);
        assert!(
            !problems.is_empty(),
            "accepted an unverified glob: {source}"
        );
    }
}

#[test]
fn macro_use_imports_cannot_supply_allowlisted_item_macros() {
    for source in [
        "#[macro_use] extern crate external; thread_local! {}",
        "#[macro_use(thread_local)] extern crate external; thread_local! {}",
        "#[macro_use] mod external; assert!(true);",
    ] {
        let (_, problems) = summary(source);
        assert!(
            !problems.is_empty(),
            "accepted an unverified macro import: {source}"
        );
    }
}

#[test]
fn ordinary_imports_and_text_that_mentions_macro_imports_remain_valid() {
    let source = r##"
use std::{thread_local, fmt::Debug};
use core::assert;
use ::std::fmt::*;
mod local { pub struct Local; }
use local::*;
use external::OrdinaryType;
macro_rules! local_entry { () => {} }
use crate::local_entry;
local_entry!();
thread_local! { static CELL: u8 = const { 0 }; }
const _: () = assert!(true);
// #[macro_use] mod external;
/* use external::*; */
const TEXT: &str = "#[macro_use] extern crate external; use external::*;";
"##;
    let (_, problems) = summary(source);
    assert!(problems.is_empty(), "false positive: {problems:?}");
}

// Cargo-shaped, in-memory metadata only: no registry access, dependency installation or build.
fn third_party_feature_graph(kind: serde_json::Value, capi: bool) -> serde_json::Value {
    use serde_json::json;
    let dependency = |name: &str, kind: serde_json::Value| {
        json!({"name": name, "source": "registry+https://example.invalid/index",
            "kind": kind, "features": [], "uses_default_features": true, "optional": false})
    };
    let node_dependency = |name: &str, kind: serde_json::Value| json!({"name": name, "pkg": name, "dep_kinds": [{"kind": kind, "target": null}]});
    let enabled = if capi { "capi" } else { "capi_docs" };
    json!({
        "workspace_members": ["kirin_hypha_ffi"],
        "packages": [
            {"name": "kirin_hypha_ffi", "id": "kirin_hypha_ffi", "source": null,
             "manifest_path": "/fixture/ffi/Cargo.toml", "features": {},
             "dependencies": [dependency("bridge", kind.clone())]},
            {"name": "bridge", "id": "bridge",
             "source": "registry+https://example.invalid/index",
             "manifest_path": "/fixture/registry/bridge/Cargo.toml",
             "features": {"default": [format!("leaf/{enabled}")]},
             "dependencies": [dependency("leaf", serde_json::Value::Null)]},
            {"name": "leaf", "id": "leaf",
             "source": "registry+https://example.invalid/index",
             "manifest_path": "/fixture/registry/leaf/Cargo.toml",
             "features": {"capi": [], "capi_docs": []},
             "dependencies": []}
        ],
        "resolve": {"root": "kirin_hypha_ffi", "nodes": [
            {"id": "kirin_hypha_ffi", "features": [], "dependencies": ["bridge"],
             "deps": [node_dependency("bridge", kind)]},
            {"id": "bridge", "features": ["default"], "dependencies": ["leaf"],
             "deps": [node_dependency("leaf", serde_json::Value::Null)]},
            {"id": "leaf", "features": [enabled], "dependencies": [], "deps": []}
        ]}
    })
}

#[test]
fn a_linked_third_party_dependency_cannot_enable_capi_transitively() {
    let metadata = third_party_feature_graph(serde_json::Value::Null, true);
    let problem = shipping_crates(&metadata).expect_err("accepted a transitive C API feature");
    assert!(
        problem.contains("C API") || problem.contains("capi"),
        "rejected for an unrelated reason: {problem}"
    );
}

#[test]
fn third_party_features_with_similar_names_are_not_c_api_exports() {
    let metadata = third_party_feature_graph(serde_json::Value::Null, false);
    let crates = shipping_crates(&metadata).expect("benign transitive features");
    let names: Vec<_> = crates.iter().map(|item| item.name.as_str()).collect();
    assert_eq!(
        names,
        ["kirin_hypha_ffi"],
        "third-party source must not be scanned as owned code"
    );
}

#[test]
fn dev_and_build_only_third_party_capi_features_are_not_shipping_links() {
    for kind in ["dev", "build"] {
        let metadata = third_party_feature_graph(serde_json::Value::String(kind.into()), true);
        let crates = shipping_crates(&metadata).expect("non-shipping dependency");
        assert_eq!(crates.len(), 1, "included a {kind} dependency");
        assert_eq!(crates[0].name, "kirin_hypha_ffi");
    }
}

#[test]
fn scanned_workspace_roots_allow_local_globs_but_not_external_ones() {
    let roots = ["kirin_measure"];
    assert!(scan_with("use kirin_measure::band::*;", &[], &roots)
        .problems
        .is_empty());
    assert!(!scan_with("use external::band::*;", &[], &roots)
        .problems
        .is_empty());
}

#[test]
fn unlinked_capi_nodes_and_disabled_capi_declarations_do_not_fail() {
    let mut metadata = third_party_feature_graph(serde_json::Value::Null, false);
    metadata["resolve"]["nodes"]
        .as_array_mut()
        .unwrap()
        .push(serde_json::json!({
            "id": "unused", "features": ["capi"], "dependencies": [], "deps": []
        }));
    assert!(shipping_crates(&metadata).is_ok());
}

#[test]
fn bare_function_paths_are_guarded_without_evaluating_a_call_first() {
    for argument in [
        "shutdown",
        "service::shutdown",
        "::service::shutdown",
        "crate::service::shutdown",
        "self::shutdown",
        "super::shutdown",
    ] {
        let source = format!("pub extern \"C\" fn entry() {{ let _ = catch_unwind({argument}); }}");
        let (entries, problems) = summary(&source);
        assert_eq!(entries, [("entry".into(), true)], "{argument}");
        assert!(problems.is_empty(), "{problems:?}");
    }
}

#[test]
fn function_argument_construction_must_not_execute_outside_the_guard() {
    for argument in [
        "service::make_callback()",
        "{ service::shutdown }",
        "callbacks[0]",
        "object.callback",
        "service::shutdown as fn()",
        "service::",
        "service shutdown",
        "",
        "(service::shutdown)",
    ] {
        for wrapped in [argument.to_owned(), format!("AssertUnwindSafe({argument})")] {
            let source =
                format!("pub extern \"C\" fn entry() {{ let _ = catch_unwind({wrapped}); }}");
            let (entries, _) = summary(&source);
            assert_eq!(entries, [("entry".into(), false)], "{wrapped}");
        }
    }
}
