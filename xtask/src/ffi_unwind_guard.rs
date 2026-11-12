//! Every Rust function the shipping product exports through the C ABI runs inside `catch_unwind`.
//!
//! A panic that crosses `extern "C"` aborts the DAW together with the user's session. The rule is
//! checked over the sources rather than over a hand-kept list, so a new entry cannot be added
//! without a guard. It is structural: the whole body of an `extern "<abi>" fn` is one
//! `catch_unwind(..)` of a closure (or of a function path), either discarded with `let _ = ..;` or
//! followed by `.unwrap_or(<constant>)`. Nothing else in the body runs outside the guard.
//!
//! Scope: the JUCE product links one Rust artifact, the `kirin_hypha_ffi` staticlib
//! (`juce_shell/CMakeLists.txt` builds `-p kirin_hypha_ffi`), so the scanned crates are that crate
//! and its workspace path dependencies. The legacy nih-plug crates (PRE/POST/GUI) are kept only to
//! verify the old VST3 identity and state; they are not linked into the JUCE binaries, and their
//! `nih_export_vst3!` exports come from vendored nih-plug code that this rule cannot guard. The
//! check fails if a shipping crate starts depending on nih-plug, exports a symbol the scanner does
//! not see as a guarded entry (`#[no_mangle]`/`export_name` elsewhere), invokes an export macro, or
//! shadows `catch_unwind`. Third-party crates are out of scope; the vendored ebur128 C API stays
//! behind its `capi` feature, which no shipping manifest enables.

use std::path::{Path, PathBuf};

#[cfg(test)]
#[path = "ffi_unwind_guard_tests.rs"]
mod tests;

#[derive(Clone, Copy, PartialEq, Debug)]
enum Kind {
    Ident,
    Punct,
    Literal,
}

#[derive(Debug)]
struct Token<'a> {
    text: &'a str,
    kind: Kind,
    start: usize,
}

impl Token<'_> {
    fn is(&self, text: &str) -> bool {
        self.kind != Kind::Literal && self.text == text
    }
}

#[derive(Debug, PartialEq)]
struct Entry {
    name: String,
    line: usize,
    guarded: bool,
}

#[derive(Debug, Default)]
struct Scan {
    entries: Vec<Entry>,
    /// Exports the scanner cannot see as entries, export macros, and `catch_unwind` shadowing.
    problems: Vec<String>,
}

fn is_ident_byte(source: &[u8], index: usize) -> bool {
    source
        .get(index)
        .is_some_and(|&b| b.is_ascii_alphanumeric() || b == b'_' || b == b'$')
}

/// Code tokens only: comments and whitespace separate tokens and never reach the rules.
/// Comment markers are byte patterns: the native storage guard refuses them between string literals.
fn tokens(source: &str) -> Vec<Token<'_>> {
    let bytes = source.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let rest = &bytes[i..];
        let (end, kind) = if rest[0].is_ascii_whitespace() {
            i += 1;
            continue;
        } else if matches!(rest, [b'/', b'/', ..]) {
            i = rest
                .iter()
                .position(|&b| b == b'\n')
                .map_or(bytes.len(), |n| i + n);
            continue;
        } else if matches!(rest, [b'/', b'*', ..]) {
            i = block_comment_end(bytes, i);
            continue;
        } else if let Some(hashes) = raw_string_start(bytes, i) {
            (raw_string_end(bytes, i, hashes), Kind::Literal)
        } else if matches!(rest, [b'b', b'"' | b'\'', ..])
            && !is_ident_byte(bytes, i.wrapping_sub(1))
        {
            (quoted_end(bytes, i + 1, rest[1]), Kind::Literal)
        } else if rest[0] == b'"' {
            (quoted_end(bytes, i, b'"'), Kind::Literal)
        } else if rest[0] == b'\'' && is_char_literal(rest) {
            (quoted_end(bytes, i, b'\''), Kind::Literal)
        } else if rest[0] == b'\'' || is_ident_byte(bytes, i) {
            let tail = (i + 1..bytes.len()).find(|&n| !is_ident_byte(bytes, n));
            (tail.unwrap_or(bytes.len()), Kind::Ident)
        } else if matches!(rest, [b':', b':', ..] | [b'|', b'|', ..] | [b'-', b'>', ..]) {
            (i + 2, Kind::Punct)
        } else {
            let width = source[i..].chars().next().map_or(1, char::len_utf8);
            (i + width, Kind::Punct)
        };
        out.push(Token {
            text: &source[i..end],
            kind,
            start: i,
        });
        i = end;
    }
    out
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

