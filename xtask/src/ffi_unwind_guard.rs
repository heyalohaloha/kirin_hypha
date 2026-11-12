//! Every Rust function a host can call through the C ABI catches its own panics.
//!
//! A panic that crosses `extern "C"` aborts the DAW together with the user's session. The rule is
//! checked over the sources rather than over a hand-kept list, so a new entry cannot be added
//! without a guard: each `extern "<abi>" fn` definition under `crates/*/src` must call
//! `catch_unwind` in its own body. Comments and string literals do not count.

use std::path::{Path, PathBuf};

#[derive(Debug, PartialEq)]
struct Entry {
    name: String,
    line: usize,
    guarded: bool,
}

/// True for bytes that are code, false inside comments and string or char literals.
/// Comment markers are byte patterns: the native storage guard refuses them between string literals.
fn code_mask(source: &[u8]) -> Vec<bool> {
    let mut mask = vec![true; source.len()];
    let mut i = 0;
    while i < source.len() {
        let rest = &source[i..];
        let end = if matches!(rest, [b'/', b'/', ..]) {
            rest.iter()
                .position(|&b| b == b'\n')
                .map_or(source.len(), |n| i + n)
        } else if matches!(rest, [b'/', b'*', ..]) {
            block_comment_end(source, i)
        } else if let Some(hashes) = raw_string_start(source, i) {
            raw_string_end(source, i, hashes)
        } else if rest[0] == b'"' {
            quoted_end(source, i, b'"')
        } else if rest[0] == b'\'' && is_char_literal(rest) {
            quoted_end(source, i, b'\'')
        } else {
            i += 1;
            continue;
        };
        mask[i..end].iter_mut().for_each(|code| *code = false);
        i = end;
    }
    mask
}

fn is_ident_byte(source: &[u8], index: usize) -> bool {
    source
        .get(index)
        .is_some_and(|&b| b.is_ascii_alphanumeric() || b == b'_' || b == b'$')
}

fn block_comment_end(source: &[u8], start: usize) -> usize {
    let (mut depth, mut i) = (0usize, start);
    while i + 1 < source.len() {
        match &source[i..i + 2] {
            [b'/', b'*'] => (depth, i) = (depth + 1, i + 2),
            [b'*', b'/'] => {
                (depth, i) = (depth - 1, i + 2);
                if depth == 0 {
                    return i;
                }
            }
            _ => i += 1,
        }
    }
    source.len()
}

/// `r"`, `r#"`, `br#"`: the number of `#` when a raw string starts here.
fn raw_string_start(source: &[u8], i: usize) -> Option<usize> {
    if is_ident_byte(source, i.wrapping_sub(1)) {
        return None;
    }
    let after_prefix = match source.get(i..)? {
        [b'b', b'r', ..] => i + 2,
        [b'r', ..] => i + 1,
        _ => return None,
    };
    let hashes = source[after_prefix..]
        .iter()
        .take_while(|&&b| b == b'#')
        .count();
    (source.get(after_prefix + hashes) == Some(&b'"')).then_some(hashes)
}

fn raw_string_end(source: &[u8], start: usize, hashes: usize) -> usize {
    let open = start + source[start..].iter().position(|&b| b == b'"').unwrap_or(0) + 1;
    let mut close = vec![b'"'];
    close.extend(std::iter::repeat_n(b'#', hashes));
    source[open..]
        .windows(close.len())
        .position(|window| window == close.as_slice())
        .map_or(source.len(), |n| open + n + close.len())
}

fn quoted_end(source: &[u8], open: usize, quote: u8) -> usize {
    let mut i = open + 1;
    while i < source.len() {
        match source[i] {
            b'\\' => i += 2,
            b if b == quote => return i + 1,
            _ => i += 1,
        }
    }
    source.len()
}

/// `'x'`, `'\n'`, `'\u{..}'` and multi-byte chars are literals; `'a` lifetimes and labels are not.
fn is_char_literal(rest: &[u8]) -> bool {
    if rest.get(1) == Some(&b'\\') {
        return true;
    }
    let text = std::str::from_utf8(&rest[1..rest.len().min(6)])
        .unwrap_or_else(|e| std::str::from_utf8(&rest[1..1 + e.valid_up_to()]).unwrap_or_default());
    let mut chars = text.chars();
    chars.next().is_some() && chars.next() == Some('\'')
}

fn skip_space(source: &[u8], mut i: usize) -> usize {
    while source.get(i).is_some_and(u8::is_ascii_whitespace) {
        i += 1;
    }
    i
}

