use super::tests::summary;

fn check(imports: &str, fallback: &str, valid: bool) {
    let source = format!(
        "{imports}\npub extern \"C\" fn entry() -> *mut u8 {{ \
         catch_unwind(|| work()).unwrap_or({fallback}) }}"
    );
    let (entries, _) = summary(&source);
    assert_eq!(entries, [("entry".into(), valid)], "{source}");
}

#[test]
fn only_standard_pointer_constructors_are_constant_calls() {
    for path in [
        "std::ptr::null_mut()",
        "core::ptr::null()",
        "::std::ptr::null()",
    ] {
        check("", path, true);
    }
    for path in [
        "null_mut()",
        "fake::null_mut()",
        "std::fake::null_mut()",
        "std::ptr::compute()",
        "std::ptr::null_mut(work())",
        "std::ptr::null_mut() as *mut u8",
        "factory().null_mut()",
    ] {
        check("", path, false);
    }
}

#[test]
fn standard_function_imports_and_renames_are_resolved() {
    for import in [
        "use std::ptr::null_mut as nil;",
        "use core::ptr::{null_mut as nil};",
        "use ::std::{ptr::{null as nil}, fmt::Debug};",
    ] {
        check(import, "nil()", true);
    }
    check("use std::ptr::null_mut;", "null_mut()", true);
}

#[test]
fn local_functions_modules_and_aliases_cannot_impersonate_null() {
    for import in [
        "fn null_mut() -> *mut u8 { work() }",
        "use fake::null_mut;",
        "use fake::compute as null_mut;",
    ] {
        check(import, "null_mut()", false);
    }
    for import in [
        "mod std { pub mod ptr { pub fn null_mut() {} } }",
        "use fake as std;",
        "extern crate fake as std;",
    ] {
        check(import, "std::ptr::null_mut()", false);
    }
    check("use fake::null_mut as nil;", "nil()", false);
    check("use std::ptr as pointers;", "pointers::null_mut()", false);
}

#[test]
fn alias_scope_and_shadowing_are_not_guessed() {
    for extra in [
        "fn nil() {}",
        "fn other(nil: fn()) {}",
        "fn other() { let nil = || panic!(); nil(); }",
        "mod inner { use fake::null_mut as nil; }",
        "fn other() { let f = |nil| nil; }",
    ] {
        check(
            &format!("use std::ptr::null_mut as nil; {extra}"),
            "nil()",
            false,
        );
    }
    check(
        "mod inner { use std::ptr::null_mut as nil; }",
        "nil()",
        false,
    );
    let source = "mod inner { use std::ptr::null_mut as nil; \
                  pub extern \"C\" fn entry() -> *mut u8 { \
                  catch_unwind(|| work()).unwrap_or(nil()) } }";
    assert_eq!(summary(source).0, [("entry".into(), false)]);
}

#[test]
fn comments_and_strings_do_not_create_null_imports_or_shadowing() {
    check("// use std::ptr::null_mut as nil;", "nil()", false);
    check(
        "use std::ptr::null_mut as nil; const TEXT: &str = \"fn nil() {}\";",
        "nil()",
        true,
    );
    check(
        "const MARKER: bool = true; /* mod std {} */",
        "std::ptr::null_mut()",
        true,
    );
}

#[test]
fn inactive_imports_macro_templates_and_globs_cannot_certify_aliases() {
    for import in [
        "#[cfg(any())] use std::ptr::null_mut as nil;",
        "#[cfg_attr(all(), cfg(any()))] use std::ptr::null_mut as nil;",
        "#[cfg(any())] pub use std::ptr::null_mut as nil;",
        "#[cfg(any())] pub(crate) use std::ptr::null_mut as nil;",
        "macro_rules! make { () => { use std::ptr::null_mut as nil; } }",
        "opaque!(; use std::ptr::null_mut as nil;);",
        "opaque![; use std::ptr::null_mut as nil;];",
        "use crate::*; use std::ptr::null_mut as nil;",
    ] {
        check(import, "nil()", false);
    }
}