/// Index of the bracket that closes the one at `open`, counting all three bracket kinds.
fn closing(tokens: &[Token], open: usize) -> Option<usize> {
    let mut depth = 0i32;
    for (index, token) in tokens.iter().enumerate().skip(open) {
        match token.text {
            "(" | "[" | "{" if token.kind == Kind::Punct => depth += 1,
            ")" | "]" | "}" if token.kind == Kind::Punct => {
                depth -= 1;
                if depth == 0 {
                    return Some(index);
                }
            }
            _ => {}
        }
    }
    None
}

fn starts_with(tokens: &[Token], texts: &[&str]) -> bool {
    tokens.len() >= texts.len() && tokens.iter().zip(texts).all(|(t, text)| t.is(text))
}

/// Skip an optional `std::panic::` or `panic::` prefix before `name`.
fn after_path<'t, 'a>(tokens: &'t [Token<'a>], name: &str) -> Option<&'t [Token<'a>]> {
    for prefix in [&["std", "::", "panic", "::"][..], &["panic", "::"], &[]] {
        if starts_with(tokens, prefix) && tokens.get(prefix.len()).is_some_and(|t| t.is(name)) {
            return Some(&tokens[prefix.len() + 1..]);
        }
    }
    None
}

/// The whole body is `[let _ =] catch_unwind(<closure or path>)` plus `;` or a constant fallback.
fn body_is_guarded(body: &[Token]) -> bool {
    let (rest, discarded) = if starts_with(body, &["let", "_", "="]) {
        (&body[3..], true)
    } else {
        (body, false)
    };
    let Some(call) = after_path(rest, "catch_unwind") else {
        return false;
    };
    let Some(close) = call.first().filter(|t| t.is("(")).and(closing(call, 0)) else {
        return false;
    };
    let (argument, tail) = (&call[1..close], &call[close + 1..]);
    if !guarded_argument(argument) {
        return false;
    }
    if discarded {
        return tail.len() == 1 && tail[0].is(";");
    }
    tail.is_empty()
        || (starts_with(tail, &[".", "unwrap_or_default", "(", ")"]) && tail.len() == 4)
        || (starts_with(tail, &[".", "unwrap_or", "("])
            && closing(tail, 2) == Some(tail.len() - 1)
            && constant(&tail[3..tail.len() - 1]))
}

fn guarded_argument(argument: &[Token]) -> bool {
    let closure = |t: &[Token]| {
        let t = if starts_with(t, &["move"]) {
            &t[1..]
        } else {
            t
        };
        t.first()
            .is_some_and(|first| first.is("||") || first.is("|"))
    };
    let path = |t: &[Token]| !t.is_empty() && t.iter().all(|t| t.kind == Kind::Ident || t.is("::"));
    if closure(argument) {
        return true;
    }
    let Some(wrapped) = after_path(argument, "AssertUnwindSafe") else {
        return false;
    };
    wrapped.first().is_some_and(|t| t.is("("))
        && closing(wrapped, 0) == Some(wrapped.len() - 1)
        && (closure(&wrapped[1..wrapped.len() - 1]) || path(&wrapped[1..wrapped.len() - 1]))
}

/// A fallback evaluated after the guard returns: names, literals and casts only, or a null pointer.
fn constant(tokens: &[Token]) -> bool {
    !tokens.is_empty()
        && tokens
            .iter()
            .enumerate()
            .all(|(index, token)| match token.kind {
                Kind::Literal | Kind::Ident => true,
                Kind::Punct => match token.text {
                    "::" | "-" => true,
                    "(" => index > 0 && matches!(tokens[index - 1].text, "null" | "null_mut"),
                    ")" => index > 0 && tokens[index - 1].is("("),
                    _ => false,
                },
            })
}