fn ffi_entries(source: &str) -> Vec<Entry> {
    let bytes = source.as_bytes();
    let mask = code_mask(bytes);
    let mut entries = Vec::new();
    let mut search = 0;
    while let Some(found) = source[search..].find("extern") {
        let at = search + found;
        search = at + "extern".len();
        if !mask[at] || is_ident_byte(bytes, at.wrapping_sub(1)) || is_ident_byte(bytes, search) {
            continue;
        }
        let mut i = skip_space(bytes, search);
        if bytes.get(i) == Some(&b'"') {
            i = skip_space(bytes, quoted_end(bytes, i, b'"'));
        }
        if !source[i..].starts_with("fn") || is_ident_byte(bytes, i + 2) {
            continue;
        }
        let name_start = skip_space(bytes, i + 2);
        let name_end = (name_start..bytes.len())
            .find(|&n| !is_ident_byte(bytes, n))
            .unwrap_or(bytes.len());
        if name_end == name_start {
            continue; // `extern "C" fn(...)` is a function pointer type, not a definition.
        }
        let Some(body) = body_range(bytes, &mask, name_end) else {
            continue; // a declaration without a body
        };
        let guarded = body.clone().any(|n| {
            mask[n]
                && source[n..].starts_with("catch_unwind")
                && !is_ident_byte(bytes, n.wrapping_sub(1))
                && bytes.get(skip_space(bytes, n + "catch_unwind".len())) == Some(&b'(')
        });
        entries.push(Entry {
            name: source[name_start..name_end].to_string(),
            line: source[..at].matches('\n').count() + 1,
            guarded,
        });
    }
    entries
}

/// The braces of the body that follows a signature, or `None` when `;` ends it first.
fn body_range(source: &[u8], mask: &[bool], from: usize) -> Option<std::ops::Range<usize>> {
    let (mut nesting, mut open) = (0i32, None);
    for i in from..source.len() {
        if !mask[i] {
            continue;
        }
        match (source[i], open) {
            (b'(' | b'[', None) => nesting += 1,
            (b')' | b']', None) => nesting -= 1,
            (b';', None) if nesting == 0 => return None,
            (b'{', None) if nesting == 0 => (open, nesting) = (Some(i), 1),
            (b'{', Some(_)) => nesting += 1,
            (b'}', Some(start)) => {
                nesting -= 1;
                if nesting == 0 {
                    return Some(start..i + 1);
                }
            }
            _ => {}
        }
    }
    None
}

fn rust_sources(directory: &Path, out: &mut Vec<PathBuf>) {
    for entry in std::fs::read_dir(directory).expect("source directory") {
        let path = entry.expect("source entry").path();
        if path.is_dir() {
            rust_sources(&path, out);
        } else if path.extension().is_some_and(|e| e == "rs") {
            out.push(path);
        }
    }
}

#[test]
fn every_c_abi_entry_catches_its_own_panics() {
    let crates = Path::new(env!("CARGO_MANIFEST_DIR")).join("../crates");
    let mut files = Vec::new();
    for krate in std::fs::read_dir(&crates).expect("crates directory") {
        let src = krate.expect("crate entry").path().join("src");
        if src.is_dir() {
            rust_sources(&src, &mut files);
        }
    }
    let mut names = Vec::new();
    let mut unguarded = Vec::new();
    for file in &files {
        let source = std::fs::read_to_string(file).expect("UTF-8 source");
        for entry in ffi_entries(&source) {
            if !entry.guarded {
                unguarded.push(format!("{}:{} {}", file.display(), entry.line, entry.name));
            }
            names.push(entry.name);
        }
    }
    assert!(
        unguarded.is_empty(),
        "wrap these C ABI entries in catch_unwind and return their failure value:\n{}",
        unguarded.join("\n")
    );
    // The scan must see the audio-callback entries; an empty or blind scan proves nothing.
    for known in [
        "kirin_hypha_push_samples",
        "kirin_hypha_push_paired_analysis",
        "kirin_hypha_create",
    ] {
        assert!(names.iter().any(|n| n == known), "scan missed {known}");
    }
}

#[test]
fn scan_ignores_comments_strings_and_pointer_types() {
    let source = r##"
// pub extern "C" fn in_comment() {}
/* nested /* */ pub extern "C" fn in_block() {} */
const S: &str = "pub extern \"C\" fn in_string() {}";
const R: &str = r#"pub extern "C" fn in_raw() {}"#;
type Callback = unsafe extern "C" fn(u32) -> bool;
#[no_mangle]
pub unsafe extern "C" fn bare<'a>(out: *mut [u8; 4], _l: &'a u8) -> bool {
    let brace = '}';
    let _text = "catch_unwind(";
    out.is_null() && brace == '}'
}
#[no_mangle]
pub extern "C" fn comment_only() { // catch_unwind(
}
#[no_mangle]
pub extern "C" fn guarded() -> bool {
    std::panic::catch_unwind(|| true).unwrap_or(false)
}
macro_rules! entry { ($name:ident) => {
    pub extern "C" fn $name() { let _ = catch_unwind(|| ()); }
} }
"##;
    let entries = ffi_entries(source);
    let summary: Vec<_> = entries
        .iter()
        .map(|e| (e.name.as_str(), e.guarded))
        .collect();
    assert_eq!(
        summary,
        [
            ("bare", false),
            ("comment_only", false),
            ("guarded", true),
            ("$name", true),
        ]
    );
}