fn scan(source: &str) -> Scan {
    let tokens = tokens(source);
    let line = |at: usize| source[..at].matches('\n').count() + 1;
    let mut scan = Scan::default();
    let mut entry_fns = Vec::new();
    for (at, token) in tokens.iter().enumerate() {
        if !token.is("extern") {
            continue;
        }
        let mut next = at + 1;
        if tokens.get(next).is_some_and(|t| t.kind == Kind::Literal) {
            next += 1;
        }
        let (Some(keyword), Some(name)) = (tokens.get(next), tokens.get(next + 1)) else {
            continue;
        };
        if !keyword.is("fn") || name.kind != Kind::Ident {
            continue; // `extern crate`, `extern "C" {}`, or a function pointer type
        }
        let mut nesting = 0;
        let mut body = None;
        for index in next + 2..tokens.len() {
            match tokens[index].text {
                "(" | "[" | "<" => nesting += 1,
                ")" | "]" | ">" => nesting -= 1,
                ";" if nesting <= 0 => break,
                "{" if nesting <= 0 => {
                    body = closing(&tokens, index).map(|close| (index + 1, close));
                    break;
                }
                _ => {}
            }
        }
        let Some((open, close)) = body else {
            continue; // a declaration without a body
        };
        entry_fns.push(next);
        scan.entries.push(Entry {
            name: name.text.to_string(),
            line: line(token.start),
            guarded: body_is_guarded(&tokens[open..close]),
        });
    }
    let macros: Vec<&str> = tokens
        .windows(3)
        .filter(|w| w[0].is("macro_rules") && w[1].is("!"))
        .map(|w| w[2].text)
        .collect();
    for (at, token) in tokens.iter().enumerate() {
        let next = tokens.get(at + 1);
        if token.kind == Kind::Ident
            && token.text.contains("export")
            && token.text != "export_name"
            && (next.is_some_and(|t| t.is("!")) || attribute_name(&tokens, at))
            && !macros.contains(&token.text)
        {
            scan.problems.push(format!(
                "line {}: export macro {}",
                line(token.start),
                token.text
            ));
        }
        if (token.is("no_mangle") || token.is("export_name")) && !attached(&tokens, at, &entry_fns)
        {
            scan.problems.push(format!(
                "line {}: export without a seen entry",
                line(token.start)
            ));
        }
        let named = if token.is("macro_rules") {
            tokens.get(at + 2)
        } else {
            next
        };
        let shadow = [
            "fn",
            "as",
            "struct",
            "enum",
            "type",
            "union",
            "trait",
            "macro_rules",
        ]
        .iter()
        .any(|d| token.is(d));
        if shadow && named.is_some_and(|t| t.is("catch_unwind") || t.is("AssertUnwindSafe")) {
            scan.problems
                .push(format!("line {}: shadows the std guard", line(token.start)));
        }
    }
    scan
}

/// `#[name`, `#[path::name` or `#![name`: an attribute, possibly a procedural macro that exports.
fn attribute_name(tokens: &[Token], mut at: usize) -> bool {
    while at >= 2 && tokens[at - 1].is("::") && tokens[at - 2].kind == Kind::Ident {
        at -= 2;
    }
    at >= 2 && tokens[at - 1].is("[") && (tokens[at - 2].is("#") || tokens[at - 2].is("!"))
}

/// The item an export attribute marks is the next `fn`, and it must be a discovered entry.
fn attached(tokens: &[Token], attribute: usize, entry_fns: &[usize]) -> bool {
    let mut index = attribute;
    while index < tokens.len() && !tokens[index].is("]") {
        index += 1;
    }
    for (offset, token) in tokens.iter().enumerate().skip(index + 1) {
        if token.is("fn") {
            return entry_fns.contains(&offset);
        }
        if token.is(";") || token.is("{") || token.is("}") {
            return false;
        }
    }
    false
}

/// `kirin_hypha_ffi` and every workspace path crate it links (not dev or build dependencies).
fn shipping_crates(root: &Path) -> Vec<PathBuf> {
    let manifest = |dir: &Path| std::fs::read_to_string(dir.join("Cargo.toml")).expect("manifest");
    let root = &normalize(root);
    let workspace = manifest(root);
    let mut pending = vec![root.join("crates/kirin_hypha_ffi")];
    let mut found: Vec<PathBuf> = Vec::new();
    while let Some(dir) = pending.pop() {
        if found.contains(&dir) {
            continue;
        }
        let text = manifest(&dir);
        let mut section = String::new();
        for line in text.lines().map(str::trim) {
            if line.starts_with('[') {
                section = line.to_string();
                continue;
            }
            let linked = section == "[dependencies]"
                || section.starts_with("[target.") && section.ends_with(".dependencies]");
            let Some((name, spec)) = line.split_once('=').filter(|_| linked) else {
                continue;
            };
            let (spec, base) = if spec.contains("workspace = true") {
                let spec = dependency_spec(&workspace, name.trim()).unwrap_or_default();
                (spec, root.as_path())
            } else {
                (spec.to_string(), dir.as_path())
            };
            if let Some(path) = spec
                .split("path = \"")
                .nth(1)
                .and_then(|p| p.split('"').next())
            {
                pending.push(normalize(&base.join(path)));
            }
        }
        found.push(dir);
    }
    found
}

/// A `{ workspace = true }` dependency's spec in the root manifest; its path is root-relative.
fn dependency_spec(workspace: &str, name: &str) -> Option<String> {
    let table = workspace.split("[workspace.dependencies]").nth(1)?;
    let line = table
        .lines()
        .take_while(|l| !l.starts_with('['))
        .find(|l| l.split('=').next().map(str::trim) == Some(name))?;
    Some(line.split_once('=')?.1.to_string())
}

fn normalize(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in path.components() {
        match part {
            std::path::Component::ParentDir => {
                out.pop();
            }
            std::path::Component::CurDir => {}
            other => out.push(other),
        }
    }
    out
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
